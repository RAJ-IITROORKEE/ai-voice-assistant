namespace LunaRelay;

/// <summary>
/// A speech pipeline owns one full device turn: it consumes microphone PCM and produces
/// transcript/response events plus assistant PCM to play. The relay's <see cref="VoiceSession"/>
/// keeps owning the device WebSocket protocol (framing, pacing, control messages); a pipeline
/// only decides HOW a turn is processed (classic STT->LLM->TTS vs realtime speech-to-speech).
/// </summary>
public interface IVoicePipeline
{
    /// <summary>Stable pipeline id used in settings and metrics: classic | azure-realtime | gemini-live.</summary>
    string Id { get; }

    /// <summary>
    /// Run a single turn. Implementations receive microphone frames via
    /// <see cref="IVoiceTurnSink.WriteAudioAsync"/> until <see cref="IVoiceTurnSink.CommitAudioAsync"/>,
    /// and stream events/PCM out through <paramref name="events"/>.
    /// </summary>
    Task RunTurnAsync(VoiceTurnRequest request, IVoiceTurnEvents events, CancellationToken cancellationToken);
}

/// <summary>Everything a pipeline needs to process one turn.</summary>
public sealed record VoiceTurnRequest(
    uint TurnId,
    AssistantContext Context,
    /// <summary>Per-user settings from Postgres (may be null when sync is off/unavailable).</summary>
    DeviceSettings? Settings,
    /// <summary>Resolved TTS voice service name for this turn.</summary>
    string VoiceServiceName,
    /// <summary>Recognition / response language (BCP-47, e.g. en-IN).</summary>
    string Language,
    /// <summary>Latency tracker; pipelines stamp stages into it.</summary>
    PerTurnLatency Latency);

/// <summary>Events a pipeline raises back to the device session.</summary>
public interface IVoiceTurnEvents
{
    /// <summary>Partial/final user transcript as it becomes available.</summary>
    ValueTask OnTranscriptAsync(string text, bool isFinal, CancellationToken cancellationToken);
    /// <summary>Partial assistant text (for on-device display / logging).</summary>
    ValueTask OnResponseDeltaAsync(string text, CancellationToken cancellationToken);
    /// <summary>A chunk of assistant PCM16 (24 kHz mono) ready to play.</summary>
    ValueTask OnAssistantPcmAsync(ReadOnlyMemory<byte> pcm, CancellationToken cancellationToken);
    /// <summary>The turn is fully processed (transcript + response + audio done).</summary>
    ValueTask OnCompletedAsync(CancellationToken cancellationToken);
}

/// <summary>Microphone ingress handle passed from the session into the running pipeline.</summary>
public interface IVoiceTurnSink
{
    ValueTask WriteAudioAsync(ReadOnlyMemory<byte> pcm, CancellationToken cancellationToken);
    ValueTask CommitAudioAsync(CancellationToken cancellationToken);
}

/// <summary>
/// Lightweight per-turn latency tracker. Stages are stamped as they complete and a compact
/// summary is logged at end-of-turn. Cheap enough to run on every turn.
/// </summary>
public sealed class PerTurnLatency
{
    private readonly long _start = Environment.TickCount64;
    private long? _sttFinal;
    private long? _agentFirstToken;
    private long? _agentComplete;
    private long? _ttsFirstAudio;
    private long? _ttsComplete;
    private long? _completed;

    public void MarkFirstMic() { /* turn start is the reference; kept for symmetry/clarity at call sites */ }
    public void MarkCompleted() => _completed ??= Environment.TickCount64;
    public void MarkSttFinal() => _sttFinal ??= Environment.TickCount64;
    public void MarkAgentFirstToken() => _agentFirstToken ??= Environment.TickCount64;
    public void MarkAgentComplete() => _agentComplete ??= Environment.TickCount64;
    public void MarkTtsFirstAudio() => _ttsFirstAudio ??= Environment.TickCount64;
    public void MarkTtsComplete() => _ttsComplete ??= Environment.TickCount64;

    private long? Ms(long? stamp) => stamp is null ? null : stamp.Value - _start;

    /// <summary>Total elapsed ms from turn start to now.</summary>
    public long Elapsed => Environment.TickCount64 - _start;

    public string Summary() =>
        $"stt={Fmt(_sttFinal)} agent1st={Fmt(_agentFirstToken)} agentDone={Fmt(_agentComplete)} " +
        $"tts1st={Fmt(_ttsFirstAudio)} ttsDone={Fmt(_ttsComplete)} e2e={Elapsed}ms";

    private string Fmt(long? stamp) => Ms(stamp)?.ToString() ?? "-";

    /// <summary>Structured metadata snapshot for persisting on the assistant message.</summary>
    public IReadOnlyDictionary<string, object?> ToMetadata(string pipelineId) =>
        new Dictionary<string, object?>
        {
            ["pipeline"] = pipelineId,
            ["stt_ms"] = Ms(_sttFinal),
            ["agent_first_token_ms"] = Ms(_agentFirstToken),
            ["agent_complete_ms"] = Ms(_agentComplete),
            ["tts_first_audio_ms"] = Ms(_ttsFirstAudio),
            ["tts_complete_ms"] = Ms(_ttsComplete),
            ["e2e_ms"] = Elapsed,
        };
}
