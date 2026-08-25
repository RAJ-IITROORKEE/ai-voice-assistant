using System.Text;
using System.Text.Json;

namespace LunaRelay;

public enum ResponsesSseEventKind
{
    None,
    TextDelta,
    Completed,
    Failed,
}

public readonly record struct ResponsesSseEvent(ResponsesSseEventKind Kind, string? Text = null);

// SSE event names are the reliable terminal signal; terminal payloads can be large.
public sealed class ResponsesSseParser
{
    private readonly StringBuilder _data = new();
    private string? _eventName;

    public bool SawTextDelta { get; private set; }

    public ResponsesSseEvent ProcessLine(string line)
    {
        ArgumentNullException.ThrowIfNull(line);

        if (line.Length == 0)
        {
            return Dispatch();
        }

        if (line[0] == ':')
        {
            return new ResponsesSseEvent(ResponsesSseEventKind.None);
        }

        int separator = line.IndexOf(':');
        if (separator <= 0)
        {
            return new ResponsesSseEvent(ResponsesSseEventKind.None);
        }

        string field = line[..separator];
        ReadOnlySpan<char> value = line.AsSpan(separator + 1);
        if (!value.IsEmpty && value[0] == ' ')
        {
            value = value[1..];
        }

        if (field == "event")
        {
            _eventName = value.ToString();
        }
        else if (field == "data" && !IsTerminalEvent(_eventName))
        {
            if (_data.Length != 0)
            {
                _data.Append('\n');
            }
            _data.Append(value);
        }

        return new ResponsesSseEvent(ResponsesSseEventKind.None);
    }

    public ResponsesSseEvent Finish() => Dispatch();

    private ResponsesSseEvent Dispatch()
    {
        string? eventName = _eventName;
        string data = _data.ToString();
        _eventName = null;
        _data.Clear();

        if (eventName == "response.completed" || data == "[DONE]")
        {
            return new ResponsesSseEvent(ResponsesSseEventKind.Completed);
        }

        if (eventName is "response.failed" or "error")
        {
            return new ResponsesSseEvent(ResponsesSseEventKind.Failed);
        }

        if (data.Length == 0)
        {
            return new ResponsesSseEvent(ResponsesSseEventKind.None);
        }

        try
        {
            using JsonDocument json = JsonDocument.Parse(data);
            JsonElement root = json.RootElement;
            string? type = root.TryGetProperty("type", out JsonElement typeValue)
                ? typeValue.GetString()
                : eventName;

            if (type == "response.output_text.delta" &&
                root.TryGetProperty("delta", out JsonElement deltaValue) &&
                !string.IsNullOrEmpty(deltaValue.GetString()))
            {
                SawTextDelta = true;
                return new ResponsesSseEvent(ResponsesSseEventKind.TextDelta, deltaValue.GetString());
            }

            if (type == "response.completed")
            {
                return new ResponsesSseEvent(ResponsesSseEventKind.Completed);
            }

            if (type is "response.failed" or "error")
            {
                return new ResponsesSseEvent(ResponsesSseEventKind.Failed);
            }
        }
        catch (JsonException)
        {
            // Unknown service keep-alives and metadata do not affect the stream.
        }

        return new ResponsesSseEvent(ResponsesSseEventKind.None);
    }

    private static bool IsTerminalEvent(string? eventName) =>
        eventName is "response.completed" or "response.failed" or "error";
}
