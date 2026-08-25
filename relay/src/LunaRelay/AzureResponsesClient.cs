using System.Net.Http.Headers;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;

namespace LunaRelay;

public sealed class AzureResponsesClient(
    HttpClient httpClient,
    AzureOpenAiOptions options,
    ILogger<AzureResponsesClient> logger)
{
    public async IAsyncEnumerable<string> StreamTextAsync(
        string transcript,
        [EnumeratorCancellation] CancellationToken cancellationToken)
    {
        var payload = new
        {
            model = options.Model,
            stream = true,
            store = false,
            max_output_tokens = 280,
            instructions = "You are Luna, a concise helpful voice assistant. Start answering immediately in plain spoken language. Do not use Markdown.",
            input = transcript,
        };

        using var request = new HttpRequestMessage(HttpMethod.Post, options.Endpoint);
        request.Headers.Add("api-key", options.ApiKey);
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("text/event-stream"));
        request.Content = new StringContent(
            JsonSerializer.Serialize(payload), Encoding.UTF8, "application/json");

        using HttpResponseMessage response = await httpClient.SendAsync(
            request, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        response.EnsureSuccessStatusCode();

        await using Stream stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        using var reader = new StreamReader(stream);
        var parser = new ResponsesSseParser();
        bool completed = false;
        bool receivedText = false;

        while (!reader.EndOfStream && !cancellationToken.IsCancellationRequested)
        {
            string? line = await reader.ReadLineAsync(cancellationToken);
            if (line is null)
            {
                break;
            }

            ResponsesSseEvent responseEvent = parser.ProcessLine(line);
            switch (responseEvent.Kind)
            {
                case ResponsesSseEventKind.TextDelta:
                    receivedText = true;
                    yield return responseEvent.Text!;
                    break;
                case ResponsesSseEventKind.Completed:
                    completed = true;
                    break;
                case ResponsesSseEventKind.Failed:
                    throw new InvalidOperationException("Azure Responses stream failed.");
            }

            if (completed)
            {
                break;
            }
        }

        if (!completed)
        {
            cancellationToken.ThrowIfCancellationRequested();
            ResponsesSseEvent finalEvent = parser.Finish();
            if (finalEvent.Kind == ResponsesSseEventKind.Completed)
            {
                completed = true;
            }
            else if (finalEvent.Kind == ResponsesSseEventKind.Failed)
            {
                throw new InvalidOperationException("Azure Responses stream failed.");
            }
        }

        // The service has occasionally ended a valid 200 SSE stream without its
        // optional terminal event. Text already delivered to TTS is still valid.
        if (!completed && (receivedText || parser.SawTextDelta))
        {
            logger.LogWarning("Responses stream ended without a completion event after text was received");
            completed = true;
        }

        if (!completed)
        {
            logger.LogWarning("Responses stream ended without a completion event");
            throw new InvalidOperationException("Azure Responses stream ended early.");
        }
    }
}
