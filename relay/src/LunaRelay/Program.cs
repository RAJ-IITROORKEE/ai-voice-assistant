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
    builder.Configuration.GetSection("Assistant").Get<AssistantOptions>() ?? new AssistantOptions(),
    builder.Configuration.GetSection("InsForge").Get<InsForgeOptions>() ?? new InsForgeOptions(),
    builder.Configuration.GetSection("GeminiLive").Get<GeminiLiveOptions>() ?? new GeminiLiveOptions(),
    builder.Configuration.GetSection("Pipelines").Get<PipelinesOptions>() ?? new PipelinesOptions());
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
builder.Services.AddSingleton(configuration.InsForge);
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
// InsForge Postgres: device ownership, heartbeat, settings sync (Phase 3).
if (configuration.InsForge.SyncEnabled || configuration.InsForge.UseAgent)
{
    builder.Services.AddSingleton(sp =>
        InsForgeStore.Create(
            configuration.InsForge, sp.GetRequiredService<ILogger<InsForgeStore>>()));
}
// Route LLM turns through luna-agent when configured, else direct Azure OpenAI (Phase 1 fallback).
if (configuration.InsForge.UseAgent)
{
    builder.Services.AddHttpClient<IAgentResponder, AgentClient>();
}
else
{
    builder.Services.AddHttpClient<IAgentResponder, AzureOpenAiChatClient>();
}
builder.Services.AddSingleton<StreamingTts>();
// Phase 4: speech pipelines. Factory resolves per-turn; realtime pipeline is stateless.
builder.Services.AddSingleton<VoicePipelineFactory>();
builder.Services.AddSingleton<AzureRealtimeVoicePipeline>();
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

    // Resolve the device's owning InsForge user so agent turns persist under their RLS scope
    // and appear in their web app. Falls back to the credential subject when sync is off.
    string? ownerId = null;
    InsForgeStore? insforge = null;
    try
    {
        insforge = context.RequestServices.GetService<InsForgeStore>();
        if (insforge is not null && deviceId is not null)
        {
            string? defaultOwner = configuration.Assistant.DefaultDeviceOwnerId;
            ownerId = await insforge.ResolveOwnerAndTouchAsync(
                deviceId, firmware: null, defaultOwner, context.RequestAborted);
        }
    }
    catch (Exception exception) when (exception is not OperationCanceledException)
    {
        // Postgres hiccup must not break voice: fall back to the credential subject
        // (Firestore memory only) and skip web-app sync for this connection.
        voiceLogger.LogWarning(exception,
            "InsForge sync unavailable for device {DeviceId}; continuing without sync", deviceId);
        insforge = null;
    }

    IAssistantContextResolver contextResolver =
        context.RequestServices.GetRequiredService<IAssistantContextResolver>();
    AssistantContext assistantContext = await contextResolver.ResolveAsync(principal, context.RequestAborted);
    if (ownerId is not null)
    {
        assistantContext = assistantContext with { OwnerId = ownerId };
    }

    using System.Net.WebSockets.WebSocket socket = await context.WebSockets.AcceptWebSocketAsync();

    // Keep the device's dashboard presence fresh for the whole (persistent) session so the
    // web app shows Online while the device is powered and connected, not just mid-turn.
    using var heartbeatCts = CancellationTokenSource.CreateLinkedTokenSource(context.RequestAborted);
    Task? heartbeat = null;
    if (insforge is not null && deviceId is not null)
    {
        InsForgeStore heartbeatStore = insforge;
        string heartbeatDevice = deviceId;
        CancellationToken heartbeatToken = heartbeatCts.Token;
        heartbeat = Task.Run(async () =>
        {
            using var timer = new PeriodicTimer(TimeSpan.FromSeconds(30));
            try
            {
                while (await timer.WaitForNextTickAsync(heartbeatToken))
                {
                    await heartbeatStore.HeartbeatAsync(heartbeatDevice, heartbeatToken);
                }
            }
            catch (OperationCanceledException) { }
        });
    }

    try
    {
        await using VoiceSession session = ActivatorUtilities.CreateInstance<VoiceSession>(
            context.RequestServices, socket, assistantContext);
        await session.RunAsync(context.RequestAborted);
    }
    finally
    {
        heartbeatCts.Cancel();
        if (heartbeat is not null)
        {
            try { await heartbeat; } catch (OperationCanceledException) { }
        }
        if (insforge is not null && deviceId is not null)
        {
            await insforge.MarkOfflineAsync(deviceId, CancellationToken.None);
        }
    }
});

await app.RunAsync();
