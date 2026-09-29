namespace LunaRelay;

public sealed class RelayOptions
{
    public int ListenPort { get; init; } = 7443;
    public bool UseTls { get; init; } = true;
    public string DeviceToken { get; init; } = string.Empty;
    public string CertificatePath { get; init; } = "certs/relay.pfx";
    public string CertificatePassword { get; init; } = string.Empty;
}

public sealed class AzureSpeechOptions
{
    public string Key { get; init; } = string.Empty;
    public string Region { get; init; } = string.Empty;
    public string DefaultServiceVoice { get; init; } = "en-IN-NeerjaNeural";
}

public sealed class AzureOpenAiOptions
{
    public string Endpoint { get; init; } = string.Empty;
    public string ApiKey { get; init; } = string.Empty;
    public string Model { get; init; } = "DeepSeek-V4-Flash";
}

public sealed class FirestoreOptions
{
    public string ProjectId { get; init; } = string.Empty;
    public string ConversationCollection { get; init; } = "luna_agent_sessions";
    /// <summary>Azure user-assigned managed identity client id used for Workload Identity Federation.</summary>
    public string? AzureClientId { get; init; }
    /// <summary>GCP service account email impersonated through Workload Identity Federation.</summary>
    public string? GcpServiceAccount { get; init; }
    /// <summary>GCP Workload Identity Pool id.</summary>
    public string? WorkloadIdentityPool { get; init; }
    /// <summary>GCP OIDC provider id inside the pool.</summary>
    public string? WorkloadIdentityProvider { get; init; }
    /// <summary>GCP project number hosting the workload identity pool.</summary>
    public string? GcpProjectNumber { get; init; }
    /// <summary>Full STS audience for the pool provider.</summary>
    public string? WifAudience =>
        string.IsNullOrWhiteSpace(GcpProjectNumber) ||
        string.IsNullOrWhiteSpace(WorkloadIdentityPool) ||
        string.IsNullOrWhiteSpace(WorkloadIdentityProvider)
            ? null
            : $"//iam.googleapis.com/projects/{GcpProjectNumber}/locations/global/workloadIdentityPools/{WorkloadIdentityPool}/providers/{WorkloadIdentityProvider}";
}

public sealed record RelayConfiguration(
    RelayOptions Relay,
    AzureSpeechOptions AzureSpeech,
    AzureOpenAiOptions AzureOpenAI,
    FirestoreOptions Firestore,
    AssistantOptions Assistant)
{
    public AssistantProfile DefaultAssistantProfile => AssistantProfile.FromOptions(Assistant.DefaultProfile);

    public void Validate(VoiceCatalog voices, ToolRegistry tools)
    {
        Require(Relay.DeviceToken, "Relay:DeviceToken");
        if (Relay.UseTls)
        {
            Require(Relay.CertificatePath, "Relay:CertificatePath");
            Require(Relay.CertificatePassword, "Relay:CertificatePassword");
        }
        Require(AzureSpeech.Key, "AzureSpeech:Key");
        Require(AzureSpeech.Region, "AzureSpeech:Region");
        Require(AzureSpeech.DefaultServiceVoice, "AzureSpeech:DefaultServiceVoice");
        Require(AzureOpenAI.Endpoint, "AzureOpenAI:Endpoint");
        Require(AzureOpenAI.ApiKey, "AzureOpenAI:ApiKey");
        Require(AzureOpenAI.Model, "AzureOpenAI:Model");
        Require(Firestore.ConversationCollection, "Firestore:ConversationCollection");
        if (!Uri.TryCreate(AzureOpenAI.Endpoint, UriKind.Absolute, out _))
        {
            throw new InvalidOperationException("AzureOpenAI:Endpoint must be an absolute URL.");
        }
        AssistantProfile profile = DefaultAssistantProfile;
        Require(profile.Id, "Assistant:DefaultProfile:Id");
        Require(profile.RecognitionLanguage, "Assistant:DefaultProfile:RecognitionLanguage");
        if (!voices.TryGet(profile.DefaultVoiceId, out _))
        {
            throw new InvalidOperationException("Assistant:DefaultProfile:DefaultVoiceId is unknown.");
        }
        if (profile.Memory.WindowMinutes < 30)
        {
            throw new InvalidOperationException("Assistant:DefaultProfile:Memory:WindowMinutes must be at least 30.");
        }
        if (profile.Memory.MaximumHistoryTurns is < 1 or > 50)
        {
            throw new InvalidOperationException("Assistant:DefaultProfile:Memory:MaximumHistoryTurns must be between 1 and 50.");
        }
        if (profile.Memory.MaximumArchivedConversations is < 1 or > 10)
        {
            throw new InvalidOperationException(
                "Assistant:DefaultProfile:Memory:MaximumArchivedConversations must be between 1 and 10.");
        }
        if (Assistant.DefaultProfile.Tools.EnabledToolIds.Distinct(StringComparer.Ordinal).Count() !=
            Assistant.DefaultProfile.Tools.EnabledToolIds.Length)
        {
            throw new InvalidOperationException("Assistant:DefaultProfile:Tools contains duplicate tool IDs.");
        }
        foreach (string toolId in profile.Tools.EnabledToolIds)
        {
            if (!tools.Contains(toolId))
            {
                throw new InvalidOperationException(
                    $"Assistant:DefaultProfile:Tools enables unregistered tool '{toolId}'.");
            }
        }
    }

    private static void Require(string value, string name)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException($"Missing required setting {name}.");
        }
    }
}
