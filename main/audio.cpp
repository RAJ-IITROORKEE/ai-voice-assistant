#include "audio.h"
#include "playback_policy.h"
#include "voice_config.h"

#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr char kTag[] = "audio";
constexpr i2s_port_t kPort = I2S_NUM_1;
constexpr uint8_t kCodecAddress = 0x18;
uint8_t* s_pcm_ring = nullptr;
constexpr size_t kRingBytes = static_cast<size_t>(voice_config::kAudioRingSamples) * sizeof(int16_t);
constexpr size_t kPrebufferBytes = static_cast<size_t>(voice_config::kAudioPrebufferSamples) * sizeof(int16_t);
constexpr size_t kRebufferBytes = static_cast<size_t>(voice_config::kAudioRebufferSamples) * sizeof(int16_t);
constexpr size_t kPlaybackBlockBytes = static_cast<size_t>(voice_config::kPlaybackBlockSamples) * sizeof(int16_t);
constexpr uint32_t kTonePlaybackTurn = UINT32_MAX;
SemaphoreHandle_t s_ring_mutex = nullptr;
SemaphoreHandle_t s_data_ready = nullptr;
SemaphoreHandle_t s_space_ready = nullptr;
SemaphoreHandle_t s_playback_done = nullptr;
size_t s_ring_read = 0;
size_t s_ring_write = 0;
size_t s_ring_used = 0;
bool s_playing = false;
bool s_started = false;
bool s_finish_requested = false;
bool s_rebuffering = false;
uint32_t s_playback_turn = 0;
size_t s_peak_ring_bytes = 0;
uint32_t s_underrun_count = 0;
TickType_t s_rebuffer_started = 0;
uint8_t s_pcm_tail = 0;
bool s_has_pcm_tail = false;

esp_err_t codec_write(uint8_t reg, uint8_t value) {
  uint8_t bytes[] = {reg, value};
  return i2c_master_write_to_device(I2C_NUM_0, kCodecAddress, bytes, sizeof(bytes), pdMS_TO_TICKS(100));
}

void codec_enable_mic() {
  const uint8_t settings[][2] = {{0x00, 0x80}, {0x01, 0xBA}, {0x02, 0x18}, {0x0D, 0x01},
                                 {0x0E, 0x02}, {0x14, 0x10}, {0x17, 0xFF}, {0x1C, 0x6A}};
  for (const auto& setting : settings) codec_write(setting[0], setting[1]);
}

void codec_enable_speaker() {
  const uint8_t settings[][2] = {{0x00, 0x80}, {0x01, 0xB5}, {0x02, 0x18}, {0x0D, 0x01},
                                 {0x12, 0x00}, {0x13, 0x10}, {0x32, 0xD0}, {0x37, 0x08}};
  for (const auto& setting : settings) codec_write(setting[0], setting[1]);
  gpio_set_level(static_cast<gpio_num_t>(voice_config::kAmpEnableGpio), 1);
}

void set_rate(int rate) {
  i2s_set_clk(kPort, rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

void ring_copy_in(const uint8_t* data, size_t bytes) {
  const size_t first = (bytes < kRingBytes - s_ring_write) ? bytes : kRingBytes - s_ring_write;
  memcpy(s_pcm_ring + s_ring_write, data, first);
  if (bytes > first) memcpy(s_pcm_ring, data + first, bytes - first);
  s_ring_write = (s_ring_write + bytes) % kRingBytes;
  s_ring_used += bytes;
  if (s_ring_used > s_peak_ring_bytes) s_peak_ring_bytes = s_ring_used;
}

void ring_copy_out(uint8_t* data, size_t bytes) {
  const size_t first = (bytes < kRingBytes - s_ring_read) ? bytes : kRingBytes - s_ring_read;
  memcpy(data, s_pcm_ring + s_ring_read, first);
  if (bytes > first) memcpy(data + first, s_pcm_ring, bytes - first);
  s_ring_read = (s_ring_read + bytes) % kRingBytes;
  s_ring_used -= bytes;
}

bool enqueue_pcm(const uint8_t* data, size_t bytes, uint32_t turn_id, uint32_t timeout_ms) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (bytes) {
    if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(20)) != pdPASS) continue;
    if (!s_playing || s_finish_requested || !playback_turn_matches(s_playback_turn, turn_id)) {
      xSemaphoreGive(s_ring_mutex);
      return false;
    }
    const size_t free_bytes = kRingBytes - s_ring_used;
    const size_t copy = (bytes < free_bytes) ? bytes : free_bytes;
    if (copy) ring_copy_in(data, copy);
    xSemaphoreGive(s_ring_mutex);
    if (copy) {
      data += copy;
      bytes -= copy;
      xSemaphoreGive(s_data_ready);
      continue;
    }
    if (timeout_ms == 0 || static_cast<int32_t>(deadline - xTaskGetTickCount()) <= 0 ||
        xSemaphoreTake(s_space_ready, deadline - xTaskGetTickCount()) != pdPASS) return false;
  }
  return true;
}

