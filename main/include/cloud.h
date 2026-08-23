#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

struct Recording;

enum class CloudError : uint8_t {
  kNone,
  kCancelled,
  kNetwork,
  kAuthentication,
  kRateLimited,
  kService,
  kProtocol,
  kMemory,
};

struct CloudResult {
  CloudError error;
  int http_status;
  char message[96];
};

using SentenceCallback = bool (*)(const char* sentence, uint32_t turn_id, void* context);
using TtsAudioCallback = bool (*)(const uint8_t* pcm, size_t len, uint32_t turn_id, void* context);

esp_err_t cloud_init_wifi(void);
bool cloud_wifi_connected(void);

// Uses the documented short-audio REST endpoint with a bounded WAV upload.
bool cloud_transcribe(const Recording& recording, char* transcript, size_t transcript_size,
                      volatile bool* cancelled, CloudResult* result);

// Processes a fragmented Responses SSE stream and calls sentence_callback before completion.
bool cloud_stream_response(const char* transcript, uint32_t turn_id,
                           SentenceCallback sentence_callback, void* context,
                           volatile bool* cancelled, CloudResult* result);

// Streams raw 24 kHz mono PCM to tts_audio_callback. The callback must copy data before returning.
bool cloud_stream_tts(const char* text, uint32_t turn_id, TtsAudioCallback tts_audio_callback,
                      void* context, volatile bool* cancelled, CloudResult* result);

const char* cloud_error_name(CloudError error);
