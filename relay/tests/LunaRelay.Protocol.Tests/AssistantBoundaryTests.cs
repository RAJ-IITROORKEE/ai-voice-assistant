using System.Net;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Logging.Abstractions;
using Xunit;

namespace LunaRelay.Protocol.Tests;

public sealed class AssistantBoundaryTests
{
    [Fact]
    public async Task StaticContextKeepsExistingCredentialScopedConversationKey()
    {
        string credential = CredentialIdentity.FromToken("one-device-secret");
        var resolver = new StaticAssistantContextResolver(DefaultProfile());

        AssistantContext first = await resolver.ResolveAsync(
            new AccessPrincipal(AssistantActorKind.Device, credential, "esp32-a1b2c3d4e5f6"),
            CancellationToken.None);
        AssistantContext second = await resolver.ResolveAsync(
            new AccessPrincipal(AssistantActorKind.Device, credential, "esp32-001122334455"),
            CancellationToken.None);

        Assert.Equal(credential, first.OwnerId);
        Assert.Equal("default", first.ProfileId);
        Assert.Equal(credential, first.Conversation.StorageKey);
        Assert.Equal(first.Conversation, second.Conversation);
    }

    [Fact]
    public void StaticTokenAuthenticationNeverTrustsDeviceMetadataAsOwnership()
    {
        var authenticator = new StaticDeviceTokenAuthenticator("one-device-secret");

        AccessPrincipal? principal = authenticator.Authenticate("one-device-secret");

        Assert.NotNull(principal);
        Assert.Equal(AssistantActorKind.Device, principal.Kind);
        Assert.Equal(CredentialIdentity.FromToken("one-device-secret"), principal.SubjectId);
        Assert.Null(principal.DeviceId);
        Assert.Null(authenticator.Authenticate("different-device-secret"));
    }

    [Fact]
    public void DefaultProfilePreservesCurrentLanguageVoiceAndMemoryBehavior()
    {
        AssistantProfile profile = AssistantProfile.FromOptions(new AssistantProfileOptions());

        Assert.Equal("default", profile.Id);
        Assert.Equal("en-IN", profile.RecognitionLanguage);
        Assert.Equal("Default", profile.DefaultVoiceId);
        Assert.Equal(7 * 24 * 60, profile.Memory.WindowMinutes);
        Assert.Equal(20, profile.Memory.MaximumHistoryTurns);
        Assert.Equal(5, profile.Memory.MaximumArchivedConversations);
        Assert.Empty(profile.Tools.EnabledToolIds);
        Assert.True(profile.Tools.RequireConfirmationForWrites);
    }

    [Theory]
    [InlineData("Missing")]
    [InlineData("")]
    public void ConfigurationRejectsUnknownDefaultVoice(string voiceId)
    {
        RelayConfiguration configuration = CreateConfiguration(new AssistantOptions
        {
            DefaultProfile = new AssistantProfileOptions { DefaultVoiceId = voiceId },
        });

        Assert.Throws<InvalidOperationException>(() => configuration.Validate(
            VoiceCatalog.Create("en-IN-NeerjaNeural"), ToolRegistry.Empty));
    }

    [Fact]
    public void ConfigurationRejectsEnabledToolsThatAreNotRegistered()
    {
        RelayConfiguration configuration = CreateConfiguration(new AssistantOptions
        {
            DefaultProfile = new AssistantProfileOptions
            {
                Tools = new ToolPolicyOptions { EnabledToolIds = ["calendar.read"] },
            },
        });

        Assert.Throws<InvalidOperationException>(() => configuration.Validate(
            VoiceCatalog.Create("en-IN-NeerjaNeural"), ToolRegistry.Empty));
    }

    [Fact]
    public async Task EmptyToolRegistryDeniesEveryToolCall()
    {
        var executor = new ToolExecutor(ToolRegistry.Empty, new DenyAllToolApprovalValidator());

        ToolResult result = await executor.ExecuteAsync(
            ContextWithTools(), ToolCallFor("calendar.read"), CancellationToken.None);

        Assert.Equal(ToolExecutionStatus.NotAvailable, result.Status);
    }

    [Fact]
    public async Task WriteToolRequiresServerApprovedGrant()
    {
        var tool = new RecordingWriteTool();
        var executor = new ToolExecutor(new ToolRegistry([tool]), new DenyAllToolApprovalValidator());
        AssistantContext context = ContextWithTools("calendar.create");

        ToolResult result = await executor.ExecuteAsync(
            context, ToolCallFor("calendar.create"), CancellationToken.None);

        Assert.Equal(ToolExecutionStatus.ConfirmationRequired, result.Status);
        Assert.False(tool.WasExecuted);
    }

