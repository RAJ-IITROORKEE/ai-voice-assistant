#include "audio.h"
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
uint8_t* s_wav = nullptr;
size_t s_capacity = 0;
uint8_t* s_pcm_ring = nullptr;
constexpr size_t kRingBytes = static_cast<size_t>(voice_config::kAudioRingSamples) * sizeof(int16_t);
constexpr size_t kPrebufferBytes = static_cast<size_t>(voice_config::kAudioPrebufferSamples) * sizeof(int16_t);
constexpr size_t kPlaybackBlockBytes = static_cast<size_t>(voice_config::kPlaybackBlockSamples) * sizeof(int16_t);
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

void write_wav_header(uint8_t* wav, size_t data_bytes) {
  const uint32_t rate = voice_config::kCaptureRateHz;
  const uint32_t riff_size = static_cast<uint32_t>(36 + data_bytes);
  const uint32_t byte_rate = rate * 2;
  const uint16_t block_align = 2;
  const uint16_t bits = 16;
  memcpy(wav, "RIFF", 4); memcpy(wav + 4, &riff_size, 4); memcpy(wav + 8, "WAVEfmt ", 8);
  const uint32_t fmt_size = 16; const uint16_t pcm = 1; const uint16_t channels = 1;
  memcpy(wav + 16, &fmt_size, 4); memcpy(wav + 20, &pcm, 2); memcpy(wav + 22, &channels, 2);
  memcpy(wav + 24, &rate, 4); memcpy(wav + 28, &byte_rate, 4); memcpy(wav + 32, &block_align, 2);
  memcpy(wav + 34, &bits, 2); memcpy(wav + 36, "data", 4);
  const uint32_t size = static_cast<uint32_t>(data_bytes); memcpy(wav + 40, &size, 4);
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
}

void ring_copy_out(uint8_t* data, size_t bytes) {
  const size_t first = (bytes < kRingBytes - s_ring_read) ? bytes : kRingBytes - s_ring_read;
  memcpy(data, s_pcm_ring + s_ring_read, first);
  if (bytes > first) memcpy(data + first, s_pcm_ring, bytes - first);
  s_ring_read = (s_ring_read + bytes) % kRingBytes;
  s_ring_used -= bytes;
}

