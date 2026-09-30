using System.Net.WebSockets;
using System.Text.Json;

namespace LunaRelay;

/// <summary>
/// Azure OpenAI Realtime speech-to-speech pipeline for gpt-realtime models (GA / v1 protocol).
/// Streams device microphone PCM (upsampled 16 kHz -> 24 kHz) to the Realtime WebSocket with
/// server-side VAD turn detection, and receives 24 kHz PCM16 assistant audio back directly — no
/// separate STT/TTS legs, so latency is lowest.
///
/// Uses the v1 GA endpoint (/openai/v1/realtime?model=...) and the nested audio.* session shape;
/// server events are response.output_audio.* / response.output_audio_transcript.*.
/// </summary>
public sealed class AzureRealtimeVoicePipeline
{
    private readonly ILogger<AzureRealtimeVoicePipeline> _logger;

    public AzureRealtimeVoicePipeline(ILogger<AzureRealtimeVoicePipeline> logger)
    {
        _logger = logger;
    }

    public string Id => VoicePipelineIds.AzureRealtime;

    public async Task RunTurnAsync(AzureRealtimeTurnContext turn, CancellationToken cancellationToken)
    {
        AzureOpenAiOptions options = turn.Configuration.AzureOpenAI;
        Uri endpoint = new(options.Endpoint.TrimEnd('/') + "/");
        string wsScheme = endpoint.Scheme == "https" ? "wss" : "ws";
        // GA v1 realtime endpoint — no api-version; model = the deployment name.
        string url = $"{wsScheme}://{endpoint.Host}/openai/v1/realtime" +
                     $"?model={Uri.EscapeDataString(options.RealtimeModel)}";

        using ClientWebSocket ws = new();
        ws.Options.SetRequestHeader("api-key", options.ApiKey);
        ws.Options.KeepAliveInterval = TimeSpan.FromSeconds(15);
        _logger.LogInformation("Realtime: connecting to {Url}", url.Replace(options.ApiKey ?? "", "***"));
        await ws.ConnectAsync(new Uri(url), cancellationToken);
        _logger.LogInformation("Realtime: WS connected (state={State})", ws.State);

        string language = (turn.Language ?? "en-IN").Split('-')[0];
        await SendJsonAsync(ws, new
        {
            type = "session.update",
            session = new
            {
                type = "realtime",
                instructions = "You are Luna, a concise and friendly voice assistant. " +
                               "Respond conversationally and keep answers short.",
                output_modalities = new[] { "audio" },
                audio = new
                {
                    input = new
                    {
                        format = new { type = "audio/pcm", rate = 24000 },
                        transcription = new { model = "whisper-1" },
                        turn_detection = new
                        {
                            type = "server_vad",
                            threshold = 0.5,
                            prefix_padding_ms = 300,
                            silence_duration_ms = 500,
                            create_response = true,
                            interrupt_response = true,
                        },
                    },
                    output = new
                    {
                        voice = MapToRealtimeVoice(turn.VoiceServiceName),
                        format = new { type = "audio/pcm", rate = 24000 },
                    },
                },
            },
        }, cancellationToken);

        var done = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        Exception? receiveError = null;
        Task receiver = Task.Run(async () =>
        {
            try
            {
                await ReceiveLoopAsync(ws, turn, cancellationToken);
            }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                receiveError = exception;
            }
            finally
            {
                done.TrySetResult();
            }
        }, cancellationToken);

        // Pump microphone audio (16 kHz from device -> upsample to 24 kHz) until commit.
        long framesSent = 0;
        long bytesSent = 0;
        await turn.PumpAudioAsync(
            (pcm16k, ct) =>
            {
                byte[] pcm24k = Resample16kTo24k(pcm16k);
                framesSent++;
                bytesSent += pcm24k.Length;
                return SendAudioAsync(ws, pcm24k, ct);
            },
            cancellationToken);
        _logger.LogInformation("Realtime: mic pump finished, frames={Frames} bytes24k={Bytes}", framesSent, bytesSent);

        // Wait for the model to finish responding (response.done) or error/cancel.
        await done.Task.WaitAsync(TimeSpan.FromSeconds(90), cancellationToken);
        if (receiveError is not null)
        {
            throw receiveError;
        }

