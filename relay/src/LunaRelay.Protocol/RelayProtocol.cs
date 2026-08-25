using System.Buffers.Binary;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace LunaRelay.Protocol;

public enum AudioFrameKind : byte
{
    MicrophonePcm = 1,
    SpeakerPcm = 2,
}

public readonly record struct AudioFrame(
    AudioFrameKind Kind,
    uint TurnId,
    uint Sequence,
    ReadOnlyMemory<byte> Pcm);

public static class RelayProtocol
{
    private static ReadOnlySpan<byte> Magic => "LUNA"u8;
    public const int AudioHeaderBytes = 16;
    public const int Version = 1;
    public const int MaximumAudioPayloadBytes = 16 * 1024;

    public static byte[] EncodeAudio(AudioFrameKind kind, uint turnId, uint sequence,
        ReadOnlySpan<byte> pcm)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind));
        }

        if ((pcm.Length & 1) != 0 || pcm.Length > MaximumAudioPayloadBytes)
        {
            throw new ArgumentException("PCM16 payload has an invalid length.", nameof(pcm));
        }

        byte[] message = new byte[AudioHeaderBytes + pcm.Length];
        Magic.CopyTo(message);
        message[4] = Version;
        message[5] = (byte)kind;
        BinaryPrimitives.WriteUInt16LittleEndian(message.AsSpan(6), AudioHeaderBytes);
        BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(8), turnId);
        BinaryPrimitives.WriteUInt32LittleEndian(message.AsSpan(12), sequence);
        pcm.CopyTo(message.AsSpan(AudioHeaderBytes));
        return message;
    }

    public static bool TryDecodeAudio(ReadOnlyMemory<byte> message, out AudioFrame frame)
    {
        frame = default;
        if (message.Length < AudioHeaderBytes || message.Length > AudioHeaderBytes + MaximumAudioPayloadBytes)
        {
            return false;
        }

        ReadOnlySpan<byte> bytes = message.Span;
        if (!bytes[..4].SequenceEqual(Magic) || bytes[4] != Version ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes[6..]) != AudioHeaderBytes)
        {
            return false;
        }

        var kind = (AudioFrameKind)bytes[5];
        int pcmBytes = message.Length - AudioHeaderBytes;
        if (!Enum.IsDefined(kind) || (pcmBytes & 1) != 0)
        {
            return false;
        }

        frame = new AudioFrame(
            kind,
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[8..]),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes[12..]),
            message[AudioHeaderBytes..]);
        return true;
    }

    public static byte[] SerializeControl(ControlMessage message) =>
        JsonSerializer.SerializeToUtf8Bytes(message, JsonOptions);

    public static ControlMessage? DeserializeControl(ReadOnlySpan<byte> utf8) =>
        JsonSerializer.Deserialize<ControlMessage>(utf8, JsonOptions);

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };
}

public sealed record ControlMessage
{
    public required string Type { get; init; }
    public uint? TurnId { get; init; }
    public int? SampleRate { get; init; }
    public string? Format { get; init; }
    public string? Text { get; init; }
    public string? Code { get; init; }
    public string? Message { get; init; }
}
