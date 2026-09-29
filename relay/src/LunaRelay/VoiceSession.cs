using System.IO;
using System.Net.WebSockets;
using System.Text;
using System.Threading.Channels;
using LunaRelay.Protocol;

namespace LunaRelay;

public sealed class VoiceSession(
    WebSocket socket,
    AssistantContext context,
    RelayConfiguration configuration,
    IAgentResponder agentResponder,
    IConversationStore memory,
    VoiceCatalog voices,
    StreamingTts tts,
    ILogger<VoiceSession> logger,
    InsForgeStore? insforge = null,
    VoicePipelineFactory? pipelineFactory = null,
    AzureRealtimeVoicePipeline? azureRealtime = null) : IAsyncDisposable
{
    private const int DevicePcmFrameBytes = 960;
    private readonly SemaphoreSlim _sendGate = new(1, 1);
    private readonly object _turnGate = new();
    private ActiveTurn? _activeTurn;
    private uint _speakerSequence;
    private long _nextSpeakerFrameDueMs;
    private const int SpeakerFrameDurationMs = 20;
    private const uint InitialSpeakerBurstFrames = 60;

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        await SendControlAsync(new ControlMessage { Type = "ready" }, cancellationToken);
        byte[] receiveBuffer = new byte[20 * 1024];

        while (socket.State == WebSocketState.Open && !cancellationToken.IsCancellationRequested)
        {
            using var message = new MemoryStream();
            WebSocketReceiveResult result;
            do
            {
                result = await socket.ReceiveAsync(receiveBuffer, cancellationToken);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    await CancelActiveTurnAsync();
                    await socket.CloseOutputAsync(WebSocketCloseStatus.NormalClosure, "closing", cancellationToken);
                    return;
                }
                if (message.Length + result.Count > 64 * 1024)
                {
                    throw new InvalidOperationException("WebSocket message exceeds protocol limit.");
                }
                message.Write(receiveBuffer, 0, result.Count);
            } while (!result.EndOfMessage);

            if (result.MessageType == WebSocketMessageType.Text)
            {
                await HandleControlAsync(message.ToArray(), cancellationToken);
            }
            else if (result.MessageType == WebSocketMessageType.Binary)
            {
                HandleAudio(message.ToArray());
            }
        }
    }

    private async Task HandleControlAsync(byte[] utf8, CancellationToken sessionToken)
    {
        ControlMessage message = RelayProtocol.DeserializeControl(utf8) ??
            throw new InvalidOperationException("Invalid control message.");
        switch (message.Type)
        {
            case "turn.start":
                if (message.TurnId is not uint startTurn || message.SampleRate != 16000 ||
                    message.Format != "pcm_s16le")
                {
                    throw new InvalidOperationException("Invalid turn.start message.");
                }
                await StartTurnAsync(startTurn, sessionToken);
                break;
            case "turn.commit":
                if (message.TurnId is uint commitTurn)
                {
                    CommitTurn(commitTurn, sessionToken);
                }
                break;
            case "turn.cancel":
                await CancelActiveTurnAsync(message.TurnId);
                break;
            default:
                logger.LogDebug("Ignoring unknown control type {ControlType}", message.Type);
                break;
        }
    }

    private async Task StartTurnAsync(uint turnId, CancellationToken sessionToken)
    {
        await CancelActiveTurnAsync();
        var cancellation = CancellationTokenSource.CreateLinkedTokenSource(sessionToken);

        // Resolve the pipeline for this turn (settings override default; unconfigured pipelines fall
        // back to classic). Resolution may need the owner's settings row, so do it up-front.
        DeviceSettings? startSettings = null;
        if (insforge is not null && context.OwnerId is not null)
        {
            try { startSettings = await insforge.LoadSettingsAsync(context.OwnerId, cancellation.Token); }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                logger.LogWarning(exception, "Could not load settings to resolve pipeline; using default");
            }
        }
        string pipelineId = pipelineFactory?.ResolveId(startSettings) ?? VoicePipelineIds.Classic;
        var micFrames = Channel.CreateUnbounded<byte[]>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = true,
        });

        bool isClassic = string.Equals(pipelineId, VoicePipelineIds.Classic, StringComparison.OrdinalIgnoreCase);
        StreamingSpeechTurn? speech = null;
        if (isClassic)
        {
            speech = new StreamingSpeechTurn(
                configuration.AzureSpeech, context.Profile.RecognitionLanguage, partial =>
            {
                _ = SendControlAsync(new ControlMessage
                {
                    Type = "transcript.delta",
                    TurnId = turnId,
                    Text = partial,
                }, cancellation.Token);
            });
        }

        var turn = new ActiveTurn(turnId, pipelineId, speech, micFrames, cancellation);
        lock (_turnGate)
        {
            _activeTurn = turn;
            _speakerSequence = 0;
            _nextSpeakerFrameDueMs = 0;
        }
        if (speech is not null)
        {
            await speech.StartAsync();
        }
        else if (turn.IsRealtime)
        {
            // Realtime S2S: open the session now so mic audio streams during the button hold.
            turn.RealtimeRun = RunAzureRealtimeTurnAsync(turn, startSettings, sessionToken);
        }
        await SendControlAsync(new ControlMessage { Type = "turn.ready", TurnId = turnId }, sessionToken);
        logger.LogInformation("Turn {TurnId} started (pipeline={PipelineId})", turnId, pipelineId);
    }

    /// <summary>Realtime S2S turn: stream mic PCM (upsampled to 24k) to Azure realtime, relay
    /// assistant PCM back to the device as it arrives. server_vad detects end-of-speech; on button
    /// release (commit) the mic channel closes and the pump finishes feeding the model.</summary>
    private async Task RunAzureRealtimeTurnAsync(
        ActiveTurn turn, DeviceSettings? settings, CancellationToken sessionToken)
    {
        if (azureRealtime is null)
        {
            await SendControlAsync(new ControlMessage { Type = "error",
                Text = "Realtime pipeline not configured." }, turn.Cancellation.Token);
            return;
        }
        try
        {
            await SendControlAsync(new ControlMessage { Type = "tts.start", TurnId = turn.TurnId },
                turn.Cancellation.Token);
            var turnContext = new SessionRealtimeTurnContext(this, configuration, context, turn, settings);
            await azureRealtime.RunTurnAsync(turnContext, turn.Cancellation.Token);
        }
        catch (OperationCanceledException)
        {
            throw;
        }
        catch (Exception exception)
        {
            logger.LogError(exception, "Realtime turn {TurnId} failed", turn.TurnId);
            await SendControlAsync(new ControlMessage { Type = "error",
                Text = $"Realtime failed: {exception.Message}" }, CancellationToken.None);
        }
    }

    /// <summary>Adapter exposing the active device session to the realtime pipeline.</summary>
    private sealed class SessionRealtimeTurnContext(
        VoiceSession session,
        RelayConfiguration configuration,
        AssistantContext context,
        ActiveTurn turn,
        DeviceSettings? settings) : AzureRealtimeTurnContext
    {
        public override RelayConfiguration Configuration { get; } = configuration;
        public override PerTurnLatency Latency => turn.Latency;
        public override string VoiceServiceName =>
            settings?.Voice ?? context.Profile.DefaultVoiceId;
        public override string Language => settings?.Language ?? "en-IN";

        public override ValueTask EmitTranscriptAsync(string text, bool isFinal, CancellationToken ct) =>
            new(session.SendControlAsync(new ControlMessage
            {
                Type = isFinal ? "transcript.final" : "transcript.delta",
                TurnId = turn.TurnId,
                Text = isFinal ? null : text,
            }, turn.Cancellation.Token));

        public override ValueTask EmitResponseDeltaAsync(string delta, CancellationToken ct) =>
            new(session.SendControlAsync(new ControlMessage
            {
                Type = "response.delta", TurnId = turn.TurnId, Text = delta,
            }, turn.Cancellation.Token));

        public override ValueTask EmitAssistantPcmAsync(ReadOnlyMemory<byte> pcm24k, CancellationToken ct) =>
            // Realtime output is already 24 kHz — exactly what the device speaker expects.
            session.QueueSpeakerFrame(turn.TurnId, pcm24k, turn.Cancellation.Token);

        public override async ValueTask EmitCompletedAsync(CancellationToken ct)
        {
            turn.Latency.MarkCompleted();
            await session.SendControlAsync(new ControlMessage { Type = "tts.end", TurnId = turn.TurnId },
                turn.Cancellation.Token);
            await session.SendControlAsync(new ControlMessage { Type = "turn.complete", TurnId = turn.TurnId },
                turn.Cancellation.Token);
        }

        public override async Task PumpAudioAsync(
            Func<ReadOnlyMemory<byte>, CancellationToken, Task> send, CancellationToken cancellationToken)
        {
            // 16 kHz mono PCM16 → 320 samples (640 bytes) per 20 ms frame.
            var carry = new MemoryStream();
            await foreach (byte[] frame in turn.MicFrames.Reader.ReadAllAsync(cancellationToken))
            {
                carry.Write(frame, 0, frame.Length);
                byte[] buffered = carry.GetBuffer();
                int available = (int)carry.Length;
                int offset = 0;
                const int frameBytes = 640;
                while (available - offset >= frameBytes)
                {
                    byte[] chunk = new byte[frameBytes];
                    Array.Copy(buffered, offset, chunk, 0, frameBytes);
                    await send(AzureRealtimeVoicePipeline.Resample16kTo24k(chunk), cancellationToken);
                    offset += frameBytes;
                }
                // Keep the remainder for the next frame.
                var remainder = new MemoryStream();
                if (offset < available)
                {
                    remainder.Write(buffered, offset, available - offset);
                }
                carry = remainder;
            }
            // Flush any trailing samples (pad to a full frame is unnecessary; server_vad tolerates it).
            if (carry.Length > 0)
            {
                await send(AzureRealtimeVoicePipeline.Resample16kTo24k(carry.ToArray()), cancellationToken);
            }
        }
    }

    private void HandleAudio(byte[] message)
    {
        if (!RelayProtocol.TryDecodeAudio(message, out AudioFrame frame) ||
            frame.Kind != AudioFrameKind.MicrophonePcm)
        {
            throw new InvalidOperationException("Invalid microphone audio frame.");
        }
        ActiveTurn? turn;
        lock (_turnGate)
        {
            turn = _activeTurn;
        }
        if (turn is null || turn.TurnId != frame.TurnId || turn.Committed)
        {
            return;
        }
        if (frame.Sequence != turn.NextMicrophoneSequence++)
        {
            throw new InvalidOperationException("Microphone audio sequence gap.");
        }
        turn.Latency.MarkFirstMic();
        if (turn.Speech is not null)
        {
            turn.Speech.Write(frame.Pcm.Span);
        }
        else
        {
            // Realtime/Gemini: buffer PCM frames for the turn-context pump to upsample+send.
            turn.MicFrames.Writer.TryWrite(frame.Pcm.ToArray());
        }
    }

    private void CommitTurn(uint turnId, CancellationToken sessionToken)
    {
        ActiveTurn? turn;
        lock (_turnGate)
        {
            turn = _activeTurn;
            if (turn is null || turn.TurnId != turnId || turn.Committed)
            {
                return;
            }
            turn.Committed = true;
        }
        // Close the mic channel so the realtime pump drains and finishes feeding the model.
        turn.MicFrames.Writer.TryComplete();
        _ = ProcessTurnAsync(turn, sessionToken);
    }

    private async Task ProcessTurnAsync(ActiveTurn turn, CancellationToken sessionToken)
    {
        // Realtime/Gemini turns are fully handled by RunAzureRealtimeTurnAsync (started at turn
        // start). Here we only await completion so errors surface and the turn cleans up.
        if (turn.IsRealtime)
        {
            if (turn.RealtimeRun is not null)
            {
                try { await turn.RealtimeRun; }
                catch (OperationCanceledException) { /* cancelled mid-turn */ }
            }
            return;
        }

        long started = Environment.TickCount64;
        try
        {
            string transcript = await turn.Speech!.CompleteAsync(turn.Cancellation.Token);
            turn.Latency.MarkSttFinal();
            await SendControlAsync(new ControlMessage
            {
                Type = "transcript.final",
                TurnId = turn.TurnId,
            }, turn.Cancellation.Token);
            logger.LogInformation("Turn {TurnId} STT final in {ElapsedMs} ms", turn.TurnId,
                Environment.TickCount64 - started);

            // Phase 3: per-user settings from InsForge Postgres override relay defaults each turn.
            DeviceSettings? syncSettings = null;
            if (insforge is not null && Guid.TryParse(context.OwnerId, out _))
            {
                try
                {
                    syncSettings = await insforge.LoadSettingsAsync(
                        context.OwnerId, turn.Cancellation.Token);
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    logger.LogDebug(exception, "Could not load settings for owner {OwnerId}", context.OwnerId);
                }
            }

            VoiceCommand? voiceCommand = VoiceCommands.TryParse(transcript, voices, out VoiceCommand parsed)
                ? parsed
                : null;
            VoiceProfile selectedVoice;
            string commandResponse = voiceCommand?.Response ?? string.Empty;
            if (voiceCommand?.Kind == VoiceCommandKind.Change)
            {
                selectedVoice = voiceCommand.Voice!;
                try
                {
                    bool saved = await memory.SaveVoiceAsync(
                        context.Conversation,
                        context.Profile.Memory,
                        selectedVoice.Name,
                        turn.StartedAt,
                        turn.Cancellation.Token);
                    if (!saved)
                    {
                        selectedVoice = voices.Resolve(
                            await memory.LoadVoiceAsync(
                                context.Conversation, context.Profile.Memory, turn.Cancellation.Token) ??
                            context.Profile.DefaultVoiceId);
                        commandResponse = $"Voice is already set to {selectedVoice.Name}.";
                    }
                }
                catch (OperationCanceledException)
                {
                    throw;
                }
                catch (Exception exception)
                {
                    logger.LogWarning(exception,
                        "Could not persist voice preference for conversation {Conversation}; using it for this turn",
                        context.Conversation.StorageKey);
                }
            }
            else
            {
                try
                {
                    selectedVoice = voices.Resolve(
                        await memory.LoadVoiceAsync(
                            context.Conversation, context.Profile.Memory, turn.Cancellation.Token) ??
                        context.Profile.DefaultVoiceId);
                }
                catch (OperationCanceledException)
                {
                    throw;
                }
                catch (Exception exception)
                {
                    logger.LogWarning(exception,
                        "Could not load voice preference for conversation {Conversation}; using default",
                        context.Conversation.StorageKey);
                    selectedVoice = voices.Resolve(context.Profile.DefaultVoiceId);
                }
            }
            // Settings from the web app (Postgres) take precedence over stored voice memory when set.
            if (voiceCommand?.Kind != VoiceCommandKind.Change &&
                !string.IsNullOrWhiteSpace(syncSettings?.Voice))
            {
                selectedVoice = ResolveSettingsVoice(syncSettings.Voice, selectedVoice);
            }
            logger.LogInformation("Turn {TurnId} using voice {VoiceName}", turn.TurnId, selectedVoice.Name);

            lock (_turnGate)
            {
                _speakerSequence = 0;
                _nextSpeakerFrameDueMs = Environment.TickCount64;
            }
            await SendControlAsync(new ControlMessage { Type = "tts.start", TurnId = turn.TurnId },
                turn.Cancellation.Token);

            async IAsyncEnumerable<string> TextChunks()
            {
                if (voiceCommand is not null)
                {
                    await SendControlAsync(new ControlMessage
                    {
                        Type = "response.delta",
                        TurnId = turn.TurnId,
                        Text = commandResponse,
                    }, turn.Cancellation.Token);
                    yield return commandResponse;
                    yield break;
                }
                await foreach (string delta in agentResponder.StreamTextAsync(
                                    new AssistantTurn(context, transcript), turn.Cancellation.Token))
                {
                    turn.Latency.MarkAgentFirstToken();
                    await SendControlAsync(new ControlMessage
                    {
                        Type = "response.delta",
                        TurnId = turn.TurnId,
                        Text = delta,
                    }, turn.Cancellation.Token);
                    yield return delta;
                }
                turn.Latency.MarkAgentComplete();
            }

            await tts.SynthesizeAsync(TextChunks(), selectedVoice.ServiceName,
                async (pcm, cancellationToken) =>
            {
                turn.Latency.MarkTtsFirstAudio();
                for (int offset = 0; offset < pcm.Length; offset += DevicePcmFrameBytes)
                {
                    int length = Math.Min(DevicePcmFrameBytes, pcm.Length - offset);
                    uint? sequence = await ReserveSpeakerSequenceAsync(turn, cancellationToken);
                    if (!sequence.HasValue)
                    {
                        return;
                    }
                    byte[] frame = RelayProtocol.EncodeAudio(
                        AudioFrameKind.SpeakerPcm, turn.TurnId, sequence.Value,
                        pcm.Span.Slice(offset, length));
                    await SendBinaryAsync(frame, cancellationToken);
                }
            }, turn.Cancellation.Token);

            turn.Latency.MarkTtsComplete();
            turn.Latency.MarkCompleted();
            await SendControlAsync(new ControlMessage { Type = "tts.end", TurnId = turn.TurnId },
                turn.Cancellation.Token);
            await SendControlAsync(new ControlMessage { Type = "turn.complete", TurnId = turn.TurnId },
                turn.Cancellation.Token);
            logger.LogInformation("Turn {TurnId} completed in {ElapsedMs} ms ({LatencySummary})",
                turn.TurnId, Environment.TickCount64 - started, turn.Latency.Summary());
        }
        catch (OperationCanceledException)
        {
            await TrySendControlAsync(new ControlMessage { Type = "turn.cancelled", TurnId = turn.TurnId },
                sessionToken);
        }
        catch (Exception exception)
        {
            logger.LogError(exception, "Turn {TurnId} failed", turn.TurnId);
            await TrySendControlAsync(new ControlMessage
            {
                Type = "turn.error",
                TurnId = turn.TurnId,
                Code = "pipeline_failed",
                Message = "The voice pipeline failed.",
            }, sessionToken);
        }
        finally
        {
            lock (_turnGate)
            {
                if (_activeTurn == turn)
                {
                    _activeTurn = null;
                }
            }
            await turn.DisposeAsync();
        }
    }

    private VoiceProfile ResolveSettingsVoice(string settingsVoice, VoiceProfile fallback)
    {
        // Friendly name (Default/Ava/Andrew/Brian) resolves via the catalog.
        if (voices.TryGet(settingsVoice, out VoiceProfile named))
        {
            return named;
        }
        // Raw Azure service voice name (e.g. en-IN-NeerjaNeural) — use directly.
        if (settingsVoice.Contains('-', StringComparison.Ordinal))
        {
            return new VoiceProfile(settingsVoice, settingsVoice, "unknown");
        }
        return fallback;
    }

    private async Task CancelActiveTurnAsync(uint? expectedTurnId = null)
    {
        ActiveTurn? turn;
        lock (_turnGate)
        {
            turn = _activeTurn;
            if (turn is null || (expectedTurnId.HasValue && expectedTurnId.Value != turn.TurnId))
            {
                return;
            }
            _activeTurn = null;
        }
        turn.Cancellation.Cancel();
        await turn.DisposeAsync();
    }

    private Task SendControlAsync(ControlMessage message, CancellationToken cancellationToken) =>
        SendAsync(RelayProtocol.SerializeControl(message), WebSocketMessageType.Text, cancellationToken);

    /// <summary>Send one 24 kHz speaker PCM buffer to the device, paced/sequenced like the classic
    /// TTS path. Used by the realtime pipeline, whose output already matches the device format.</summary>
    private async ValueTask QueueSpeakerFrame(
        uint turnId, ReadOnlyMemory<byte> pcm24k, CancellationToken cancellationToken)
    {
        ActiveTurn? turn;
        lock (_turnGate)
        {
            turn = _activeTurn;
        }
        if (turn is null || turn.TurnId != turnId)
        {
            return;
        }
        for (int offset = 0; offset < pcm24k.Length; offset += DevicePcmFrameBytes)
        {
            int length = Math.Min(DevicePcmFrameBytes, pcm24k.Length - offset);
            uint? sequence = await ReserveSpeakerSequenceAsync(turn, cancellationToken);
            if (!sequence.HasValue)
            {
                return;
            }
            byte[] frame = RelayProtocol.EncodeAudio(
                AudioFrameKind.SpeakerPcm, turnId, sequence.Value,
                pcm24k.Span.Slice(offset, length));
            await SendBinaryAsync(frame, cancellationToken);
        }
    }

    private async Task<uint?> ReserveSpeakerSequenceAsync(ActiveTurn turn,
                                                           CancellationToken cancellationToken)
    {
        while (true)
        {
            long due;
            lock (_turnGate)
            {
                if (_activeTurn != turn)
                {
                    return null;
                }
                if (_speakerSequence < InitialSpeakerBurstFrames)
                {
                    return _speakerSequence++;
                }
                due = _nextSpeakerFrameDueMs;
            }

            long now = Environment.TickCount64;
            if (due > now)
            {
                await Task.Delay(TimeSpan.FromMilliseconds(due - now), cancellationToken);
                continue;
            }

            lock (_turnGate)
            {
                if (_activeTurn != turn)
                {
                    return null;
                }
                now = Environment.TickCount64;
                if (_nextSpeakerFrameDueMs > now)
                {
                    continue;
                }
                uint sequence = _speakerSequence++;
                _nextSpeakerFrameDueMs = now + SpeakerFrameDurationMs;
                return sequence;
            }
        }
    }

    private Task SendBinaryAsync(byte[] bytes, CancellationToken cancellationToken) =>
        SendAsync(bytes, WebSocketMessageType.Binary, cancellationToken);

    private async Task TrySendControlAsync(ControlMessage message, CancellationToken cancellationToken)
    {
        try
        {
            await SendControlAsync(message, cancellationToken);
        }
        catch (Exception exception) when (exception is WebSocketException or OperationCanceledException)
        {
            logger.LogDebug(exception, "Could not send terminal control message");
        }
    }

    private async Task SendAsync(byte[] bytes, WebSocketMessageType type, CancellationToken cancellationToken)
    {
        await _sendGate.WaitAsync(cancellationToken);
        try
        {
            if (socket.State != WebSocketState.Open)
            {
                throw new WebSocketException("Device WebSocket is not open.");
            }
            await socket.SendAsync(bytes, type, true, cancellationToken);
        }
        finally
        {
            _sendGate.Release();
        }
    }

    public async ValueTask DisposeAsync()
    {
        await CancelActiveTurnAsync();
        _sendGate.Dispose();
    }

    private sealed class ActiveTurn(
        uint turnId,
        string pipelineId,
        StreamingSpeechTurn? speech,
        Channel<byte[]> micFrames,
        CancellationTokenSource cancellation) : IAsyncDisposable
    {
        private int _disposed;
        public uint TurnId { get; } = turnId;
        public string PipelineId { get; } = pipelineId;
        public StreamingSpeechTurn? Speech { get; } = speech;
        public Channel<byte[]> MicFrames { get; } = micFrames;
        public CancellationTokenSource Cancellation { get; } = cancellation;
        public DateTimeOffset StartedAt { get; } = DateTimeOffset.UtcNow;
        public PerTurnLatency Latency { get; } = new();
        public uint NextMicrophoneSequence { get; set; }
        public bool Committed { get; set; }

        /// <summary>The running realtime S2S session task (non-classic pipelines), started at
        /// turn start and completed when the model finishes its response.</summary>
        public Task? RealtimeRun { get; set; }

        public bool IsRealtime => !string.Equals(PipelineId, VoicePipelineIds.Classic, StringComparison.OrdinalIgnoreCase);

        public async ValueTask DisposeAsync()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
            {
                return;
            }
            MicFrames.Writer.TryComplete();
            if (Speech is not null)
            {
                await Speech.DisposeAsync();
            }
            Cancellation.Dispose();
        }
    }
}
