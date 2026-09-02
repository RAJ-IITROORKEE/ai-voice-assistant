#include "relay_client.h"

#include "audio.h"
#include "relay_protocol.h"
#include "relay_uplink_policy.h"
#include "voice_config.h"

#if __has_include("../relay_config.h")
#include "../relay_config.h"
#else
#error "Missing main/relay_config.h. Run: dotnet run --project relay/tools/LunaRelay.Setup -- ."
#endif

#include <atomic>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_transport_ws.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"

// Local development embeds its generated CA. Public Cloud Run uses the IDF CA bundle.
#if !defined(LUNA_RELAY_USE_PUBLIC_CA)
extern const uint8_t relay_ca_start[] asm("_binary_relay_ca_pem_start");
#endif

namespace {
constexpr char kTag[] = "relay";
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kWifiIpBit = BIT1;
#define LUNA_SET_IP4(address, octets) IP4_ADDR(address, octets)
constexpr size_t kRxMessageBytes = 2048;

static_assert(relay_uplink_backlog_supported(voice_config::kRelayUplinkQueueDepth,
                                              voice_config::kRelayUplinkBacklogMilliseconds,
                                              voice_config::kRelayUplinkFrameMilliseconds),
              "Relay uplink queue must cover the configured Wi-Fi stall budget");

static_assert(voice_config::kRelayUplinkFrameBytes % 2 == 0,
              "Uplink frames must be whole PCM16 samples");
static_assert(voice_config::kRelayDownlinkFrameBytes % 2 == 0,
              "Downlink frames must be whole PCM16 samples");

enum class OutputType : uint8_t { kStart, kPcm, kFinish, kFailed };

struct UplinkItem {
  uint32_t turn_id;
  uint32_t sequence;
  uint16_t bytes;
  uint8_t pcm[voice_config::kRelayUplinkFrameBytes];
};

struct OutputItem {
  OutputType type;
  uint32_t turn_id;
  uint32_t sequence;
  uint16_t bytes;
  uint8_t pcm[voice_config::kRelayDownlinkFrameBytes];
};

struct ReceiveState {
  uint8_t bytes[kRxMessageBytes];
  size_t used;
  uint8_t opcode;
  bool active;
  bool drop;
};

EventGroupHandle_t s_events = nullptr;
QueueHandle_t s_uplink_queue = nullptr;
QueueHandle_t s_output_queue = nullptr;
SemaphoreHandle_t s_tx_mutex = nullptr;
SemaphoreHandle_t s_completion_mutex = nullptr;
SemaphoreHandle_t s_turn_done = nullptr;
esp_websocket_client_handle_t s_client = nullptr;
ReceiveState s_receive = {};
std::atomic<uint32_t> s_active_turn{0};
std::atomic<uint32_t> s_uplink_sequence{0};
std::atomic<uint32_t> s_pending_uplink{0};
std::atomic<uint32_t> s_uplink_peak{0};
std::atomic<bool> s_input_failed{false};
uint32_t s_completed_turn = 0;
bool s_completed_success = false;

bool send_text(const char* text) {
  if (!text || !s_client || !esp_websocket_client_is_connected(s_client)) return false;
  xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
  const int length = static_cast<int>(strlen(text));
  const int sent = esp_websocket_client_send_text(s_client, text, length, pdMS_TO_TICKS(2000));
  xSemaphoreGive(s_tx_mutex);
  return sent == length;
}

bool send_binary(const uint8_t* bytes, size_t length) {
  if (!bytes || !s_client || !esp_websocket_client_is_connected(s_client) ||
      length > static_cast<size_t>(INT32_MAX)) {
    return false;
  }
  xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
  const int sent = esp_websocket_client_send_bin(
      s_client, reinterpret_cast<const char*>(bytes), static_cast<int>(length), pdMS_TO_TICKS(3000));
  xSemaphoreGive(s_tx_mutex);
  return sent == static_cast<int>(length);
}

void signal_turn_done(uint32_t turn_id, bool success) {
  xSemaphoreTake(s_completion_mutex, portMAX_DELAY);
  s_completed_turn = turn_id;
  s_completed_success = success;
  xSemaphoreGive(s_completion_mutex);
  xSemaphoreGive(s_turn_done);
}

void fail_turn(uint32_t turn_id) {
  if (!turn_id) return;
  if (s_active_turn.load() == turn_id) s_active_turn.store(0);
  s_input_failed.store(true);
  audio_playback_cancel();
  signal_turn_done(turn_id, false);
}

bool queue_output(const OutputItem& item) {
  if (xQueueSend(s_output_queue, &item, pdMS_TO_TICKS(100)) == pdPASS) return true;
  ESP_LOGW(kTag, "downlink queue overflow for turn %u", static_cast<unsigned>(item.turn_id));
  fail_turn(item.turn_id);
  return false;
}

void uplink_task(void*) {
  UplinkItem item = {};
  uint8_t encoded[kRelayAudioHeaderBytes + voice_config::kRelayUplinkFrameBytes];
  while (true) {
    if (xQueueReceive(s_uplink_queue, &item, portMAX_DELAY) != pdPASS) continue;
    bool sent = false;
    if (s_active_turn.load() == item.turn_id && !s_input_failed.load()) {
      size_t encoded_bytes = 0;
      if (relay_encode_audio_frame(encoded, sizeof(encoded), RelayAudioKind::kMicrophonePcm,
                                   item.turn_id, item.sequence, item.pcm, item.bytes,
                                   &encoded_bytes)) {
        sent = send_binary(encoded, encoded_bytes);
      }
    }
    s_pending_uplink.fetch_sub(1);
    if (!sent && s_active_turn.load() == item.turn_id) {
      if (!s_input_failed.exchange(true)) {
        ESP_LOGE(kTag, "microphone transport failed for turn %u", static_cast<unsigned>(item.turn_id));
      }
    }
  }
}

void output_task(void*) {
  OutputItem item = {};
  uint32_t output_turn = 0;
  uint32_t expected_sequence = 0;
  bool playback_started = false;

  while (true) {
    if (xQueueReceive(s_output_queue, &item, portMAX_DELAY) != pdPASS) continue;
    if (item.type == OutputType::kStart) {
      if (output_turn && output_turn != item.turn_id) {
        audio_playback_cancel();
        audio_playback_wait_idle(1500);
      }
      output_turn = item.turn_id;
      expected_sequence = 0;
      playback_started = false;
      continue;
    }
    if (item.turn_id != output_turn) continue;

    if (item.type == OutputType::kPcm) {
      if (item.sequence != expected_sequence++) {
        ESP_LOGW(kTag, "speaker sequence gap on turn %u", static_cast<unsigned>(item.turn_id));
        audio_playback_cancel();
        signal_turn_done(item.turn_id, false);
        output_turn = 0;
        continue;
      }
      if (!playback_started) {
        if (audio_playback_begin(item.turn_id) != ESP_OK) {
          signal_turn_done(item.turn_id, false);
          output_turn = 0;
          continue;
        }
        playback_started = true;
      }
      if (!audio_playback_write(item.pcm, item.bytes, item.turn_id,
                                voice_config::kTtsWriteTimeoutMs)) {
        audio_playback_cancel();
        signal_turn_done(item.turn_id, false);
        output_turn = 0;
      }
      continue;
    }

    if (item.type == OutputType::kFinish) {
      const bool success = playback_started && audio_playback_finish(item.turn_id);
      if (s_active_turn.load() == item.turn_id) s_active_turn.store(0);
      signal_turn_done(item.turn_id, success);
      output_turn = 0;
      continue;
    }

    audio_playback_cancel();
    audio_playback_wait_idle(1500);
    if (s_active_turn.load() == item.turn_id) s_active_turn.store(0);
    signal_turn_done(item.turn_id, false);
    output_turn = 0;
  }
}

void handle_control(const uint8_t* bytes, size_t length) {
  cJSON* json = cJSON_ParseWithLength(reinterpret_cast<const char*>(bytes), length);
  cJSON* type = json ? cJSON_GetObjectItemCaseSensitive(json, "type") : nullptr;
  cJSON* turn = json ? cJSON_GetObjectItemCaseSensitive(json, "turn_id") : nullptr;
  if (!cJSON_IsString(type) || !type->valuestring) {
    if (json) cJSON_Delete(json);
    return;
  }
  const uint32_t turn_id = cJSON_IsNumber(turn) ? static_cast<uint32_t>(turn->valuedouble) : 0;
  if (!strcmp(type->valuestring, "ready")) {
    ESP_LOGI(kTag, "relay application ready");
  } else if (!strcmp(type->valuestring, "transcript.final")) {
    ESP_LOGI(kTag, "turn %u transcript finalized", static_cast<unsigned>(turn_id));
  } else if (!strcmp(type->valuestring, "tts.start")) {
    queue_output({OutputType::kStart, turn_id, 0, 0, {}});
  } else if (!strcmp(type->valuestring, "tts.end")) {
    queue_output({OutputType::kFinish, turn_id, 0, 0, {}});
  } else if (!strcmp(type->valuestring, "turn.error") ||
             !strcmp(type->valuestring, "turn.cancelled")) {
    queue_output({OutputType::kFailed, turn_id, 0, 0, {}});
  }
  cJSON_Delete(json);
}

void handle_binary(const uint8_t* bytes, size_t length) {
  RelayAudioFrame frame = {};
  if (!relay_decode_audio_frame(bytes, length, &frame) ||
      frame.kind != RelayAudioKind::kSpeakerPcm ||
      frame.pcm_bytes > voice_config::kRelayDownlinkFrameBytes ||
      s_active_turn.load() != frame.turn_id) {
    return;
  }
  OutputItem item = {OutputType::kPcm, frame.turn_id, frame.sequence,
                     static_cast<uint16_t>(frame.pcm_bytes), {}};
  memcpy(item.pcm, frame.pcm, frame.pcm_bytes);
  queue_output(item);
}

void reset_receive() { s_receive = {}; }

void receive_message(const esp_websocket_event_data_t* event) {
  const uint8_t opcode = event->op_code;
  if (opcode != WS_TRANSPORT_OPCODES_TEXT && opcode != WS_TRANSPORT_OPCODES_BINARY &&
      opcode != WS_TRANSPORT_OPCODES_CONT) {
    return;
  }
  if ((opcode == WS_TRANSPORT_OPCODES_TEXT || opcode == WS_TRANSPORT_OPCODES_BINARY) &&
      event->payload_offset == 0) {
    reset_receive();
    s_receive.active = true;
    s_receive.opcode = opcode;
  } else if (opcode == WS_TRANSPORT_OPCODES_CONT && !s_receive.active) {
    return;
  }
  if (!s_receive.active || event->data_len < 0) {
    reset_receive();
    return;
  }
  const size_t chunk = static_cast<size_t>(event->data_len);
  if (chunk > sizeof(s_receive.bytes) - s_receive.used) {
    s_receive.drop = true;
  } else if (!s_receive.drop && chunk) {
    memcpy(s_receive.bytes + s_receive.used, event->data_ptr, chunk);
    s_receive.used += chunk;
  }
  const bool frame_complete = event->payload_offset + event->data_len >= event->payload_len;
  if (!frame_complete || !event->fin) return;
  if (!s_receive.drop) {
    if (s_receive.opcode == WS_TRANSPORT_OPCODES_TEXT) {
      handle_control(s_receive.bytes, s_receive.used);
    } else {
      handle_binary(s_receive.bytes, s_receive.used);
    }
  } else {
    ESP_LOGW(kTag, "dropping oversized WebSocket message");
  }
  reset_receive();
}

void websocket_event(void*, esp_event_base_t, int32_t event_id, void* event_data) {
  auto* event = static_cast<esp_websocket_event_data_t*>(event_data);
  switch (static_cast<esp_websocket_event_id_t>(event_id)) {
    case WEBSOCKET_EVENT_CONNECTED:
      reset_receive();
      xEventGroupSetBits(s_events, kConnectedBit);
      ESP_LOGI(kTag, "WSS connected");
      break;
    case WEBSOCKET_EVENT_DATA:
      if (event) receive_message(event);
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      reset_receive();
      xEventGroupClearBits(s_events, kConnectedBit);
      ESP_LOGW(kTag, "WSS disconnected; reconnect pending");
      break;
    case WEBSOCKET_EVENT_ERROR:
      ESP_LOGW(kTag, "WSS transport error");
      break;
    default:
      break;
  }
}

void wifi_event(void*, esp_event_base_t base, int32_t event_id, void*) {
  if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(s_events, kWifiIpBit);
    ESP_LOGW(kTag, "Wi-Fi disconnected; reconnecting");
    esp_wifi_connect();
  }
#if !defined(LUNA_RELAY_USE_PUBLIC_CA) && defined(LUNA_WIFI_STATIC_IP) && \
    defined(LUNA_WIFI_STATIC_GATEWAY) && defined(LUNA_WIFI_STATIC_NETMASK)
  if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    xEventGroupSetBits(s_events, kWifiIpBit);
    ESP_LOGI(kTag, "Wi-Fi static IPv4 active");
  }
