using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace LunaRelay;

public enum AssistantActorKind
{
    Device,
    User,
}

public sealed record AccessPrincipal(AssistantActorKind Kind, string SubjectId, string? DeviceId);
public sealed record ConversationScope(string StorageKey);
public sealed record AssistantContext(
    AccessPrincipal Actor,
    string OwnerId,
    string ProfileId,
    ConversationScope Conversation,
    AssistantProfile Profile);
public sealed record AssistantTurn(AssistantContext Context, string InputText);

public interface IAssistantContextResolver
{
    ValueTask<AssistantContext> ResolveAsync(AccessPrincipal actor, CancellationToken cancellationToken);
}

public sealed class StaticAssistantContextResolver(AssistantProfile profile) : IAssistantContextResolver
{
    public ValueTask<AssistantContext> ResolveAsync(
        AccessPrincipal actor, CancellationToken cancellationToken) =>
        ValueTask.FromResult(new AssistantContext(
            actor, actor.SubjectId, profile.Id, new ConversationScope(actor.SubjectId), profile));
}

public static partial class DeviceIdentity
{
    [GeneratedRegex("^esp32-[0-9a-f]{12}$", RegexOptions.CultureInvariant)]
    private static partial Regex ValidIdentifier();

    public static bool TryNormalize(string? supplied, out string normalized)
    {
        normalized = supplied?.Trim().ToLowerInvariant() ?? string.Empty;
        return ValidIdentifier().IsMatch(normalized);
    }
}

public static class CredentialIdentity
{
    public static string FromToken(string token)
    {
        byte[] hash = SHA256.HashData(Encoding.UTF8.GetBytes(token));
        return $"credential-{Convert.ToHexString(hash.AsSpan(0, 12)).ToLowerInvariant()}";
    }
}