void playback_task(void*) {
  uint8_t block[kPlaybackBlockBytes];
  while (true) {
    xSemaphoreTake(s_data_ready, portMAX_DELAY);
    while (true) {
      size_t bytes = 0;
      bool stop = false;
      xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
      if (!s_playing) {
        xSemaphoreGive(s_ring_mutex);
        break;
      }
      stop = s_finish_requested && s_ring_used == 0;
      const size_t start_threshold = s_rebuffering ? kRebufferBytes : kPrebufferBytes;
      if (!stop && (!s_started || s_rebuffering) &&
          !playback_should_start(s_ring_used, start_threshold, s_finish_requested)) {
        xSemaphoreGive(s_ring_mutex);
        break;
      }
      if (!s_started) {
        s_started = true;
        ESP_LOGI(kTag, "turn %u I2S playback started; buffered=%u", static_cast<unsigned>(s_playback_turn),
                 static_cast<unsigned>(s_ring_used));
      } else if (s_rebuffering) {
        s_rebuffering = false;
        ESP_LOGW(kTag, "turn %u playback resumed after %u ms; buffered=%u",
                 static_cast<unsigned>(s_playback_turn),
                 static_cast<unsigned>((xTaskGetTickCount() - s_rebuffer_started) * portTICK_PERIOD_MS),
                 static_cast<unsigned>(s_ring_used));
      }
      bytes = (s_ring_used < sizeof(block)) ? s_ring_used : sizeof(block);
      if (bytes) ring_copy_out(block, bytes);
      stop = s_finish_requested && s_ring_used == 0;
      if (!bytes && !stop) {
        if (!s_rebuffering) {
          s_rebuffering = true;
          s_rebuffer_started = xTaskGetTickCount();
          ++s_underrun_count;
          ESP_LOGW(kTag, "turn %u playback underrun; rebuffering", static_cast<unsigned>(s_playback_turn));
        }
        xSemaphoreGive(s_ring_mutex);
        break;
      }
      xSemaphoreGive(s_ring_mutex);
      if (bytes) {
        xSemaphoreGive(s_space_ready);
        size_t written = 0;
        if (i2s_write(kPort, block, bytes, &written, pdMS_TO_TICKS(1000)) != ESP_OK || written != bytes) {
          ESP_LOGW(kTag, "I2S playback write failed");
          xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
          s_finish_requested = true;
          stop = true;
          xSemaphoreGive(s_ring_mutex);
        }
      }
      if (!stop) continue;
      // Let the legacy driver's queued DMA frames reach the speaker before mute.
      vTaskDelay(pdMS_TO_TICKS(120));
      i2s_zero_dma_buffer(kPort);
      gpio_set_level(static_cast<gpio_num_t>(voice_config::kAmpEnableGpio), 0);
       xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
       s_playing = false;
       xSemaphoreGive(s_playback_done);
       xSemaphoreGive(s_ring_mutex);
      ESP_LOGI(kTag, "turn %u playback complete; peak=%u underruns=%u",
               static_cast<unsigned>(s_playback_turn), static_cast<unsigned>(s_peak_ring_bytes),
               static_cast<unsigned>(s_underrun_count));
       break;
    }
  }
}
}  // namespace

