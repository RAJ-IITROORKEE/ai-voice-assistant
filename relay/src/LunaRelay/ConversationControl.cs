using System.Text.RegularExpressions;

namespace LunaRelay;

public static partial class ConversationCommands
{
    [GeneratedRegex(
        @"^\s*(?:please\s+)?(?:start|begin|open)\s+(?:a\s+)?new\s+(?:conversation|chat)\b[\s,.:;\-]*(?:and\s+)?(.*)$",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex ImperativeStartPattern();

    [GeneratedRegex(
        @"^\s*new\s+(?:conversation|chat)(?:\s*$|\s*[:;\-]\s*(.*)$)",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex ShortStartPattern();

    public static bool TryStartNew(string transcript, out string firstRequest)
    {
        Match match = ImperativeStartPattern().Match(transcript);
        if (!match.Success)
        {
            match = ShortStartPattern().Match(transcript);
        }
        firstRequest = match.Success ? match.Groups[1].Value.Trim() : string.Empty;
        return match.Success;
    }

    public static bool RequestsPreviousConversation(string transcript)
    {
        string normalized = transcript.ToLowerInvariant();
        return normalized.Contains("previous conversation", StringComparison.Ordinal) ||
            normalized.Contains("last conversation", StringComparison.Ordinal) ||
            normalized.Contains("previous chat", StringComparison.Ordinal) ||
            normalized.Contains("last chat", StringComparison.Ordinal) ||
            normalized.Contains("earlier conversation", StringComparison.Ordinal);
    }

    public static bool TryAnswerFirstQuestion(
        ConversationState state,
        string transcript,
        out string answer)
    {
        string normalized = transcript.ToLowerInvariant();
        bool asksForFirst = normalized.Contains("first question", StringComparison.Ordinal) ||
            normalized.Contains("first thing i asked", StringComparison.Ordinal) ||
            normalized.Contains("first thing that i asked", StringComparison.Ordinal);
        bool referencesConversation = normalized.Contains("i asked", StringComparison.Ordinal) ||
            normalized.Contains("did i ask", StringComparison.Ordinal) ||
            normalized.Contains("our conversation", StringComparison.Ordinal) ||
            normalized.Contains("our chat", StringComparison.Ordinal) ||
            normalized.Contains("this conversation", StringComparison.Ordinal) ||
            normalized.Contains("this chat", StringComparison.Ordinal) ||
            normalized.Contains("conversation we", StringComparison.Ordinal) ||
            normalized.Contains("chat we", StringComparison.Ordinal) ||
            normalized.Contains("we discussed", StringComparison.Ordinal) ||
            normalized.Contains("you remember", StringComparison.Ordinal) ||
            RequestsPreviousConversation(transcript);
        if (!asksForFirst || !referencesConversation)
        {
            answer = string.Empty;
            return false;
        }

        IReadOnlyList<ConversationExchange> history = RequestsPreviousConversation(transcript)
            ? state.Archives.LastOrDefault()?.History ?? []
            : state.ActiveHistory;
        answer = history.Count == 0
            ? "There is no first question available in that conversation."
            : $"The first question you asked was: {history[0].UserText}";
        return true;
    }
}

public static class AgentGuardrails
{
    private const string SafeResponse =
        "I can't provide private instructions or internal implementation details, but I can explain my user-facing capabilities.";

    private static readonly string[] DirectSensitiveTerms =
    [
        "system prompt", "developer message", "hidden instruction", "chain of thought",
        "api key", "credentials", "secret token", "instructions above", "reveal instructions",
        "repeat every instruction", "hidden prompt", "how are you built",
    ];

    private static readonly string[] InternalTerms =
    [
        "backend", "architecture", "source code", "database", "model", "tools",
        "cloud provider", "infrastructure", "internal", "under the hood", "powers you",
    ];

    private static readonly string[] SensitiveResponseTerms =
    [
        "vertex", "firestore", "azure", "cloud run", "google cloud", "gemini",
        "system prompt", "developer message", "hidden instruction", "api key",
        "secret token", "chain of thought",
    ];

    private static readonly string[] SelfReferenceTerms =
    [
        "i use", "i rely", "my backend", "my architecture", "my system", "my database",
        "my model", "my tools", "my provider", "powered by", "built on", "running on",
        "hosted by",
    ];

    public static string? GetSafeResponse(string request)
    {
        string normalized = request.ToLowerInvariant();
        bool directDisclosure = DirectSensitiveTerms.Any(normalized.Contains);
        bool asksAboutAssistant = normalized.Contains("your", StringComparison.Ordinal) ||
            normalized.Contains("you use", StringComparison.Ordinal) ||
            normalized.Contains("luna", StringComparison.Ordinal) ||
            normalized.Contains("powers you", StringComparison.Ordinal) ||
            normalized.Contains("under the hood", StringComparison.Ordinal) ||
            normalized.Contains("how do you work", StringComparison.Ordinal);
        return directDisclosure || asksAboutAssistant && InternalTerms.Any(normalized.Contains)
            ? SafeResponse
            : null;
    }

    public static string FilterResponse(string request, string response)
    {
        bool selfReference = SelfReferenceTerms.Any(
            term => response.Contains(term, StringComparison.OrdinalIgnoreCase));
        bool namesPrivateImplementation = SensitiveResponseTerms.Any(
            term => response.Contains(term, StringComparison.OrdinalIgnoreCase));
        bool describesInternalCategory = InternalTerms.Any(
            term => response.Contains(term, StringComparison.OrdinalIgnoreCase));
        bool selfDisclosure = selfReference && (namesPrivateImplementation || describesInternalCategory);
        bool extractionRequest = GetSafeResponse(request) is not null;
        return selfDisclosure || extractionRequest && namesPrivateImplementation
            ? SafeResponse
            : response;
    }
}
