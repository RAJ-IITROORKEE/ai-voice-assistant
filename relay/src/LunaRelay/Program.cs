using LunaRelay;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);
string relayRoot = Path.GetFullPath(Path.Combine(builder.Environment.ContentRootPath, "..", ".."));
builder.Configuration.AddJsonFile(
    Path.Combine(relayRoot, "appsettings.Local.json"), optional: true, reloadOnChange: false);
// Container App secrets arrive as environment variables and must override local development state.
builder.Configuration.AddEnvironmentVariables();

var configuration = new RelayConfiguration(
    builder.Configuration.GetSection("Relay").Get<RelayOptions>() ?? new RelayOptions(),
    builder.Configuration.GetSection("AzureSpeech").Get<AzureSpeechOptions>() ?? new AzureSpeechOptions(),
    builder.Configuration.GetSection("AzureOpenAI").Get<AzureOpenAiOptions>() ?? new AzureOpenAiOptions());
configuration.Validate();

builder.WebHost.ConfigureKestrel(server =>
{
    if (configuration.Relay.UseTls)
    {
        string certificatePath = Path.GetFullPath(configuration.Relay.CertificatePath, relayRoot);
        server.ListenAnyIP(configuration.Relay.ListenPort, listen =>
            listen.UseHttps(certificatePath, configuration.Relay.CertificatePassword));
        return;
    }

    // Azure Container Apps terminates the public TLS connection before this port.
    server.ListenAnyIP(configuration.Relay.ListenPort);
});
builder.Services.AddSingleton(configuration);
builder.Services.AddSingleton(configuration.AzureSpeech);
builder.Services.AddSingleton(configuration.AzureOpenAI);
builder.Services.AddHttpClient<AzureResponsesClient>();
builder.Services.AddSingleton<StreamingTts>();
builder.Services.AddTransient<VoiceSession>();

WebApplication app = builder.Build();
app.UseWebSockets(new WebSocketOptions { KeepAliveInterval = TimeSpan.FromSeconds(20) });
app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
app.Map("/voice", async context =>
{
    string? token = context.Request.Headers["X-Device-Token"].FirstOrDefault();
    if (!DeviceAuthenticator.IsAuthorized(token, configuration.Relay.DeviceToken))
    {
        context.Response.StatusCode = StatusCodes.Status401Unauthorized;
        return;
    }
    if (!context.WebSockets.IsWebSocketRequest)
    {
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        return;
    }

    using System.Net.WebSockets.WebSocket socket = await context.WebSockets.AcceptWebSocketAsync();
    await using VoiceSession session = ActivatorUtilities.CreateInstance<VoiceSession>(context.RequestServices, socket);
    await session.RunAsync(context.RequestAborted);
});

await app.RunAsync();