    [Fact]
    public async Task ChatCompletionRequestDoesNotExposeToolsUntilOrchestrationExists()
    {
        var handler = new CaptureHandler();
        using var client = new HttpClient(handler);
        var responder = new AzureOpenAiChatClient(
            client,
            new AzureOpenAiOptions
            {
                Endpoint = "https://example.test/openai/v1/chat/completions",
                ApiKey = "agent-key",
                Model = "test-model",
            },
            new EmptyConversationStore(),
            NullLogger<AzureOpenAiChatClient>.Instance);

        var chunks = new List<string>();
        await foreach (string chunk in responder.StreamTextAsync(
                           new AssistantTurn(ContextWithTools("calendar.read"), "Hello"),
                           CancellationToken.None))
        {
            chunks.Add(chunk);
        }

        Assert.Equal(["Hello."], chunks);
        using JsonDocument request = JsonDocument.Parse(handler.RequestBody);
        Assert.False(request.RootElement.TryGetProperty("tools", out _));
    }

    private static RelayConfiguration CreateConfiguration(AssistantOptions assistant) => new(
        new RelayOptions { ListenPort = 8080, UseTls = false, DeviceToken = "device-token" },
        new AzureSpeechOptions
        {
            Key = "speech-key",
            Region = "eastus",
            DefaultServiceVoice = "en-IN-NeerjaNeural",
        },
        new AzureOpenAiOptions
        {
            Endpoint = "https://example.test/openai/v1/chat/completions",
            ApiKey = "agent-key",
            Model = "test-model",
        },
        new FirestoreOptions { ConversationCollection = "luna_agent_sessions" },
        assistant);

    private static AssistantProfile DefaultProfile(string[]? enabledToolIds = null) => new(
        "default",
        "en-IN",
        "Default",
        new MemoryPolicy(7 * 24 * 60, 20, 5),
        new ToolPolicy(new HashSet<string>(enabledToolIds ?? [], StringComparer.Ordinal), true));

    private static AssistantContext ContextWithTools(params string[] enabledToolIds)
    {
        AssistantProfile profile = DefaultProfile(enabledToolIds);
        var actor = new AccessPrincipal(AssistantActorKind.Device, "credential-0123456789abcdef01234567", null);
        return new AssistantContext(actor, actor.SubjectId, profile.Id,
            new ConversationScope(actor.SubjectId), profile);
    }

    private static ToolCall ToolCallFor(string toolId)
    {
        using JsonDocument arguments = JsonDocument.Parse("{}");
        return new ToolCall(toolId, arguments.RootElement.Clone(), null);
    }

    private sealed class RecordingWriteTool : IAssistantTool
    {
        public ToolDefinition Definition { get; } = new(
            "calendar.create", "Create calendar event", "Creates a calendar event.", "{}",
            ToolEffect.ExternalWrite);

        public bool WasExecuted { get; private set; }

        public Task<ToolResult> ExecuteAsync(
            AssistantContext context,
            ToolCall call,
            CancellationToken cancellationToken)
        {
            WasExecuted = true;
            return Task.FromResult(ToolResult.Succeeded("created"));
        }
    }

    private sealed class EmptyConversationStore : IConversationStore
    {
        public Task<ConversationState> LoadAsync(
            ConversationScope scope,
            MemoryPolicy policy,
            CancellationToken cancellationToken) => Task.FromResult(ConversationState.Empty);

        public Task<bool> AppendAsync(
            ConversationScope scope,
            MemoryPolicy policy,
            long expectedGeneration,
            ConversationExchange exchange,
            CancellationToken cancellationToken) => Task.FromResult(true);

        public Task StartNewConversationAsync(
            ConversationScope scope,
            MemoryPolicy policy,
            CancellationToken cancellationToken) => Task.CompletedTask;

        public Task<string?> LoadVoiceAsync(
            ConversationScope scope,
            MemoryPolicy policy,
            CancellationToken cancellationToken) => Task.FromResult<string?>(null);

        public Task<bool> SaveVoiceAsync(
            ConversationScope scope,
            MemoryPolicy policy,
            string voiceName,
            DateTimeOffset requestStartedAt,
            CancellationToken cancellationToken) => Task.FromResult(true);
    }

    private sealed class CaptureHandler : HttpMessageHandler
    {
        public string RequestBody { get; private set; } = string.Empty;

        protected override async Task<HttpResponseMessage> SendAsync(
            HttpRequestMessage request,
            CancellationToken cancellationToken)
        {
            RequestBody = await request.Content!.ReadAsStringAsync(cancellationToken);
            return new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = new StringContent(
                    "{\"choices\":[{\"message\":{\"content\":\"Hello.\"}}]}",
                    Encoding.UTF8,
                    "application/json"),
            };
        }
    }
}