esp_err_t audio_init(void) {
  gpio_config_t amp = {}; amp.pin_bit_mask = 1ULL << voice_config::kAmpEnableGpio; amp.mode = GPIO_MODE_OUTPUT;
  ESP_ERROR_CHECK(gpio_config(&amp)); gpio_set_level(static_cast<gpio_num_t>(voice_config::kAmpEnableGpio), 0);
  i2c_config_t i2c = {}; i2c.mode = I2C_MODE_MASTER; i2c.sda_io_num = static_cast<gpio_num_t>(voice_config::kI2cSdaGpio);
  i2c.scl_io_num = static_cast<gpio_num_t>(voice_config::kI2cSclGpio); i2c.sda_pullup_en = GPIO_PULLUP_ENABLE;
  i2c.scl_pullup_en = GPIO_PULLUP_ENABLE; i2c.master.clk_speed = 400000;
  ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c)); ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, i2c.mode, 0, 0, 0));
  i2s_config_t config = {}; config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
  config.sample_rate = voice_config::kCaptureRateHz; config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1; config.dma_buf_count = 6; config.dma_buf_len = voice_config::kCaptureBlockSamples;
  config.use_apll = false; config.tx_desc_auto_clear = true;
  ESP_ERROR_CHECK(i2s_driver_install(kPort, &config, 0, nullptr));
  i2s_pin_config_t pins = {}; pins.mck_io_num = voice_config::kI2sMclkGpio; pins.bck_io_num = voice_config::kI2sBclkGpio;
  pins.ws_io_num = voice_config::kI2sWsGpio; pins.data_out_num = voice_config::kI2sTxGpio; pins.data_in_num = voice_config::kI2sRxGpio;
  ESP_ERROR_CHECK(i2s_set_pin(kPort, &pins));
  s_pcm_ring = static_cast<uint8_t*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  s_ring_mutex = xSemaphoreCreateMutex(); s_data_ready = xSemaphoreCreateCounting(16, 0);
  s_space_ready = xSemaphoreCreateCounting(16, 0); s_playback_done = xSemaphoreCreateBinary();
  if (!s_pcm_ring || !s_ring_mutex || !s_data_ready || !s_space_ready || !s_playback_done) return ESP_ERR_NO_MEM;
  xTaskCreate(playback_task, "luna_audio", 4096, nullptr, 7, nullptr);
  ESP_LOGI(kTag, "codec ready; live PCM capture enabled");
  return ESP_OK;
}

