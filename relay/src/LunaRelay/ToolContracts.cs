using System.Text.Json;

namespace LunaRelay;

public enum ToolEffect
{
    ReadOnly,
    ExternalWrite,
}

public sealed record ToolDefinition(
    string Id,
    string DisplayName,
    string UserDescription,
    string InputSchemaJson,
    ToolEffect Effect);
public sealed record ToolApproval(string OpaqueGrant);
public sealed record ToolCall(string ToolId, JsonElement Arguments, ToolApproval? Approval);

public enum ToolExecutionStatus
{
    Succeeded,
    NotAvailable,
    ConfirmationRequired,
    Failed,
}

public sealed record ToolResult(ToolExecutionStatus Status, string Message)
{
    public static ToolResult Succeeded(string message) => new(ToolExecutionStatus.Succeeded, message);
    public static ToolResult NotAvailable(string message = "This capability is not available.") =>
        new(ToolExecutionStatus.NotAvailable, message);
    public static ToolResult ConfirmationRequired(string message = "Confirmation is required.") =>
        new(ToolExecutionStatus.ConfirmationRequired, message);
}

public interface IAssistantTool
{
    ToolDefinition Definition { get; }
    Task<ToolResult> ExecuteAsync(
        AssistantContext context, ToolCall call, CancellationToken cancellationToken);
}

public sealed class ToolRegistry
{
    private readonly IReadOnlyDictionary<string, IAssistantTool> _tools;

    public ToolRegistry(IEnumerable<IAssistantTool> tools)
    {
        _tools = tools.ToDictionary(tool => tool.Definition.Id, StringComparer.Ordinal);
    }

    public static ToolRegistry Empty { get; } = new([]);
    public bool TryGet(string id, out IAssistantTool tool) => _tools.TryGetValue(id, out tool!);
    public bool Contains(string id) => _tools.ContainsKey(id);
}

public interface IToolApprovalValidator
{
    ValueTask<bool> IsApprovedAsync(AssistantContext context, ToolCall call, CancellationToken cancellationToken);
}

public sealed class DenyAllToolApprovalValidator : IToolApprovalValidator
{
    public ValueTask<bool> IsApprovedAsync(
        AssistantContext context, ToolCall call, CancellationToken cancellationToken) => ValueTask.FromResult(false);
}

public interface IToolExecutor
{
    Task<ToolResult> ExecuteAsync(
        AssistantContext context, ToolCall call, CancellationToken cancellationToken);
}

public sealed class ToolExecutor(ToolRegistry registry, IToolApprovalValidator approvals) : IToolExecutor
{
    public async Task<ToolResult> ExecuteAsync(
        AssistantContext context, ToolCall call, CancellationToken cancellationToken)
    {
        if (!registry.TryGet(call.ToolId, out IAssistantTool tool) ||
            !context.Profile.Tools.EnabledToolIds.Contains(call.ToolId))
        {
            return ToolResult.NotAvailable();
        }

        if (tool.Definition.Effect == ToolEffect.ExternalWrite &&
            !await approvals.IsApprovedAsync(context, call, cancellationToken))
        {
            return ToolResult.ConfirmationRequired();
        }

        return await tool.ExecuteAsync(context, call, cancellationToken);
    }
}
