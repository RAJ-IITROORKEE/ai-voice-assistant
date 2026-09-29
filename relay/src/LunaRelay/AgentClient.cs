using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace LunaRelay;

/// <summary>
/// Streams assistant text from the luna-agent service (OpenAI-compatible SSE endpoint).
/// The relay authenticates devices out-of-band (device token) and passes the resolved
/// owner's InsForge user id as `user_sub`; the agent treats that as trusted and persists
/// the turn to InsForge Postgres under the owner's RLS scope, so device turns appear in
/// the web app in realtime.
/// </summary>
public sealed class AgentClient(
    HttpClient httpClient,
    InsForgeOptions options,
    ILogger<AgentClient> logger) : IAgentResponder
{
    public async IAsyncEnumerable<string> StreamTextAsync(
        AssistantTurn turn,
        [EnumeratorCancellation] CancellationToken cancellationToken)
    {
        string transcript = turn.InputText;
        // The agent owns system prompt, history, persistence and title; the relay only
        // forwards the current user turn. Conversation thread is stable per owner so the
        // web app shows one continuous device conversation.
        var payload = new JsonObject
        {
            ["model"] = options.AgentModel,
            ["stream"] = true,
            ["user_sub"] = turn.Context.OwnerId,
            ["thread_id"] = ThreadId(turn.Context),
            ["source"] = "device",
            ["messages"] = new JsonArray
            {
                new JsonObject { ["role"] = "user", ["content"] = transcript },
            },
        };

        using var request = new HttpRequestMessage(
            HttpMethod.Post, options.AgentUrl.TrimEnd('/') + "/v1/chat/completions");
        request.Content = new StringContent(payload.ToJsonString(), Encoding.UTF8, "application/json");
        request.Headers.Accept.ParseAdd("text/event-stream");

        using HttpResponseMessage response = await httpClient.SendAsync(
            request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        if (!response.IsSuccessStatusCode)
        {
            string error = await response.Content.ReadAsStringAsync(cancellationToken);
            logger.LogError("luna-agent returned {StatusCode}: {Body}", (int)response.StatusCode, error);
            response.EnsureSuccessStatusCode();
        }

        await using Stream stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        using var reader = new StreamReader(stream);
        while (!reader.EndOfStream)
        {
            cancellationToken.ThrowIfCancellationRequested();
            string? line = await reader.ReadLineAsync(cancellationToken);
            if (string.IsNullOrEmpty(line) || !line.StartsWith("data: ", StringComparison.Ordinal))
            {
                continue;
            }
            string data = line["data: ".Length..];
            if (data == "[DONE]")
            {
                yield break;
            }
            string? delta = TryExtractDelta(data);
            if (!string.IsNullOrEmpty(delta))
            {
                yield return delta;
            }
        }
    }

    /// <summary>Stable conversation id per owner so device turns share one web-app thread.</summary>
    private static string ThreadId(AssistantContext context) =>
        context.Actor.DeviceId is { } deviceId
            ? $"device-{deviceId}"
            : $"owner-{context.OwnerId}";

    private static string? TryExtractDelta(string data)
    {
        try
        {
            JsonNode? node = JsonNode.Parse(data);
            return node?["choices"]?[0]?["delta"]?["content"]?.GetValue<string>();
        }
        catch (JsonException)
        {
            return null;
        }
    }
}
