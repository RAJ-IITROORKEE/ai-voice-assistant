#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

struct Recording {
  size_t data_bytes;
  bool truncated;
  bool has_speech;
};

// Called with each complete 20 ms PCM capture block for immediate relay forwarding.
// Returning false stops forwarding while capture continues for speech validation.
using CaptureAudioCallback = bool (*)(const uint8_t* pcm, size_t len, void* context);

// Initializes the ES8311/I2S hardware and PSRAM playback ring.
esp_err_t audio_init(void);

// Captures and forwards 16-bit mono PCM until button release or the configured limit.
esp_err_t audio_capture_while_pressed(Recording* recording, volatile bool* cancelled,
                                      CaptureAudioCallback callback = nullptr,
                                      void* callback_context = nullptr);

// TTS stream producer API. HTTP callbacks enqueue arbitrary byte boundaries;
// a dedicated task feeds I2S after a PSRAM prebuffer is available.
esp_err_t audio_playback_begin(uint32_t turn_id);
bool audio_playback_write(const uint8_t* data, size_t len, uint32_t turn_id, uint32_t timeout_ms);
bool audio_playback_finish(uint32_t turn_id);
void audio_playback_cancel(void);
bool audio_playback_is_active(void);
bool audio_playback_wait_idle(uint32_t timeout_ms);

// Short local feedback sounds use the same ES8311/I2S path and never overlap TTS.
void audio_play_tone(uint16_t frequency_hz, uint16_t duration_ms, uint8_t volume_percent);
