#pragma once

#include <stddef.h>
#include <stdint.h>

enum class RelayAudioKind : uint8_t {
  kMicrophonePcm = 1,
  kSpeakerPcm = 2,
};

struct RelayAudioFrame {
  RelayAudioKind kind;
  uint32_t turn_id;
  uint32_t sequence;
  const uint8_t* pcm;
  size_t pcm_bytes;
};

constexpr size_t kRelayAudioHeaderBytes = 16;
constexpr size_t kRelayMaxAudioPayloadBytes = 16 * 1024;

// Encodes one binary LUNA frame. PCM must be signed little-endian PCM16.
bool relay_encode_audio_frame(uint8_t* output, size_t output_capacity, RelayAudioKind kind,
                              uint32_t turn_id, uint32_t sequence, const uint8_t* pcm,
                              size_t pcm_bytes, size_t* encoded_bytes);

// The returned PCM pointer aliases message and remains valid only while message remains valid.
bool relay_decode_audio_frame(const uint8_t* message, size_t message_bytes,
                              RelayAudioFrame* frame);
