using LunaRelay;

WebApplicationBuilder builder = WebApplication.CreateBuilder(args);
string relayRoot = Path.GetFullPath(Path.Combine(builder.Environment.ContentRootPath, "..", ".."));
builder.Configuration.AddJsonFile(
    Path.Combine(relayRoot, "appsettings.Local.json"), optional: true, reloadOnChange: false);
// Cloud Run secrets arrive as environment variables and must override local development state.
builder.Configuration.AddEnvironmentVariables();

var configuration = new RelayConfiguration(
    builder.Configuration.GetSection("Relay").Get<RelayOptions>() ?? new RelayOptions(),
    builder.Configuration.GetSection("AzureSpeech").Get<AzureSpeechOptions>() ?? new AzureSpeechOptions(),
    builder.Configuration.GetSection("AzureOpenAI").Get<AzureOpenAiOptions>() ?? new AzureOpenAiOptions(),
    builder.Configuration.GetSection("Firestore").Get<FirestoreOptions>() ?? new FirestoreOptions(),
    builder.Configuration.GetSection("Assistant").Get<AssistantOptions>() ?? new AssistantOptions());
VoiceCatalog voices = VoiceCatalog.Create(configuration.AzureSpeech.DefaultServiceVoice);
ToolRegistry tools = ToolRegistry.Empty;
configuration.Validate(voices, tools);

builder.WebHost.ConfigureKestrel(server =>
{
    if (configuration.Relay.UseTls)
    {
        string certificatePath = Path.GetFullPath(configuration.Relay.CertificatePath, relayRoot);
        server.ListenAnyIP(configuration.Relay.ListenPort, listen =>
            listen.UseHttps(certificatePath, configuration.Relay.CertificatePassword));
        return;
    }

    // Cloud Run terminates the public TLS connection before this port.
    server.ListenAnyIP(configuration.Relay.ListenPort);
});
builder.Services.AddSingleton(configuration);
builder.Services.AddSingleton(configuration.AzureSpeech);
builder.Services.AddSingleton(configuration.AzureOpenAI);
builder.Services.AddSingleton(configuration.Firestore);
builder.Services.AddSingleton(configuration.DefaultAssistantProfile);
builder.Services.AddSingleton(voices);
builder.Services.AddSingleton(tools);
builder.Services.AddSingleton<IDeviceAuthenticator>(
    new StaticDeviceTokenAuthenticator(configuration.Relay.DeviceToken));
builder.Services.AddSingleton<IAssistantContextResolver, StaticAssistantContextResolver>();
builder.Services.AddSingleton<IToolApprovalValidator, DenyAllToolApprovalValidator>();
builder.Services.AddSingleton<IToolExecutor, ToolExecutor>();
builder.Services.AddSingleton<IConversationStore>(
    await FirestoreConversationStore.CreateAsync(configuration.Firestore));
builder.Services.AddHttpClient<IAgentResponder, AzureOpenAiChatClient>();
builder.Services.AddSingleton<StreamingTts>();
builder.Services.AddTransient<VoiceSession>();

WebApplication app = builder.Build();
ILogger voiceLogger = app.Services.GetRequiredService<ILoggerFactory>().CreateLogger("VoiceEndpoint");
app.UseWebSockets(new WebSocketOptions { KeepAliveInterval = TimeSpan.FromSeconds(20) });
app.MapGet("/health", () => Results.Ok(new { status = "ready" }));
app.Map("/voice", async context =>
{
    string? token = context.Request.Headers["X-Device-Token"].FirstOrDefault();
    IDeviceAuthenticator authenticator = context.RequestServices.GetRequiredService<IDeviceAuthenticator>();
    AccessPrincipal? principal = authenticator.Authenticate(token);
    if (principal is null)
    {
        context.Response.StatusCode = StatusCodes.Status401Unauthorized;
        return;
    }
    if (!context.WebSockets.IsWebSocketRequest)
    {
        voiceLogger.LogWarning(
            "Rejected authenticated non-WebSocket request. Connection={HasConnection}, Upgrade={HasUpgrade}, Version={HasVersion}, Key={HasKey}",
            context.Request.Headers.ContainsKey("Connection"),
            context.Request.Headers.ContainsKey("Upgrade"),
            context.Request.Headers.ContainsKey("Sec-WebSocket-Version"),
            context.Request.Headers.ContainsKey("Sec-WebSocket-Key"));
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        return;
    }
    string? suppliedDeviceId = context.Request.Headers["X-Device-Id"].FirstOrDefault();
    string? deviceId = null;
    if (suppliedDeviceId is not null && !DeviceIdentity.TryNormalize(suppliedDeviceId, out deviceId))
    {
        voiceLogger.LogWarning("Rejected invalid device identity. Present={Present}, Length={Length}",
            suppliedDeviceId is not null, suppliedDeviceId?.Length ?? 0);
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        return;
    }

    if (suppliedDeviceId is not null)
    {
        principal = principal with { DeviceId = deviceId };
    }
    IAssistantContextResolver contextResolver =
        context.RequestServices.GetRequiredService<IAssistantContextResolver>();
    AssistantContext assistantContext = await contextResolver.ResolveAsync(principal, context.RequestAborted);

    using System.Net.WebSockets.WebSocket socket = await context.WebSockets.AcceptWebSocketAsync();
    await using VoiceSession session = ActivatorUtilities.CreateInstance<VoiceSession>(
        context.RequestServices, socket, assistantContext);
    await session.RunAsync(context.RequestAborted);
});

await app.RunAsync();
