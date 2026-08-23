#include "audio.h"
#include "cloud.h"
#include "voice_config.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <time.h>
#include <string.h>

namespace {
constexpr char kTag[] = "luna";
uint32_t s_turn = 0;
volatile bool s_cancelled = false;
volatile bool s_processing = false;
volatile bool s_tts_busy = false;
volatile uint32_t s_tts_failed_turn = 0;
QueueHandle_t s_sentence_queue = nullptr;
uint8_t* s_tts_cache = nullptr;

struct SentenceItem { uint32_t turn_id; char text[voice_config::kSentenceBytes]; };
struct TtsCache { size_t used; bool overflow; };

bool tts_cache_sink(const uint8_t* pcm, size_t len, uint32_t, void* context) {
  auto* cache = static_cast<TtsCache*>(context);
  if (!s_tts_cache || len > voice_config::kTtsCacheBytes - cache->used) { cache->overflow = true; return false; }
  memcpy(s_tts_cache + cache->used, pcm, len);
  cache->used += len;
  return true;
}

bool synthesize_and_play(const char* text, uint32_t turn_id, CloudResult* result) {
  TtsCache cache = {};
  if (!cloud_stream_tts(text, turn_id, tts_cache_sink, &cache, &s_cancelled, result) || cache.overflow || !cache.used) return false;
  if (audio_playback_begin(turn_id) != ESP_OK) return false;
  const bool played = audio_playback_write(s_tts_cache, cache.used, turn_id, 30000);
  audio_playback_finish(turn_id);
  return played;
}

bool queue_sentence(const char* text, uint32_t turn_id, void*) {
  SentenceItem item = {}; item.turn_id = turn_id; snprintf(item.text, sizeof(item.text), "%s", text);
  return xQueueSend(s_sentence_queue, &item, 0) == pdPASS;
}

void tts_worker(void*) {
  SentenceItem item;
  while (true) {
    if (xQueueReceive(s_sentence_queue, &item, portMAX_DELAY) != pdPASS) continue;
    if (item.turn_id != s_turn || s_cancelled) continue;
    s_tts_busy = true;
    CloudResult result = {};
    const bool ok = synthesize_and_play(item.text, item.turn_id, &result);
    if (!ok && !s_cancelled) {
      s_tts_failed_turn = item.turn_id;
      ESP_LOGW(kTag, "TTS: %s (%d)", cloud_error_name(result.error), result.http_status);
    }
    s_tts_busy = false;
  }
}

void button_monitor(void*) {
  while (true) {
    if (s_processing && gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) {
      vTaskDelay(pdMS_TO_TICKS(25));
      if (s_processing && gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) {
        s_cancelled = true;
        audio_playback_cancel();
        while (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

bool wait_for_tts(uint32_t turn_id) {
  for (int i = 0; i < 12000; ++i) {
    if (s_cancelled || s_tts_failed_turn == turn_id) return false;
    if (!s_tts_busy && uxQueueMessagesWaiting(s_sentence_queue) == 0) return true;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  s_tts_failed_turn = turn_id;
  return false;
}

void speak_greeting() {
  setenv("TZ", voice_config::kTimezone, 1); tzset();
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  if (esp_netif_sntp_init(&config) == ESP_OK) esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
  time_t now = time(nullptr); struct tm local = {}; localtime_r(&now, &local);
  const char* greeting = local.tm_year >= 120 && local.tm_hour < 12 ? "Good morning. I am Luna. Hold the button and speak." :
                         local.tm_year >= 120 && local.tm_hour < 17 ? "Good afternoon. I am Luna. Hold the button and speak." :
                         local.tm_year >= 120 ? "Good evening. I am Luna. Hold the button and speak." :
                                                "Hello. I am Luna. Hold the button and speak.";
  CloudResult result = {};
  if (!synthesize_and_play(greeting, 0, &result)) ESP_LOGW(kTag, "greeting TTS: %s (%d)", cloud_error_name(result.error), result.http_status);
}

void wait_button_release() { while (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) vTaskDelay(pdMS_TO_TICKS(10)); }
}

extern "C" void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init()); gpio_config_t button = {}; button.pin_bit_mask = 1ULL << voice_config::kButtonGpio; button.mode = GPIO_MODE_INPUT; button.pull_up_en = GPIO_PULLUP_ENABLE; ESP_ERROR_CHECK(gpio_config(&button));
  ESP_ERROR_CHECK(audio_init()); ESP_ERROR_CHECK(cloud_init_wifi());
  s_tts_cache = static_cast<uint8_t*>(heap_caps_malloc(voice_config::kTtsCacheBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  configASSERT(s_tts_cache);
  s_sentence_queue = xQueueCreate(voice_config::kSentenceQueueDepth, sizeof(SentenceItem));
  configASSERT(s_sentence_queue); xTaskCreate(tts_worker, "luna_tts", 8192, nullptr, 5, nullptr); xTaskCreate(button_monitor, "luna_button", 3072, nullptr, 6, nullptr);
  ESP_LOGI(kTag, "READY free_internal=%u free_psram=%u", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  speak_greeting();
  while (true) { if (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) != 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; } vTaskDelay(pdMS_TO_TICKS(25)); if (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) != 0) continue;
    ++s_turn; s_cancelled = false; s_tts_failed_turn = 0; audio_playback_cancel(); Recording recording = {}; ESP_LOGI(kTag, "turn %u recording", static_cast<unsigned>(s_turn)); esp_err_t capture = audio_capture_while_pressed(&recording, &s_cancelled); wait_button_release(); if (capture != ESP_OK || !recording.has_speech) { ESP_LOGW(kTag, "recording rejected (%s)", esp_err_to_name(capture)); audio_play_tone(260, 120, 12); continue; }
    audio_play_tone(660, 70, 10); char transcript[1024] = {}; CloudResult result = {}; s_processing = true;
    if (!cloud_transcribe(recording, transcript, sizeof(transcript), &s_cancelled, &result)) { if (!s_cancelled) { ESP_LOGW(kTag, "STT: %s (%d)", cloud_error_name(result.error), result.http_status); audio_play_tone(240, 180, 12); } s_processing = false; continue; }
    ESP_LOGI(kTag, "turn %u generating", static_cast<unsigned>(s_turn)); bool generated = cloud_stream_response(transcript, s_turn, queue_sentence, nullptr, &s_cancelled, &result); bool spoken = generated && wait_for_tts(s_turn); s_processing = false;
    if (!generated || !spoken) { if (!s_cancelled) { ESP_LOGW(kTag, "LLM/TTS: %s (%d)", cloud_error_name(result.error), result.http_status); audio_play_tone(240, 180, 12); } } else ESP_LOGI(kTag, "turn %u complete", static_cast<unsigned>(s_turn));
  }
}