        try { await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "done", CancellationToken.None); }
        catch { /* best effort */ }
        await receiver;
    }

    private static Task SendAudioAsync(ClientWebSocket ws, byte[] pcm24k, CancellationToken ct) =>
        pcm24k.Length == 0
            ? Task.CompletedTask
            : SendJsonAsync(ws, new
            {
                type = "input_audio_buffer.append",
                audio = Convert.ToBase64String(pcm24k),
            }, ct);

    private async Task ReceiveLoopAsync(
        ClientWebSocket ws, AzureRealtimeTurnContext turn, CancellationToken ct)
    {
        byte[] buffer = new byte[64 * 1024];
        using var message = new MemoryStream();
        while (ws.State == WebSocketState.Open && !ct.IsCancellationRequested)
        {
            message.SetLength(0);
            WebSocketReceiveResult result;
            do
            {
                result = await ws.ReceiveAsync(buffer, ct);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    _logger.LogInformation("Realtime: server closed WS (status={Status}, desc={Desc})",
                        ws.CloseStatus, ws.CloseStatusDescription);
                    return;
                }
                message.Write(buffer, 0, result.Count);
            } while (!result.EndOfMessage);

            if (result.MessageType == WebSocketMessageType.Text)
            {
                await HandleEventAsync(message.ToArray(), turn, ct);
            }
        }
    }

    private async Task HandleEventAsync(byte[] utf8, AzureRealtimeTurnContext turn, CancellationToken ct)
    {
        using JsonDocument doc = JsonDocument.Parse(utf8);
        JsonElement root = doc.RootElement;
        if (!root.TryGetProperty("type", out JsonElement typeEl))
        {
            return;
        }
        string eventType = typeEl.GetString() ?? "";
        switch (eventType)
        {
            case "session.created":
            case "session.updated":
                _logger.LogInformation("Realtime: {EventType}", eventType);
                break;
            case "input_audio_buffer.speech_started":
                _logger.LogInformation("Realtime: VAD speech_started");
                break;
            case "input_audio_buffer.speech_stopped":
                _logger.LogInformation("Realtime: VAD speech_stopped");
                break;
            case "input_audio_buffer.committed":
                _logger.LogInformation("Realtime: input committed");
                break;
            case "conversation.item.input_audio_transcription.delta":
                if (root.TryGetProperty("delta", out JsonElement td))
                {
                    await turn.EmitTranscriptAsync(td.GetString() ?? string.Empty, isFinal: false, ct);
                }
                break;
            case "conversation.item.input_audio_transcription.completed":
                turn.Latency.MarkSttFinal();
                string finalText = root.TryGetProperty("transcript", out JsonElement tf) ? tf.GetString() ?? "" : "";
                await turn.EmitTranscriptAsync(finalText, isFinal: true, ct);
                break;
            case "response.output_audio_transcript.delta":
                turn.Latency.MarkAgentFirstToken();
                if (root.TryGetProperty("delta", out JsonElement rd))
                {
                    await turn.EmitResponseDeltaAsync(rd.GetString() ?? string.Empty, ct);
                }
                break;
            case "response.output_audio.delta":
                turn.Latency.MarkTtsFirstAudio();
                if (root.TryGetProperty("delta", out JsonElement ad))
                {
                    byte[] pcm = Convert.FromBase64String(ad.GetString() ?? string.Empty);
                    await turn.EmitAssistantPcmAsync(pcm, ct);
                }
                break;
            case "response.output_audio.done":
                turn.Latency.MarkTtsComplete();
                _logger.LogInformation("Realtime: output_audio.done");
                break;
            case "response.done":
                turn.Latency.MarkAgentComplete();
                _logger.LogInformation("Realtime: response.done (turn complete)");
                await turn.EmitCompletedAsync(ct);
                break;
            case "error":
                string err = root.TryGetProperty("error", out JsonElement e) ? e.ToString() : "unknown";
                _logger.LogError("Realtime: server error {Error}", err);
                throw new InvalidOperationException($"Azure Realtime error: {err}");
        }
    }

    private static async Task SendJsonAsync(ClientWebSocket ws, object payload, CancellationToken ct)
    {
        byte[] bytes = JsonSerializer.SerializeToUtf8Bytes(payload);
        await ws.SendAsync(bytes, WebSocketMessageType.Text, endOfMessage: true, ct);
    }

    /// <summary>Realtime S2S voices supported by the GA realtime API.</summary>
    private static readonly HashSet<string> RealtimeVoices = new(StringComparer.OrdinalIgnoreCase)
    {
        "alloy", "ash", "ballad", "coral", "echo", "sage", "shimmer", "verse", "marin", "cedar",
    };

    /// <summary>
    /// The realtime API only accepts its own voice names, NOT Azure TTS names (e.g.
    /// "en-IN-NeerjaNeural"). If the configured voice is a valid realtime voice, use it;
    /// otherwise map an Azure TTS voice to the closest realtime voice, defaulting to "alloy".
    /// Neerja is a feminine Indian-English voice; "marin" is the closest natural feminine GA voice.
    /// </summary>
    private static string MapToRealtimeVoice(string? configuredVoice)
    {
        if (string.IsNullOrWhiteSpace(configuredVoice))
        {
            return "alloy";
        }
        string voice = configuredVoice.Trim();
        if (RealtimeVoices.Contains(voice))
        {
            return voice.ToLowerInvariant();
        }
        // Azure TTS service names look like "en-IN-NeerjaNeural" — map common ones.
        if (voice.Contains('-'))
        {
            if (voice.Contains("Neerja", StringComparison.OrdinalIgnoreCase) ||
                voice.Contains("Swara", StringComparison.OrdinalIgnoreCase) ||
                voice.Contains("Aditi", StringComparison.OrdinalIgnoreCase))
            {
                return "marin";
            }
            if (voice.Contains("Prabhat", StringComparison.OrdinalIgnoreCase) ||
                voice.Contains("Arjun", StringComparison.OrdinalIgnoreCase))
            {
                return "echo";
            }
            return "alloy";
        }
        return "alloy";
    }

    /// <summary>
    /// Linear-interpolation resample from 16 kHz PCM16 to 24 kHz PCM16 (factor 1.5).
    /// Good enough for voice; keeps the relay self-contained without a DSP dependency.
    /// </summary>
    public static byte[] Resample16kTo24k(ReadOnlyMemory<byte> pcm16)
    {
        int inSamples = pcm16.Length / 2;
        if (inSamples == 0)
        {
            return Array.Empty<byte>();
        }
        Span<short> src = stackalloc short[inSamples];
        for (int i = 0; i < inSamples; i++)
        {
            src[i] = BitConverter.ToInt16(pcm16.Span.Slice(i * 2, 2));
        }
        int outSamples = (int)(inSamples * 1.5);
        byte[] dest = new byte[outSamples * 2];
        for (int o = 0; o < outSamples; o++)
        {
            double pos = o / 1.5;
            int i0 = (int)pos;
            int i1 = Math.Min(i0 + 1, inSamples - 1);
            double frac = pos - i0;
            short sample = (short)(src[i0] + (short)((src[i1] - src[i0]) * frac));
            BitConverter.GetBytes(sample).CopyTo(dest, o * 2);
        }
        return dest;
    }
}

/// <summary>
/// Adapter that lets <see cref="AzureRealtimeVoicePipeline"/> drive a turn without knowing the
/// device session: <see cref="VoiceSession"/> implements the Emit*/Pump* members. Kept as an
/// abstract class so the pipeline has a single, testable surface.
/// </summary>
public abstract class AzureRealtimeTurnContext
{
    public abstract RelayConfiguration Configuration { get; }
    public abstract string VoiceServiceName { get; }
    public abstract string Language { get; }
    public abstract PerTurnLatency Latency { get; }

    public abstract ValueTask EmitTranscriptAsync(string text, bool isFinal, CancellationToken ct);
    public abstract ValueTask EmitResponseDeltaAsync(string text, CancellationToken ct);
    public abstract ValueTask EmitAssistantPcmAsync(ReadOnlyMemory<byte> pcm, CancellationToken ct);
    public abstract ValueTask EmitCompletedAsync(CancellationToken ct);
    public abstract Task PumpAudioAsync(
        Func<ReadOnlyMemory<byte>, CancellationToken, Task> sendAsync, CancellationToken ct);
}
