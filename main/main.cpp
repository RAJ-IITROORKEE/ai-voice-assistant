#include "audio.h"
#include "cloud.h"
#include "tts_pipeline_policy.h"
#include "voice_config.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

namespace {
constexpr char kTag[] = "luna";
uint32_t s_turn = 0;
volatile bool s_cancelled = false;
volatile bool s_processing = false;
volatile uint32_t s_tts_failed_turn = 0;
QueueHandle_t s_direct_tts_queue = nullptr;
QueueHandle_t s_prefetch_tts_queue = nullptr;
QueueHandle_t s_prefetched_audio_queue = nullptr;
SemaphoreHandle_t s_tts_done = nullptr;
SemaphoreHandle_t s_lookahead_free = nullptr;
uint8_t* s_first_pcm = nullptr;
uint8_t* s_lookahead_pcm = nullptr;
int64_t s_release_time_us = 0;
size_t s_accepted_tts_segments = 0;

enum class TtsMessageType : uint8_t { kSentence, kFinish };
struct SentenceItem { TtsMessageType type; uint32_t turn_id; char text[voice_config::kSentenceBytes]; };
enum class PrefetchedMessageType : uint8_t { kAudio, kFinish, kFailed };
struct PrefetchedItem {
  PrefetchedMessageType type;
  uint32_t turn_id;
  size_t bytes;
  CloudResult result;
};
struct SegmentCache {
  uint8_t* bytes;
  size_t capacity;
  size_t used;
  bool received_pcm;
  bool overflow;
  bool log_first_pcm;
};

static_assert(voice_config::kAudioPrebufferSamples <= voice_config::kAudioRingSamples,
              "Playback prebuffer must fit in the ring");
static_assert(voice_config::kTtsLookaheadBytes <=
                  static_cast<size_t>(voice_config::kAudioRingSamples - voice_config::kAudioPrebufferSamples) * sizeof(int16_t),
              "Look-ahead audio must fit after the retained playback lead");

bool segment_cache_sink(const uint8_t* pcm, size_t len, uint32_t turn_id, void* context) {
  auto* cache = static_cast<SegmentCache*>(context);
  if (!cache->received_pcm && cache->log_first_pcm && turn_id && s_release_time_us) {
    ESP_LOGI(kTag, "turn %u first TTS PCM at %lld ms after release", static_cast<unsigned>(turn_id),
             static_cast<long long>((esp_timer_get_time() - s_release_time_us) / 1000));
  }
  if (!pcm || cache->used + len > cache->capacity) {
    cache->overflow = true;
    return false;
  }
  memcpy(cache->bytes + cache->used, pcm, len);
  cache->used += len;
  cache->received_pcm = true;
  return true;
}

bool synthesize_to_cache(const char* text, uint32_t turn_id, SegmentCache* cache, CloudResult* result) {
  const int64_t request_started = esp_timer_get_time();
  const bool ok = cloud_stream_tts(text, turn_id, segment_cache_sink, cache, &s_cancelled, result) &&
                  cache->received_pcm && !cache->overflow;
  ESP_LOGI(kTag, "turn %u TTS segment cached in %lld ms; bytes=%u", static_cast<unsigned>(turn_id),
           static_cast<long long>((esp_timer_get_time() - request_started) / 1000),
           static_cast<unsigned>(cache->used));
  return ok;
}

bool queue_prefetched(const PrefetchedItem& item) {
  return xQueueSend(s_prefetched_audio_queue, &item, portMAX_DELAY) == pdPASS;
}

bool queue_sentence(const char* text, uint32_t turn_id, void*) {
  SentenceItem item = {}; item.type = TtsMessageType::kSentence; item.turn_id = turn_id;
  snprintf(item.text, sizeof(item.text), "%s", text);
  const bool direct = tts_segment_route(s_accepted_tts_segments) == TtsSegmentRoute::kDirect;
  QueueHandle_t queue = direct ? s_direct_tts_queue : s_prefetch_tts_queue;
  bool queued = false;
  for (int i = 0; i < 40 && !s_cancelled; ++i) {
    // Keep one input slot free so the terminal marker cannot be starved by text.
    if (!direct && uxQueueMessagesWaiting(queue) >= voice_config::kTtsPrefetchQueueDepth - 1) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (xQueueSend(queue, &item, pdMS_TO_TICKS(50)) == pdPASS) {
      queued = true;
      break;
    }
  }
  if (queued) {
    const bool first_segment = s_accepted_tts_segments == 0;
    ++s_accepted_tts_segments;
    if (!first_segment) return true;
    ESP_LOGI(kTag, "turn %u first sentence at %lld ms after release", static_cast<unsigned>(turn_id),
              static_cast<long long>((esp_timer_get_time() - s_release_time_us) / 1000));
  }
  return queued;
}

void prefetch_tts_worker(void*) {
  SentenceItem item;
  while (true) {
    if (xQueueReceive(s_prefetch_tts_queue, &item, portMAX_DELAY) != pdPASS) continue;
    if (item.turn_id != s_turn) continue;

    if (item.type == TtsMessageType::kFinish) {
      const PrefetchedItem finish = {PrefetchedMessageType::kFinish, item.turn_id, 0, {}};
      queue_prefetched(finish);
      continue;
    }

    PrefetchedItem ready = {PrefetchedMessageType::kFailed, item.turn_id, 0, {}};
    if (xSemaphoreTake(s_lookahead_free, pdMS_TO_TICKS(120000)) != pdPASS) {
      ready.result.error = CloudError::kMemory;
      snprintf(ready.result.message, sizeof(ready.result.message), "%s", "look-ahead slot busy");
      queue_prefetched(ready);
      continue;
    }

    SegmentCache cache = {s_lookahead_pcm, voice_config::kTtsLookaheadBytes, 0, false, false, false};
    CloudResult result = {};
    const bool ok = !s_cancelled && synthesize_to_cache(item.text, item.turn_id, &cache, &result);
    ready.type = ok ? PrefetchedMessageType::kAudio : PrefetchedMessageType::kFailed;
    ready.bytes = cache.used;
    ready.result = result;
    if (ok) {
      ESP_LOGI(kTag, "turn %u look-ahead TTS cached %u bytes", static_cast<unsigned>(item.turn_id),
               static_cast<unsigned>(cache.used));
    } else {
      xSemaphoreGive(s_lookahead_free);
    }
    if (!queue_prefetched(ready) && ok) xSemaphoreGive(s_lookahead_free);
  }
}

void tts_worker(void*) {
  SentenceItem first;
  while (true) {
    if (xQueueReceive(s_direct_tts_queue, &first, portMAX_DELAY) != pdPASS) continue;
    if (first.turn_id != s_turn) continue;
    if (first.type == TtsMessageType::kFinish) {
      xSemaphoreGive(s_tts_done);
      continue;
    }

    SegmentCache first_cache = {s_first_pcm, voice_config::kTtsLookaheadBytes, 0, false, false, true};
    CloudResult direct_result = {};
    bool failed = s_cancelled || !synthesize_to_cache(first.text, first.turn_id, &first_cache, &direct_result);
    if (!failed) {
      failed = audio_playback_begin(first.turn_id) != ESP_OK ||
               !audio_playback_write(first_cache.bytes, first_cache.used, first.turn_id,
                                     voice_config::kTtsWriteTimeoutMs);
    }
    if (failed && !s_cancelled) {
      ESP_LOGW(kTag, "TTS: %s (%d)", cloud_error_name(direct_result.error), direct_result.http_status);
      audio_playback_cancel();
    }

    bool finished = false;
    while (!finished) {
      PrefetchedItem item = {};
      if (xQueueReceive(s_prefetched_audio_queue, &item, pdMS_TO_TICKS(120000)) != pdPASS) {
        failed = true;
        ESP_LOGW(kTag, "TTS look-ahead timed out");
        audio_playback_cancel();
        break;
      }
      if (item.turn_id != first.turn_id) {
        if (item.type == PrefetchedMessageType::kAudio) xSemaphoreGive(s_lookahead_free);
        continue;
      }
      if (item.type == PrefetchedMessageType::kAudio) {
        const bool copied = !failed && !s_cancelled &&
                            audio_playback_write(s_lookahead_pcm, item.bytes, item.turn_id,
                                                 voice_config::kTtsWriteTimeoutMs);
        xSemaphoreGive(s_lookahead_free);
        if (!copied) {
          failed = true;
          audio_playback_cancel();
        }
        continue;
      }
      if (item.type == PrefetchedMessageType::kFailed) {
        failed = true;
        if (!s_cancelled) ESP_LOGW(kTag, "look-ahead TTS: %s (%d)",
                                   cloud_error_name(item.result.error), item.result.http_status);
        audio_playback_cancel();
        continue;
      }
      finished = true;
    }

    if (!failed && !s_cancelled && audio_playback_is_active() && !audio_playback_finish(first.turn_id)) failed = true;
    if (failed && !s_cancelled) s_tts_failed_turn = first.turn_id;
    xSemaphoreGive(s_tts_done);
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

bool queue_tts_finish(uint32_t turn_id) {
  const SentenceItem finish = {TtsMessageType::kFinish, turn_id, {}};
  QueueHandle_t queue = s_accepted_tts_segments ? s_prefetch_tts_queue : s_direct_tts_queue;
  for (int i = 0; i < 40; ++i) {
    if (xQueueSend(queue, &finish, pdMS_TO_TICKS(50)) == pdPASS) return true;
  }
  return false;
}

void wait_button_release() { while (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) vTaskDelay(pdMS_TO_TICKS(10)); }
}

extern "C" void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init()); gpio_config_t button = {}; button.pin_bit_mask = 1ULL << voice_config::kButtonGpio; button.mode = GPIO_MODE_INPUT; button.pull_up_en = GPIO_PULLUP_ENABLE; ESP_ERROR_CHECK(gpio_config(&button));
  ESP_ERROR_CHECK(audio_init()); ESP_ERROR_CHECK(cloud_init_wifi());
  s_direct_tts_queue = xQueueCreate(2, sizeof(SentenceItem));
  s_prefetch_tts_queue = xQueueCreate(voice_config::kTtsPrefetchQueueDepth, sizeof(SentenceItem));
  s_prefetched_audio_queue = xQueueCreate(2, sizeof(PrefetchedItem));
  s_tts_done = xSemaphoreCreateBinary();
  s_lookahead_free = xSemaphoreCreateBinary();
  s_first_pcm = static_cast<uint8_t*>(heap_caps_malloc(voice_config::kTtsLookaheadBytes,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  s_lookahead_pcm = static_cast<uint8_t*>(heap_caps_malloc(voice_config::kTtsLookaheadBytes,
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  configASSERT(s_direct_tts_queue && s_prefetch_tts_queue && s_prefetched_audio_queue &&
               s_tts_done && s_lookahead_free && s_first_pcm && s_lookahead_pcm);
  xSemaphoreGive(s_lookahead_free);
  xTaskCreate(tts_worker, "luna_tts", 8192, nullptr, 5, nullptr);
  xTaskCreate(prefetch_tts_worker, "luna_tts_next", 8192, nullptr, 5, nullptr);
  xTaskCreate(button_monitor, "luna_button", 3072, nullptr, 6, nullptr);
  ESP_LOGI(kTag, "READY free_internal=%u free_psram=%u", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  // A cloud greeting previously blocked push-to-talk for the full TTS request.
  // This local ready tone keeps boot immediately usable while greeting caching is deferred.
  audio_play_tone(1000, 130, 32);
  while (true) { if (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) != 0) { vTaskDelay(pdMS_TO_TICKS(20)); continue; } vTaskDelay(pdMS_TO_TICKS(25)); if (gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) != 0) continue;
    ++s_turn; s_cancelled = false; s_tts_failed_turn = 0; s_accepted_tts_segments = 0; s_release_time_us = 0;
    audio_playback_cancel();
    if (!audio_playback_wait_idle(1500)) {
      ESP_LOGW(kTag, "audio did not become idle before recording");
      wait_button_release();
      continue;
    }
    while (xSemaphoreTake(s_tts_done, 0) == pdPASS) {}
    audio_play_tone(1040, 120, 38);
    Recording recording = {}; ESP_LOGI(kTag, "turn %u recording", static_cast<unsigned>(s_turn));
    esp_err_t capture = audio_capture_while_pressed(&recording, &s_cancelled);
    wait_button_release(); s_release_time_us = esp_timer_get_time();
    ESP_LOGI(kTag, "turn %u released", static_cast<unsigned>(s_turn));
    audio_play_tone(620, 90, 38);
    if (capture != ESP_OK || !recording.has_speech) { ESP_LOGW(kTag, "recording rejected (%s)", esp_err_to_name(capture)); audio_play_tone(260, 120, 12); continue; }
    char transcript[1024] = {}; CloudResult result = {}; s_processing = true;
    const bool transcribed = cloud_transcribe(recording, transcript, sizeof(transcript), &s_cancelled, &result);
    ESP_LOGI(kTag, "turn %u STT final at %lld ms after release", static_cast<unsigned>(s_turn),
             static_cast<long long>((esp_timer_get_time() - s_release_time_us) / 1000));
    if (!transcribed) { if (!s_cancelled) { ESP_LOGW(kTag, "STT: %s (%d)", cloud_error_name(result.error), result.http_status); audio_play_tone(240, 180, 12); } s_processing = false; continue; }
    ESP_LOGI(kTag, "turn %u generating", static_cast<unsigned>(s_turn));
    const bool generated = cloud_stream_response(transcript, s_turn, queue_sentence, nullptr, &s_cancelled, &result);
    const bool finish_queued = queue_tts_finish(s_turn);
    if (!finish_queued) audio_playback_cancel();
    const bool tts_drained = finish_queued && xSemaphoreTake(s_tts_done, pdMS_TO_TICKS(120000)) == pdPASS;
    const bool spoken = generated && tts_drained && s_tts_failed_turn != s_turn;
    s_processing = false;
    if (!generated || !spoken) { if (!s_cancelled) { ESP_LOGW(kTag, "LLM/TTS: %s (%d)", cloud_error_name(result.error), result.http_status); audio_play_tone(240, 180, 12); } } else ESP_LOGI(kTag, "turn %u complete", static_cast<unsigned>(s_turn));
  }
}
