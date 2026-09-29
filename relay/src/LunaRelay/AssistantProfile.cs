namespace LunaRelay;

public sealed class AssistantOptions
{
    public AssistantProfileOptions DefaultProfile { get; init; } = new();
    /// <summary>
    /// InsForge user id (uuid) that owns unclaimed devices. Single-user deployment: set this to
    /// the admin user and any new device is auto-claimed on first connect.
    /// </summary>
    public string? DefaultDeviceOwnerId { get; init; }
}

public sealed class AssistantProfileOptions
{
    public string Id { get; init; } = "default";
    public string RecognitionLanguage { get; init; } = "en-IN";
    public string DefaultVoiceId { get; init; } = "Default";
    public MemoryPolicyOptions Memory { get; init; } = new();
    public ToolPolicyOptions Tools { get; init; } = new();
}

public sealed class MemoryPolicyOptions
{
    public int WindowMinutes { get; init; } = 7 * 24 * 60;
    public int MaximumHistoryTurns { get; init; } = 20;
    public int MaximumArchivedConversations { get; init; } = 5;
}

public sealed class ToolPolicyOptions
{
    public string[] EnabledToolIds { get; init; } = [];
    public bool RequireConfirmationForWrites { get; init; } = true;
}

public sealed record MemoryPolicy(
    int WindowMinutes,
    int MaximumHistoryTurns,
    int MaximumArchivedConversations);

public sealed record ToolPolicy(
    IReadOnlySet<string> EnabledToolIds,
    bool RequireConfirmationForWrites);

public sealed record AssistantProfile(
    string Id,
    string RecognitionLanguage,
    string DefaultVoiceId,
    MemoryPolicy Memory,
    ToolPolicy Tools)
{
    public static AssistantProfile FromOptions(AssistantProfileOptions options) => new(
        options.Id,
        options.RecognitionLanguage,
        options.DefaultVoiceId,
        new MemoryPolicy(
            options.Memory.WindowMinutes,
            options.Memory.MaximumHistoryTurns,
            options.Memory.MaximumArchivedConversations),
        new ToolPolicy(
            new HashSet<string>(options.Tools.EnabledToolIds, StringComparer.Ordinal),
            options.Tools.RequireConfirmationForWrites));
}
