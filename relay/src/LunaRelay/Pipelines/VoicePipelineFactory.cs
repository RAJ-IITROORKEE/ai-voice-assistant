namespace LunaRelay;

/// <summary>Well-known speech pipeline ids.</summary>
public static class VoicePipelineIds
{
    public const string Classic = "classic";
    public const string AzureRealtime = "azure-realtime";
    public const string GeminiLive = "gemini-live";
}

/// <summary>
/// Resolves which speech pipeline handles a turn. The user's settings (Postgres) override the
/// configured default; unavailable pipelines fall back to Classic so a misconfigured setting
/// never breaks voice.
/// </summary>
public sealed class VoicePipelineFactory(
    RelayConfiguration configuration,
    ILogger<VoicePipelineFactory> logger)
{
    /// <summary>
    /// Pick the pipeline id for a turn given the user's settings. Returns one of
    /// <see cref="VoicePipelineIds"/>. Never returns an unavailable pipeline.
    /// </summary>
    public string ResolveId(DeviceSettings? settings)
    {
        string requested = !string.IsNullOrWhiteSpace(settings?.Pipeline)
            ? settings!.Pipeline.Trim().ToLowerInvariant()
            : configuration.Pipelines.Default.Trim().ToLowerInvariant();

        switch (requested)
        {
            case VoicePipelineIds.AzureRealtime:
                if (!string.IsNullOrWhiteSpace(configuration.AzureOpenAI.RealtimeModel) &&
                    !string.IsNullOrWhiteSpace(configuration.AzureOpenAI.Endpoint) &&
                    !string.IsNullOrWhiteSpace(configuration.AzureOpenAI.ApiKey))
                {
                    return VoicePipelineIds.AzureRealtime;
                }
                logger.LogWarning("azure-realtime requested but Azure OpenAI realtime is not configured; using classic");
                return VoicePipelineIds.Classic;

            case VoicePipelineIds.GeminiLive:
                if (!string.IsNullOrWhiteSpace(configuration.GeminiLive.ApiKey))
                {
                    return VoicePipelineIds.GeminiLive;
                }
                logger.LogWarning("gemini-live requested but no Gemini API key is configured; using classic");
                return VoicePipelineIds.Classic;

            case VoicePipelineIds.Classic:
            default:
                if (requested is not (VoicePipelineIds.Classic or ""))
                {
                    logger.LogWarning("Unknown pipeline '{Pipeline}'; using classic", requested);
                }
                return VoicePipelineIds.Classic;
        }
    }
}
