namespace LunaRelay;

/// <summary>
/// Gemini Live speech-to-speech pipeline (scaffold). The Google AI Live API requires an API key
/// that is not yet provisioned for this deployment. Until a key is configured this pipeline is
/// never selected by <see cref="VoicePipelineFactory"/> (it falls back to Classic). This type
/// exists so the seam and settings surface are stable; once a key is available the RunTurnAsync
/// body is implemented against the Gemini Live bidirectional streaming API.
/// </summary>
public sealed class GeminiLiveVoicePipeline : IVoicePipeline
{
    public string Id => VoicePipelineIds.GeminiLive;

    public Task RunTurnAsync(VoiceTurnRequest request, IVoiceTurnEvents events, CancellationToken cancellationToken)
        => throw new NotSupportedException(
            "Gemini Live is not yet enabled: configure GeminiLive:ApiKey to use this pipeline.");
}
