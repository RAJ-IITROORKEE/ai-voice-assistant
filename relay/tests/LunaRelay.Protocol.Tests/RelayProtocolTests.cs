using System.Net;
using LunaRelay.Protocol;
using LunaRelay;
using LunaRelay.Setup;
using Xunit;

namespace LunaRelay.Protocol.Tests;

public sealed class RelayProtocolTests
{
    [Fact]
    public void AudioFrameRoundTripsWithoutChangingPcm()
    {
        byte[] pcm = [0x00, 0x80, 0xFF, 0x7F, 0x34, 0x12];

        byte[] encoded = RelayProtocol.EncodeAudio(
            AudioFrameKind.MicrophonePcm, 42, 7, pcm);

        Assert.True(RelayProtocol.TryDecodeAudio(encoded, out AudioFrame decoded));
        Assert.Equal(AudioFrameKind.MicrophonePcm, decoded.Kind);
        Assert.Equal(42u, decoded.TurnId);
        Assert.Equal(7u, decoded.Sequence);
        Assert.Equal(pcm, decoded.Pcm.ToArray());
    }

    [Fact]
    public void DecoderRejectsBadMagicVersionAndOddPcm()
    {
        byte[] valid = RelayProtocol.EncodeAudio(
            AudioFrameKind.SpeakerPcm, 1, 2, new byte[] { 1, 2 });

        byte[] badMagic = (byte[])valid.Clone();
        badMagic[0] ^= 0xFF;
        Assert.False(RelayProtocol.TryDecodeAudio(badMagic, out _));

        byte[] badVersion = (byte[])valid.Clone();
        badVersion[4] = 2;
        Assert.False(RelayProtocol.TryDecodeAudio(badVersion, out _));

        Assert.Throws<ArgumentException>(() => RelayProtocol.EncodeAudio(
            AudioFrameKind.MicrophonePcm, 1, 2, new byte[] { 1 }));
    }

    [Fact]
    public void ControlMessageUsesStableSnakeCaseContract()
    {
        var message = new ControlMessage
        {
            Type = "turn.start",
            TurnId = 99,
            SampleRate = 16000,
            Format = "pcm_s16le",
        };

        string json = System.Text.Encoding.UTF8.GetString(
            RelayProtocol.SerializeControl(message));
        ControlMessage? decoded = RelayProtocol.DeserializeControl(
            System.Text.Encoding.UTF8.GetBytes(json));

        Assert.Contains("\"turn_id\":99", json, StringComparison.Ordinal);
        Assert.Equal(message, decoded);
    }

    [Fact]
    public void TtsAudioBacklogIsByteBoundedWithoutDroppingQueuedAudio()
    {
        var backlog = new TtsAudioBacklog(100);

        Assert.True(backlog.TryReserve(60));
        Assert.False(backlog.TryReserve(41));
        Assert.Equal(60, backlog.BytesQueued);

        backlog.Release(60);

        Assert.Equal(0, backlog.BytesQueued);
        Assert.True(backlog.TryReserve(100));
    }

    [Theory]
    [InlineData(57599, 57600, false, false)]
    [InlineData(57600, 57600, false, true)]
    [InlineData(1, 57600, true, true)]
    [InlineData(0, 57600, true, false)]
    public void TtsInitialDeliveryRequiresAudioLeadUnlessInputIsComplete(
        int bufferedBytes, int minimumBytes, bool inputComplete, bool expected)
    {
        Assert.Equal(expected,
            TtsDeliveryPolicy.IsInitialBufferReady(bufferedBytes, minimumBytes, inputComplete));
    }

    [Fact]
    public void RelaySetupPrefersActiveWirelessLanAddress()
    {
        IPAddress selected = LanAddressSelector.Select([
            new LanAddressCandidate(true, false, IPAddress.Parse("192.168.56.1")),
            new LanAddressCandidate(true, true, IPAddress.Parse("10.70.93.5")),
        ]);

        Assert.Equal(IPAddress.Parse("10.70.93.5"), selected);
    }

    [Fact]
    public void CloudRelayDoesNotRequireAContainerCertificate()
    {
        var configuration = new RelayConfiguration(
            new RelayOptions { ListenPort = 8080, UseTls = false, DeviceToken = "device-token" },
            new AzureSpeechOptions { Key = "speech-key", Region = "eastus", DefaultServiceVoice = "en-IN-NeerjaNeural" },
            new AzureOpenAiOptions
            {
                Endpoint = "https://example.test/openai/v1/chat/completions",
                ApiKey = "agent-key",
                Model = "DeepSeek-V4-Flash",
            },
            new FirestoreOptions { ConversationCollection = "luna_agent_sessions" },
            new AssistantOptions());

        configuration.Validate(VoiceCatalog.Create("en-IN-NeerjaNeural"), ToolRegistry.Empty);
    }
}
