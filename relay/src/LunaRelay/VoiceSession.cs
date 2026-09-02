using System.Net.WebSockets;
using System.Text;
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
    ILogger<VoiceSession> logger) : IAsyncDisposable
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
        var speech = new StreamingSpeechTurn(
            configuration.AzureSpeech, context.Profile.RecognitionLanguage, partial =>
        {
            _ = SendControlAsync(new ControlMessage
            {
                Type = "transcript.delta",
                TurnId = turnId,
                Text = partial,
            }, cancellation.Token);
        });
        var turn = new ActiveTurn(turnId, speech, cancellation);
        lock (_turnGate)
        {
            _activeTurn = turn;
            _speakerSequence = 0;
            _nextSpeakerFrameDueMs = 0;
        }
        await speech.StartAsync();
        await SendControlAsync(new ControlMessage { Type = "turn.ready", TurnId = turnId }, sessionToken);
        logger.LogInformation("Turn {TurnId} recognition started", turnId);
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
        turn.Speech.Write(frame.Pcm.Span);
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
        _ = ProcessTurnAsync(turn, sessionToken);
    }

    private async Task ProcessTurnAsync(ActiveTurn turn, CancellationToken sessionToken)
    {
        long started = Environment.TickCount64;
        try
        {
            string transcript = await turn.Speech.CompleteAsync(turn.Cancellation.Token);
            await SendControlAsync(new ControlMessage
            {
                Type = "transcript.final",
                TurnId = turn.TurnId,
            }, turn.Cancellation.Token);
            logger.LogInformation("Turn {TurnId} STT final in {ElapsedMs} ms", turn.TurnId,
                Environment.TickCount64 - started);

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
                    await SendControlAsync(new ControlMessage
                    {
                        Type = "response.delta",
                        TurnId = turn.TurnId,
                        Text = delta,
                    }, turn.Cancellation.Token);
                    yield return delta;
                }
            }

            await tts.SynthesizeAsync(TextChunks(), selectedVoice.ServiceName,
                async (pcm, cancellationToken) =>
            {
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

            await SendControlAsync(new ControlMessage { Type = "tts.end", TurnId = turn.TurnId },
                turn.Cancellation.Token);
            await SendControlAsync(new ControlMessage { Type = "turn.complete", TurnId = turn.TurnId },
                turn.Cancellation.Token);
            logger.LogInformation("Turn {TurnId} completed in {ElapsedMs} ms", turn.TurnId,
                Environment.TickCount64 - started);
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
        StreamingSpeechTurn speech,
        CancellationTokenSource cancellation) : IAsyncDisposable
    {
        private int _disposed;
        public uint TurnId { get; } = turnId;
        public StreamingSpeechTurn Speech { get; } = speech;
        public CancellationTokenSource Cancellation { get; } = cancellation;
        public DateTimeOffset StartedAt { get; } = DateTimeOffset.UtcNow;
        public uint NextMicrophoneSequence { get; set; }
        public bool Committed { get; set; }

        public async ValueTask DisposeAsync()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0)
            {
                return;
            }
            await Speech.DisposeAsync();
            Cancellation.Dispose();
        }
    }
}