esp_err_t audio_capture_while_pressed(Recording* recording, volatile bool* cancelled,
                                      CaptureAudioCallback callback, void* callback_context) {
  if (!recording) return ESP_ERR_INVALID_STATE;
  *recording = {0, false, false}; codec_enable_mic(); set_rate(voice_config::kCaptureRateHz);
  size_t used = 0; int64_t energy = 0; int16_t block[voice_config::kCaptureBlockSamples];
  while (!*cancelled && gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) {
    size_t received = 0;
    esp_err_t err = i2s_read(kPort, block, sizeof(block), &received, pdMS_TO_TICKS(150));
    if (err != ESP_OK) return err;
    if (used + received > static_cast<size_t>(voice_config::kCaptureRateHz) *
                              voice_config::kMaxRecordSeconds * sizeof(int16_t)) {
      recording->truncated = true;
      break;
    }
    used += received;
    if (callback && !callback(reinterpret_cast<const uint8_t*>(block), received, callback_context)) {
      callback = nullptr;
    }
    for (size_t i = 0; i < received / sizeof(int16_t); ++i) energy += abs(block[i]);
  }
  recording->data_bytes = used;
  const size_t samples = used / sizeof(int16_t); recording->has_speech = samples && energy / static_cast<int64_t>(samples) > 80;
  return samples >= static_cast<size_t>(voice_config::kCaptureRateHz * voice_config::kMinRecordMilliseconds / 1000) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t audio_playback_begin(uint32_t turn_id) {
  xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
  if (s_playing) { xSemaphoreGive(s_ring_mutex); return ESP_ERR_INVALID_STATE; }
  while (xSemaphoreTake(s_playback_done, 0) == pdPASS) {}
  s_ring_read = 0; s_ring_write = 0; s_ring_used = 0; s_started = false; s_finish_requested = false;
  s_rebuffering = false; s_peak_ring_bytes = 0; s_underrun_count = 0;
  s_has_pcm_tail = false; s_playback_turn = turn_id; s_playing = true;
  xSemaphoreGive(s_ring_mutex);
  set_rate(voice_config::kTtsRateHz); codec_enable_speaker();
  return ESP_OK;
}
bool audio_playback_write(const uint8_t* data, size_t len, uint32_t turn_id, uint32_t timeout_ms) {
  if (!data) return false;
  if (s_has_pcm_tail && len) {
    const uint8_t sample[] = {s_pcm_tail, data[0]};
    if (!enqueue_pcm(sample, sizeof(sample), turn_id, timeout_ms)) return false;
    s_has_pcm_tail = false;
    ++data;
    --len;
  }
  if (len & 1U) {
    s_pcm_tail = data[len - 1];
    s_has_pcm_tail = true;
    --len;
  }
  if (!len) return true;
  return enqueue_pcm(data, len, turn_id, timeout_ms);
}
bool audio_playback_finish(uint32_t turn_id) {
  // Raw PCM is 16-bit; an unpaired network byte cannot be played safely.
  xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
  if (!s_playing || !playback_turn_matches(s_playback_turn, turn_id)) { xSemaphoreGive(s_ring_mutex); return false; }
  s_has_pcm_tail = false;
  s_finish_requested = true;
  xSemaphoreGive(s_ring_mutex);
  xSemaphoreGive(s_data_ready);
  if (xSemaphoreTake(s_playback_done, pdMS_TO_TICKS(voice_config::kPlaybackDrainTimeoutMs)) != pdPASS) {
    ESP_LOGW(kTag, "turn %u playback drain timed out", static_cast<unsigned>(turn_id));
    return false;
  }
  return !audio_playback_is_active();
}
void audio_playback_cancel(void) {
  xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
  s_has_pcm_tail = false; s_ring_read = 0; s_ring_write = 0; s_ring_used = 0;
  s_finish_requested = true;
  xSemaphoreGive(s_ring_mutex);
  i2s_zero_dma_buffer(kPort); gpio_set_level(static_cast<gpio_num_t>(voice_config::kAmpEnableGpio), 0);
  xSemaphoreGive(s_data_ready);
}
bool audio_playback_is_active(void) {
  xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
  const bool active = s_playing;
  xSemaphoreGive(s_ring_mutex);
  return active;
}
bool audio_playback_wait_idle(uint32_t timeout_ms) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (audio_playback_is_active() && static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0) vTaskDelay(pdMS_TO_TICKS(10));
  return !audio_playback_is_active();
}
void audio_play_tone(uint16_t frequency_hz, uint16_t duration_ms, uint8_t volume_percent) {
  if (audio_playback_begin(kTonePlaybackTurn) != ESP_OK) {
    ESP_LOGW(kTag, "tone skipped while playback is active");
    return;
  }
  ESP_LOGI(kTag, "tone %u Hz for %u ms", static_cast<unsigned>(frequency_hz), static_cast<unsigned>(duration_ms));
  const int samples = voice_config::kTtsRateHz * duration_ms / 1000; int16_t tone[240];
  const int envelope_samples = (voice_config::kTtsRateHz * 3) / 1000;
  for (int base = 0; base < samples; base += 240) { const int n = (samples - base) < 240 ? samples - base : 240;
    for (int i = 0; i < n; ++i) {
      const int sample = base + i;
      const int fade = sample < envelope_samples ? sample : samples - 1 - sample;
      const float envelope = fade < envelope_samples ? static_cast<float>(fade) / envelope_samples : 1.0F;
      tone[i] = static_cast<int16_t>(sin(2.0 * M_PI * frequency_hz * sample / voice_config::kTtsRateHz) *
                                     32767 * volume_percent * envelope / 100);
    }
    if (!audio_playback_write(reinterpret_cast<const uint8_t*>(tone), n * sizeof(int16_t), kTonePlaybackTurn, 1000)) {
      audio_playback_cancel();
      return;
    }
  }
  if (!audio_playback_finish(kTonePlaybackTurn)) audio_playback_cancel();
}
