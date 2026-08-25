#include "relay_protocol.h"

#include <assert.h>
#include <string.h>

int main() {
  const uint8_t pcm[] = {0x00, 0x80, 0xFF, 0x7F, 0x34, 0x12};
  uint8_t encoded[kRelayAudioHeaderBytes + sizeof(pcm)] = {};
  size_t encoded_bytes = 0;

  assert(relay_encode_audio_frame(encoded, sizeof(encoded), RelayAudioKind::kMicrophonePcm,
                                  42, 7, pcm, sizeof(pcm), &encoded_bytes));
  assert(encoded_bytes == sizeof(encoded));

  RelayAudioFrame decoded = {};
  assert(relay_decode_audio_frame(encoded, encoded_bytes, &decoded));
  assert(decoded.kind == RelayAudioKind::kMicrophonePcm);
  assert(decoded.turn_id == 42);
  assert(decoded.sequence == 7);
  assert(decoded.pcm_bytes == sizeof(pcm));
  assert(memcmp(decoded.pcm, pcm, sizeof(pcm)) == 0);

  encoded[0] = 'X';
  assert(!relay_decode_audio_frame(encoded, encoded_bytes, &decoded));
  encoded[0] = 'L';
  encoded[4] = 2;
  assert(!relay_decode_audio_frame(encoded, encoded_bytes, &decoded));

  assert(!relay_encode_audio_frame(encoded, sizeof(encoded), RelayAudioKind::kSpeakerPcm,
                                   1, 1, pcm, 1, &encoded_bytes));
  return 0;
}