bool enqueue_pcm(const uint8_t* data, size_t bytes, uint32_t timeout_ms) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (bytes) {
    if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(20)) != pdPASS) continue;
    if (!s_playing || s_finish_requested) { xSemaphoreGive(s_ring_mutex); return false; }
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
      if (!s_started && s_ring_used < kPrebufferBytes && !s_finish_requested) {
        xSemaphoreGive(s_ring_mutex);
        break;
      }
      s_started = true;
      bytes = (s_ring_used < sizeof(block)) ? s_ring_used : sizeof(block);
      if (bytes) ring_copy_out(block, bytes);
      stop = s_finish_requested && s_ring_used == 0;
      if (!bytes && !stop) {
        ESP_LOGW(kTag, "playback underrun; waiting for TTS data");
        xSemaphoreGive(s_ring_mutex);
        break;
      }
      if (stop) s_playing = false;
      xSemaphoreGive(s_ring_mutex);
      if (bytes) {
        xSemaphoreGive(s_space_ready);
        size_t written = 0;
        if (i2s_write(kPort, block, bytes, &written, pdMS_TO_TICKS(1000)) != ESP_OK || written != bytes) {
          ESP_LOGW(kTag, "I2S playback write failed");
          xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
          s_playing = false;
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
      xSemaphoreGive(s_playback_done);
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
  s_capacity = 44 + static_cast<size_t>(voice_config::kCaptureRateHz) * voice_config::kMaxRecordSeconds * sizeof(int16_t);
  s_wav = static_cast<uint8_t*>(heap_caps_malloc(s_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  s_pcm_ring = static_cast<uint8_t*>(heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  s_ring_mutex = xSemaphoreCreateMutex(); s_data_ready = xSemaphoreCreateCounting(16, 0);
  s_space_ready = xSemaphoreCreateCounting(16, 0); s_playback_done = xSemaphoreCreateBinary();
  if (!s_wav || !s_pcm_ring || !s_ring_mutex || !s_data_ready || !s_space_ready || !s_playback_done) return ESP_ERR_NO_MEM;
  xTaskCreate(playback_task, "luna_audio", 4096, nullptr, 7, nullptr);
  ESP_LOGI(kTag, "codec ready; capture capacity=%u", static_cast<unsigned>(s_capacity));
  return ESP_OK;
}

esp_err_t audio_capture_while_pressed(Recording* recording, volatile bool* cancelled) {
  if (!recording || !s_wav) return ESP_ERR_INVALID_STATE;
  *recording = {s_wav, s_capacity, 0, false, false}; codec_enable_mic(); set_rate(voice_config::kCaptureRateHz);
  size_t used = 0; int64_t energy = 0; int16_t block[voice_config::kCaptureBlockSamples];
  while (!*cancelled && gpio_get_level(static_cast<gpio_num_t>(voice_config::kButtonGpio)) == 0) {
    size_t received = 0;
    esp_err_t err = i2s_read(kPort, block, sizeof(block), &received, pdMS_TO_TICKS(150));
    if (err != ESP_OK) return err;
    if (used + received > s_capacity - 44) { recording->truncated = true; break; }
    memcpy(s_wav + 44 + used, block, received); used += received;
    for (size_t i = 0; i < received / sizeof(int16_t); ++i) energy += abs(block[i]);
  }
  recording->data_bytes = used; write_wav_header(s_wav, used);
  const size_t samples = used / sizeof(int16_t); recording->has_speech = samples && energy / static_cast<int64_t>(samples) > 80;
  return samples >= static_cast<size_t>(voice_config::kCaptureRateHz * voice_config::kMinRecordMilliseconds / 1000) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

void audio_reset_recording(void) {}

esp_err_t audio_playback_begin(uint32_t) {
  xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
  if (s_playing) { xSemaphoreGive(s_ring_mutex); return ESP_ERR_INVALID_STATE; }
  while (xSemaphoreTake(s_playback_done, 0) == pdPASS) {}
  s_ring_read = 0; s_ring_write = 0; s_ring_used = 0; s_started = false; s_finish_requested = false;
  s_has_pcm_tail = false; s_playing = true;
  xSemaphoreGive(s_ring_mutex);
  set_rate(voice_config::kTtsRateHz); codec_enable_speaker();
  return ESP_OK;
}
bool audio_playback_write(const uint8_t* data, size_t len, uint32_t, uint32_t timeout_ms) {
  if (!s_playing || !data) return false;
  if (s_has_pcm_tail && len) {
    const uint8_t sample[] = {s_pcm_tail, data[0]};
    if (!enqueue_pcm(sample, sizeof(sample), timeout_ms)) return false;
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
  return enqueue_pcm(data, len, timeout_ms);
}
void audio_playback_finish(uint32_t) {
  // Raw PCM is 16-bit; an unpaired network byte cannot be played safely.
  s_has_pcm_tail = false;
  xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
  if (!s_playing) { xSemaphoreGive(s_ring_mutex); return; }
  s_finish_requested = true;
  xSemaphoreGive(s_ring_mutex);
  xSemaphoreGive(s_data_ready);
  xSemaphoreTake(s_playback_done, pdMS_TO_TICKS(5000));
}
void audio_playback_cancel(void) {
  xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
  s_has_pcm_tail = false; s_ring_read = 0; s_ring_write = 0; s_ring_used = 0;
  s_finish_requested = true; s_playing = false;
  xSemaphoreGive(s_ring_mutex);
  i2s_zero_dma_buffer(kPort); gpio_set_level(static_cast<gpio_num_t>(voice_config::kAmpEnableGpio), 0);
  xSemaphoreGive(s_data_ready); xSemaphoreGive(s_playback_done);
}
bool audio_playback_is_active(void) { return s_playing; }
bool audio_playback_wait_idle(uint32_t timeout_ms) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (s_playing && static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0) vTaskDelay(pdMS_TO_TICKS(10));
  return !s_playing;
}
void audio_play_tone(uint16_t frequency_hz, uint16_t duration_ms, uint8_t volume_percent) {
  audio_playback_begin(0); const int samples = voice_config::kTtsRateHz * duration_ms / 1000; int16_t tone[240];
  for (int base = 0; base < samples; base += 240) { const int n = (samples - base) < 240 ? samples - base : 240;
    for (int i = 0; i < n; ++i) tone[i] = static_cast<int16_t>(sin(2.0 * M_PI * frequency_hz * (base + i) / voice_config::kTtsRateHz) * 32767 * volume_percent / 100);
    audio_playback_write(reinterpret_cast<const uint8_t*>(tone), n * sizeof(int16_t), 0, 1000); }
  audio_playback_finish(0);
}
