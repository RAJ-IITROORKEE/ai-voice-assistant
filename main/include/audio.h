#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

struct Recording {
  uint8_t* wav;
  size_t capacity_bytes;
  size_t data_bytes;
  bool truncated;
  bool has_speech;
};

// Initializes the ES8311/I2S hardware, PSRAM capture buffer, and playback task.
esp_err_t audio_init(void);

// Captures 16-bit mono PCM until the active-low button is released or 60 s elapse.
// The returned WAV pointer remains valid until audio_reset_recording is called.
esp_err_t audio_capture_while_pressed(Recording* recording, volatile bool* cancelled);
void audio_reset_recording(void);

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