#endif
}

void ip_event(void*, esp_event_base_t base, int32_t event_id, void*) {
  if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_events, kWifiIpBit);
    ESP_LOGI(kTag, "Wi-Fi IPv4 lease acquired");
  }
}

bool wait_for_pending_uplink(uint32_t timeout_ms) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (s_pending_uplink.load() && static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return s_pending_uplink.load() == 0;
}
}  // namespace

esp_err_t relay_init(void) {
  s_events = xEventGroupCreate();
  s_uplink_queue = xQueueCreateWithCaps(voice_config::kRelayUplinkQueueDepth, sizeof(UplinkItem),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_output_queue = xQueueCreateWithCaps(voice_config::kRelayDownlinkQueueDepth, sizeof(OutputItem),
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_tx_mutex = xSemaphoreCreateMutex();
  s_completion_mutex = xSemaphoreCreateMutex();
  s_turn_done = xSemaphoreCreateBinary();
  if (!s_events || !s_uplink_queue || !s_output_queue || !s_tx_mutex || !s_completion_mutex ||
      !s_turn_done) {
    return ESP_ERR_NO_MEM;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_t* wifi_netif = esp_netif_create_default_wifi_sta();
  if (!wifi_netif) return ESP_ERR_NO_MEM;
  wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event, nullptr));
  wifi_config_t wifi = {};
  snprintf(reinterpret_cast<char*>(wifi.sta.ssid), sizeof(wifi.sta.ssid), "%s", WIFI_SSID);
  snprintf(reinterpret_cast<char*>(wifi.sta.password), sizeof(wifi.sta.password), "%s", WIFI_PASSWORD);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
  ESP_ERROR_CHECK(esp_wifi_start());

#if !defined(LUNA_RELAY_USE_PUBLIC_CA) && defined(LUNA_WIFI_STATIC_IP) && \
    defined(LUNA_WIFI_STATIC_GATEWAY) && defined(LUNA_WIFI_STATIC_NETMASK)
  const esp_err_t dhcp_stop = esp_netif_dhcpc_stop(wifi_netif);
  if (dhcp_stop != ESP_OK && dhcp_stop != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return dhcp_stop;
  esp_netif_ip_info_t static_ip = {};
  LUNA_SET_IP4(&static_ip.ip, LUNA_WIFI_STATIC_IP);
  LUNA_SET_IP4(&static_ip.gw, LUNA_WIFI_STATIC_GATEWAY);
  LUNA_SET_IP4(&static_ip.netmask, LUNA_WIFI_STATIC_NETMASK);
  ESP_ERROR_CHECK(esp_netif_set_ip_info(wifi_netif, &static_ip));
#else
  // Reset any DHCP state retained across a network change before associating.
  const esp_err_t dhcp_stop = esp_netif_dhcpc_stop(wifi_netif);
  if (dhcp_stop != ESP_OK && dhcp_stop != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) return dhcp_stop;
  ESP_ERROR_CHECK(esp_netif_dhcpc_start(wifi_netif));
#endif
  ESP_ERROR_CHECK(esp_wifi_connect());

  if (!(xEventGroupWaitBits(s_events, kWifiIpBit, pdFALSE, pdTRUE,
                            pdMS_TO_TICKS(15000)) & kWifiIpBit)) {
    return ESP_ERR_TIMEOUT;
  }
  ESP_LOGI(kTag, "Wi-Fi ready; starting relay connection");

  esp_websocket_client_config_t config = {};
  config.uri = LUNA_RELAY_URI;
#if defined(LUNA_RELAY_USE_PUBLIC_CA)
  config.crt_bundle_attach = esp_crt_bundle_attach;
#else
  config.cert_pem = reinterpret_cast<const char*>(relay_ca_start);
#endif
  config.buffer_size = 2048;
  config.network_timeout_ms = 10000;
  config.disable_auto_reconnect = false;
  config.enable_close_reconnect = true;
  config.reconnect_timeout_ms = 3000;
  config.ping_interval_sec = 15;
  config.pingpong_timeout_sec = 45;
  config.keep_alive_enable = true;
  config.keep_alive_idle = 30;
  config.keep_alive_interval = 10;
  config.keep_alive_count = 3;
  s_client = esp_websocket_client_init(&config);
  if (!s_client) return ESP_ERR_NO_MEM;
  uint8_t mac[6] = {};
  ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
  char device_id[sizeof("esp32-000000000000")] = {};
  snprintf(device_id, sizeof(device_id), "esp32-%02x%02x%02x%02x%02x%02x", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  ESP_ERROR_CHECK(esp_websocket_client_append_header(s_client, "X-Device-Token",
                                                        LUNA_RELAY_DEVICE_TOKEN));
  ESP_ERROR_CHECK(esp_websocket_client_append_header(s_client, "X-Device-Id", device_id));
  ESP_ERROR_CHECK(esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, websocket_event, nullptr));
  ESP_ERROR_CHECK(esp_websocket_client_start(s_client));
  xTaskCreate(uplink_task, "luna_uplink", 4096, nullptr, 6, nullptr);
  xTaskCreate(output_task, "luna_downlink", 4096, nullptr, 6, nullptr);
  return ESP_OK;
}

bool relay_wait_connected(uint32_t timeout_ms) {
  if (!s_events) return false;
  return (xEventGroupWaitBits(s_events, kConnectedBit, pdFALSE, pdTRUE,
                              pdMS_TO_TICKS(timeout_ms)) & kConnectedBit) != 0;
}

bool relay_is_connected(void) {
  return s_client && esp_websocket_client_is_connected(s_client);
}

bool relay_start_turn(uint32_t turn_id) {
  if (!turn_id || !relay_is_connected() || !wait_for_pending_uplink(4000)) return false;
  while (xSemaphoreTake(s_turn_done, 0) == pdPASS) {}
  s_active_turn.store(turn_id);
  s_uplink_sequence.store(0);
  s_uplink_peak.store(0);
  s_input_failed.store(false);
  char message[128] = {};
  snprintf(message, sizeof(message),
           "{\"type\":\"turn.start\",\"turn_id\":%u,\"sample_rate\":16000,\"format\":\"pcm_s16le\"}",
           static_cast<unsigned>(turn_id));
  if (send_text(message)) return true;
  s_active_turn.store(0);
  return false;
}

bool relay_send_pcm(uint32_t turn_id, const uint8_t* pcm, size_t pcm_bytes) {
  if (!pcm || !pcm_bytes || pcm_bytes > voice_config::kRelayUplinkFrameBytes ||
      (pcm_bytes & 1U) || s_active_turn.load() != turn_id || s_input_failed.load()) {
    return false;
  }
  UplinkItem item = {turn_id, s_uplink_sequence.fetch_add(1), static_cast<uint16_t>(pcm_bytes), {}};
  memcpy(item.pcm, pcm, pcm_bytes);
  s_pending_uplink.fetch_add(1);
  if (xQueueSend(s_uplink_queue, &item, 0) != pdPASS) {
    s_pending_uplink.fetch_sub(1);
    if (!s_input_failed.exchange(true)) {
      ESP_LOGE(kTag, "microphone backlog exhausted for turn %u after %u queued frames",
               static_cast<unsigned>(turn_id),
               static_cast<unsigned>(uxQueueMessagesWaiting(s_uplink_queue)));
    }
    return false;
  }
  const uint32_t queued = uxQueueMessagesWaiting(s_uplink_queue);
  uint32_t peak = s_uplink_peak.load();
  while (queued > peak && !s_uplink_peak.compare_exchange_weak(peak, queued)) {}
  return true;
}

bool relay_input_failed(uint32_t turn_id) {
  return s_active_turn.load() != turn_id || s_input_failed.load();
}

bool relay_commit_turn(uint32_t turn_id) {
  if (s_active_turn.load() != turn_id || s_input_failed.load()) {
    return false;
  }
  if (!wait_for_pending_uplink(voice_config::kRelayUplinkDrainTimeoutMs)) {
    s_input_failed.store(true);
    ESP_LOGE(kTag, "turn %u microphone drain timed out; peak backlog=%u frames",
             static_cast<unsigned>(turn_id), static_cast<unsigned>(s_uplink_peak.load()));
    return false;
  }
  ESP_LOGI(kTag, "turn %u microphone upload drained; peak backlog=%u frames",
           static_cast<unsigned>(turn_id), static_cast<unsigned>(s_uplink_peak.load()));
  char message[64] = {};
  snprintf(message, sizeof(message), "{\"type\":\"turn.commit\",\"turn_id\":%u}",
           static_cast<unsigned>(turn_id));
  return send_text(message);
}

void relay_cancel_turn(uint32_t turn_id) {
  if (!turn_id || s_active_turn.load() != turn_id) return;
  s_active_turn.store(0);
  char message[64] = {};
  snprintf(message, sizeof(message), "{\"type\":\"turn.cancel\",\"turn_id\":%u}",
           static_cast<unsigned>(turn_id));
  send_text(message);
  audio_playback_cancel();
  signal_turn_done(turn_id, false);
}

bool relay_wait_turn_finished(uint32_t turn_id, uint32_t timeout_ms) {
  const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
  while (static_cast<int32_t>(deadline - xTaskGetTickCount()) > 0) {
    if (xSemaphoreTake(s_turn_done, deadline - xTaskGetTickCount()) != pdPASS) break;
    xSemaphoreTake(s_completion_mutex, portMAX_DELAY);
    const bool match = s_completed_turn == turn_id;
    const bool success = s_completed_success;
    xSemaphoreGive(s_completion_mutex);
    if (match) return success;
  }
  return false;
}
