#include "audio.h"
#include "relay_client.h"
#include "voice_config.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr char kTag[] = "luna";
uint32_t s_turn = 0;
volatile bool s_cancelled = false;
volatile bool s_processing = false;

struct CaptureContext {
  uint32_t turn_id;
};

bool forward_capture_pcm(const uint8_t* pcm, size_t bytes, void* context) {
  const auto* capture = static_cast<const CaptureContext*>(context);
  return capture && !s_cancelled && relay_send_pcm(capture->turn_id, pcm, bytes);
}

void wait_button_release() {
  while (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void button_monitor(void*) {
  while (true) {
    if (s_processing && gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) {
      vTaskDelay(pdMS_TO_TICKS(25));
      if (s_processing && gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) {
        s_cancelled = true;
        relay_cancel_turn(s_turn);
        audio_playback_cancel();
        wait_button_release();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void play_error_tone() {
  if (audio_playback_wait_idle(1500)) audio_play_tone(240, 160, 16);
}
}  // namespace

extern "C" void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  gpio_config_t button = {};
  button.pin_bit_mask = 1ULL << voice_config::kButtonGpio;
  button.mode = GPIO_MODE_INPUT;
  button.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&button));
  ESP_ERROR_CHECK(audio_init());
  ESP_ERROR_CHECK(relay_init());
  xTaskCreate(button_monitor, "luna_button", 3072, nullptr, 6, nullptr);

  const bool connected_at_boot = relay_wait_connected(8000);
  ESP_LOGI(kTag, "READY relay=%s free_internal=%u free_psram=%u",
           connected_at_boot ? "connected" : "connecting",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  audio_play_tone(1000, 130, 32);

  while (true) {
    if (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) != 0) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(25));
    if (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) != 0) continue;

    ++s_turn;
    s_cancelled = false;
    s_processing = false;
    audio_playback_cancel();
    if (!audio_playback_wait_idle(1500)) {
      ESP_LOGW(kTag, "audio did not become idle before recording");
      wait_button_release();
      continue;
    }
    if (!relay_wait_connected(500) || !relay_start_turn(s_turn)) {
      ESP_LOGW(kTag, "relay unavailable for turn %u", static_cast<unsigned>(s_turn));
      play_error_tone();
      wait_button_release();
      continue;
    }

    audio_play_tone(1040, 120, 38);
    CaptureContext capture_context = {s_turn};
    Recording recording = {};
    ESP_LOGI(kTag, "turn %u recording and streaming", static_cast<unsigned>(s_turn));
    const esp_err_t capture = audio_capture_while_pressed(
        &recording, &s_cancelled, forward_capture_pcm, &capture_context);
    if (recording.truncated) {
      ESP_LOGW(kTag, "turn %u reached the %d-second recording limit", static_cast<unsigned>(s_turn),
               voice_config::kMaxRecordSeconds);
      relay_cancel_turn(s_turn);
      wait_button_release();
      if (!s_cancelled) play_error_tone();
      continue;
    }
    wait_button_release();
    const int64_t released_at = esp_timer_get_time();
    ESP_LOGI(kTag, "turn %u released", static_cast<unsigned>(s_turn));

    if (capture != ESP_OK || !recording.has_speech || s_cancelled) {
      relay_cancel_turn(s_turn);
      if (!s_cancelled) {
        ESP_LOGW(kTag, "recording rejected (%s)", esp_err_to_name(capture));
        play_error_tone();
      }
      continue;
    }
    if (relay_input_failed(s_turn) || !relay_commit_turn(s_turn)) {
      ESP_LOGW(kTag, "turn %u relay microphone upload failed or exceeded the drain deadline",
               static_cast<unsigned>(s_turn));
      relay_cancel_turn(s_turn);
      play_error_tone();
      continue;
    }

    // Commit first so STT finalization overlaps the local release feedback.
    audio_play_tone(620, 55, 38);
    s_processing = true;
    const bool spoken = relay_wait_turn_finished(s_turn, 120000);
    s_processing = false;
    const int64_t elapsed_ms = (esp_timer_get_time() - released_at) / 1000;
    if (!spoken && !s_cancelled) {
      ESP_LOGW(kTag, "turn %u relay pipeline failed after %lld ms",
               static_cast<unsigned>(s_turn), static_cast<long long>(elapsed_ms));
      play_error_tone();
    } else if (spoken) {
      ESP_LOGI(kTag, "turn %u complete in %lld ms", static_cast<unsigned>(s_turn),
               static_cast<long long>(elapsed_ms));
    }
  }
}
