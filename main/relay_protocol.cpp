#include "relay_protocol.h"

namespace {
constexpr uint8_t kMagic[] = {'L', 'U', 'N', 'A'};
constexpr uint8_t kVersion = 1;

void write_u16_le(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
}

void write_u32_le(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8);
  output[2] = static_cast<uint8_t>(value >> 16);
  output[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t read_u16_le(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(input[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8) |
         (static_cast<uint32_t>(input[2]) << 16) |
         (static_cast<uint32_t>(input[3]) << 24);
}

bool valid_kind(RelayAudioKind kind) {
  return kind == RelayAudioKind::kMicrophonePcm || kind == RelayAudioKind::kSpeakerPcm;
}
}  // namespace

bool relay_encode_audio_frame(uint8_t* output, size_t output_capacity, RelayAudioKind kind,
                              uint32_t turn_id, uint32_t sequence, const uint8_t* pcm,
                              size_t pcm_bytes, size_t* encoded_bytes) {
  if (!output || !encoded_bytes || (!pcm && pcm_bytes) || !valid_kind(kind) ||
      (pcm_bytes & 1U) || pcm_bytes > kRelayMaxAudioPayloadBytes ||
      output_capacity < kRelayAudioHeaderBytes + pcm_bytes) {
    return false;
  }

  output[0] = kMagic[0];
  output[1] = kMagic[1];
  output[2] = kMagic[2];
  output[3] = kMagic[3];
  output[4] = kVersion;
  output[5] = static_cast<uint8_t>(kind);
  write_u16_le(output + 6, kRelayAudioHeaderBytes);
  write_u32_le(output + 8, turn_id);
  write_u32_le(output + 12, sequence);
  for (size_t i = 0; i < pcm_bytes; ++i) output[kRelayAudioHeaderBytes + i] = pcm[i];
  *encoded_bytes = kRelayAudioHeaderBytes + pcm_bytes;
  return true;
}

bool relay_decode_audio_frame(const uint8_t* message, size_t message_bytes,
                              RelayAudioFrame* frame) {
  if (!message || !frame || message_bytes < kRelayAudioHeaderBytes ||
      message_bytes > kRelayAudioHeaderBytes + kRelayMaxAudioPayloadBytes ||
      message[0] != kMagic[0] || message[1] != kMagic[1] || message[2] != kMagic[2] ||
      message[3] != kMagic[3] || message[4] != kVersion ||
      read_u16_le(message + 6) != kRelayAudioHeaderBytes) {
    return false;
  }

  const RelayAudioKind kind = static_cast<RelayAudioKind>(message[5]);
  const size_t pcm_bytes = message_bytes - kRelayAudioHeaderBytes;
  if (!valid_kind(kind) || (pcm_bytes & 1U)) return false;

  *frame = {kind, read_u32_le(message + 8), read_u32_le(message + 12),
            message + kRelayAudioHeaderBytes, pcm_bytes};
  return true;
}
