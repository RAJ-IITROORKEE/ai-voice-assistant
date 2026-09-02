using System.Text.RegularExpressions;

namespace LunaRelay;

public sealed record VoiceProfile(string Name, string ServiceName, string Gender);

public sealed class VoiceCatalog
{
    private readonly Dictionary<string, VoiceProfile> _byName;

    private VoiceCatalog(IReadOnlyList<VoiceProfile> voices)
    {
        Voices = voices;
        Default = voices[0];
        _byName = voices.ToDictionary(voice => voice.Name, StringComparer.OrdinalIgnoreCase);
    }

    public IReadOnlyList<VoiceProfile> Voices { get; }
    public VoiceProfile Default { get; }

    public static VoiceCatalog Create(string defaultServiceName) => new(
    [
        new("Default", defaultServiceName, "female"),
        new("Ava", "en-US-AvaMultilingualNeural", "female"),
        new("Andrew", "en-US-AndrewMultilingualNeural", "male"),
        new("Brian", "en-US-BrianMultilingualNeural", "male"),
    ]);

    public bool TryGet(string? name, out VoiceProfile voice) =>
        _byName.TryGetValue(name?.Trim() ?? string.Empty, out voice!);

    public VoiceProfile Resolve(string? name) => TryGet(name, out VoiceProfile voice) ? voice : Default;
}

public enum VoiceCommandKind
{
    Change,
    List,
    Unknown,
}

public sealed record VoiceCommand(VoiceCommandKind Kind, VoiceProfile? Voice, string Response);

public static partial class VoiceCommands
{
    [GeneratedRegex(
        "^(?:(?:luna)[,\\s]+)?(?:please\\s+)?(?:(?:(?:change|switch|set)(?:\\s+(?:the|your))?\\s+voice\\s+to\\s+(?<name>[a-z]+))|(?:(?:change|switch|set)\\s+(?:it\\s+)?(?:back\\s+)?to\\s+(?<name>[a-z]+))|(?:set\\s+my\\s+voice\\s+to\\s+(?<name>[a-z]+))|(?:go\\s+back\\s+to|return\\s+to)\\s+(?<name>default)|use\\s+(?:the\\s+)?(?<useName>[a-z]+)(?:'s)?\\s+voice)(?:\\s+please)?\\s*[,!.?]*$",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex ChangePattern();

    [GeneratedRegex(
        "^(?:(?:(?:what|which)(?:\\s+are)?\\s+(?:your\\s+)?voices?(?:\\s+(?:can|could)\\s+i\\s+(?:choose|use))?)|(?:list|show)(?:\\s+(?:your|the|available))?\\s+voices?)[?!.]*$",
        RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex ListPattern();

    public static bool TryParse(string transcript, VoiceCatalog catalog, out VoiceCommand command)
    {
        string input = transcript.Trim();
        if (ListPattern().IsMatch(input))
        {
            command = new VoiceCommand(VoiceCommandKind.List, null, Describe(catalog));
            return true;
        }

        Match change = ChangePattern().Match(input);
        if (!change.Success)
        {
            command = null!;
            return false;
        }

        string requestedName = change.Groups["useName"].Success
            ? change.Groups["useName"].Value
            : change.Groups["name"].Value;
        if (change.Groups["useName"].Success && !catalog.TryGet(requestedName, out _))
        {
            command = null!;
            return false;
        }
        if (!catalog.TryGet(requestedName, out VoiceProfile voice))
        {
            command = new VoiceCommand(
                VoiceCommandKind.Unknown,
                null,
                $"That voice is not available. {Describe(catalog)}");
            return true;
        }

        command = new VoiceCommand(
            VoiceCommandKind.Change,
            voice,
            $"Voice changed to {voice.Name}.");
        return true;
    }

    private static string Describe(VoiceCatalog catalog) =>
        "Available voices are Default, Ava, Andrew, and Brian. " +
        "Default and Ava are female. Andrew and Brian are male.";
}
