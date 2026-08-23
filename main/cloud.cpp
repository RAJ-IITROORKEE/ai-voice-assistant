#include "cloud.h"
#include "audio.h"
#include "multipart_form.h"
#include "sse_protocol.h"
#include "voice_config.h"
#include "secrets.h"

#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace {
constexpr char kTag[] = "cloud";
bool s_wifi = false;
void set_result(CloudResult* result, CloudError error, int status, const char* message) { if (result) { result->error = error; result->http_status = status; snprintf(result->message, sizeof(result->message), "%s", message ? message : ""); } }
CloudError classify(int status) { if (status == 401 || status == 403) return CloudError::kAuthentication; if (status == 429) return CloudError::kRateLimited; return status >= 500 ? CloudError::kService : CloudError::kProtocol; }
void json_escape(const char* source, char* target, size_t size) { size_t j = 0; for (size_t i = 0; source[i] && j + 2 < size; ++i) { const char c = source[i]; if (c == '"' || c == '\\') target[j++] = '\\'; if (c == '\n') { target[j++] = '\\'; target[j++] = 'n'; } else if (c != '\r') target[j++] = c; } target[j] = 0; }
bool contains_devanagari(const char* text) { for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); p[0] && p[1] && p[2]; ++p) if (p[0] == 0xE0 && p[1] >= 0xA4 && p[1] <= 0xA5) return true; return false; }
bool xml_escape(const char* source, char* target, size_t size) { size_t j = 0; for (size_t i = 0; source[i]; ++i) { const char* replacement = nullptr; switch (source[i]) { case '&': replacement = "&amp;"; break; case '<': replacement = "&lt;"; break; case '>': replacement = "&gt;"; break; case '\"': replacement = "&quot;"; break; case '\'': replacement = "&apos;"; break; default: break; } if (replacement) { size_t n = strlen(replacement); if (j + n >= size) return false; memcpy(target + j, replacement, n); j += n; } else if (j + 1 >= size) return false; else target[j++] = source[i]; } target[j] = 0; return true; }
esp_http_client_handle_t client_for(const char* url, http_event_handle_cb callback, void* user) { esp_http_client_config_t config = {}; config.url = url; config.timeout_ms = voice_config::kHttpTimeoutMs; config.crt_bundle_attach = esp_crt_bundle_attach; config.event_handler = callback; config.user_data = user; config.keep_alive_enable = true; return esp_http_client_init(&config); }
void wifi_event(void*, esp_event_base_t base, int32_t event, void* data) {
  if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* disconnected = static_cast<const wifi_event_sta_disconnected_t*>(data);
    s_wifi = false;
    ESP_LOGW(kTag, "Wi-Fi disconnected; reason=%u", disconnected ? disconnected->reason : 0);
    esp_wifi_connect();
  }
  if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) {
    s_wifi = true;
    ESP_LOGI(kTag, "Wi-Fi ready");
  }
}
}  // namespace

namespace {
bool write_all(esp_http_client_handle_t client, const void* data, size_t len) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  while (len) {
    const int written = esp_http_client_write(client, reinterpret_cast<const char*>(bytes), len);
    if (written <= 0) return false;
    bytes += written;
    len -= static_cast<size_t>(written);
  }
  return true;
}

bool write_all_cancelled(esp_http_client_handle_t client, const void* data, size_t len,
                         volatile bool* cancelled) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  while (len && !*cancelled) {
    const size_t block = len < 4096 ? len : 4096;
    if (!write_all(client, bytes, block)) return false;
    bytes += block;
    len -= block;
  }
  return len == 0 && !*cancelled;
}

bool parse_simple_transcript(const char* body, size_t body_len, char* transcript,
                             size_t transcript_size, int status, CloudResult* result) {
  cJSON* json = cJSON_ParseWithLength(body, body_len);
  cJSON* text = json ? cJSON_GetObjectItemCaseSensitive(json, "DisplayText") : nullptr;
  if (!cJSON_IsString(text) || !text->valuestring) {
    if (json) cJSON_Delete(json);
    set_result(result, CloudError::kProtocol, status, "speech response");
    return false;
  }
  snprintf(transcript, transcript_size, "%s", text->valuestring);
  cJSON_Delete(json);
  set_result(result, CloudError::kNone, status, "");
  return true;
}
}  // namespace

esp_err_t cloud_init_wifi(void) {
  ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta(); wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&config));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &wifi_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event, nullptr));
  wifi_config_t wifi = {}; snprintf(reinterpret_cast<char*>(wifi.sta.ssid), sizeof(wifi.sta.ssid), "%s", WIFI_SSID); snprintf(reinterpret_cast<char*>(wifi.sta.password), sizeof(wifi.sta.password), "%s", WIFI_PASSWORD);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi)); ESP_ERROR_CHECK(esp_wifi_start()); ESP_ERROR_CHECK(esp_wifi_connect());
  while (!s_wifi) vTaskDelay(pdMS_TO_TICKS(100));
  return ESP_OK;
}
bool cloud_wifi_connected(void) { wifi_ap_record_t ap; s_wifi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK; return s_wifi; }

struct ResponseBody { char* bytes; size_t capacity; size_t used; bool overflow; };
esp_err_t response_body_event(esp_http_client_event_t* event) {
  auto* body = static_cast<ResponseBody*>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_DATA || !body) return ESP_OK;
  const size_t available = body->capacity > body->used ? body->capacity - body->used - 1 : 0;
  const size_t copy = event->data_len < static_cast<int>(available) ? static_cast<size_t>(event->data_len) : available;
  if (copy) memcpy(body->bytes + body->used, event->data, copy);
  body->used += copy;
  body->bytes[body->used] = 0;
  body->overflow = copy != static_cast<size_t>(event->data_len);
  return body->overflow ? ESP_FAIL : ESP_OK;
}

bool cloud_transcribe_short(const Recording& recording, char* transcript, size_t transcript_size,
                            volatile bool* cancelled, CloudResult* result) {
  if (!recording.wav || !recording.data_bytes || *cancelled) { set_result(result, CloudError::kCancelled, 0, "cancelled"); return false; }
  char url[320]; snprintf(url, sizeof(url), "%s/stt/speech/recognition/conversation/cognitiveservices/v1?language=%s&format=simple", AZURE_SPEECH_ENDPOINT, SPEECH_LANGUAGE);
  char body[1024] = {};
  ResponseBody response = {body, sizeof(body), 0, false};
  esp_http_client_handle_t client = client_for(url, response_body_event, &response); if (!client) { set_result(result, CloudError::kMemory, 0, "client allocation"); return false; }
  esp_http_client_set_method(client, HTTP_METHOD_POST); esp_http_client_set_header(client, "Ocp-Apim-Subscription-Key", AZURE_SPEECH_KEY); esp_http_client_set_header(client, "Content-Type", "audio/wav; codecs=audio/pcm; samplerate=16000");
  esp_http_client_set_post_field(client, reinterpret_cast<const char*>(recording.wav), recording.data_bytes + 44);
  esp_err_t err = esp_http_client_perform(client); int status = esp_http_client_get_status_code(client); esp_http_client_cleanup(client);
  if (err != ESP_OK || status != 200 || response.overflow) { set_result(result, err == ESP_OK ? classify(status) : CloudError::kNetwork, status, "speech request failed"); return false; }
  return parse_simple_transcript(body, response.used, transcript, transcript_size, status, result);
}

bool parse_fast_transcript(const char* body, size_t body_len, char* transcript,
                           size_t transcript_size, int status, CloudResult* result) {
  cJSON* json = cJSON_ParseWithLength(body, body_len);
  cJSON* combined = json ? cJSON_GetObjectItemCaseSensitive(json, "combinedPhrases") : nullptr;
  size_t used = 0;
  cJSON* phrase = nullptr;
  cJSON_ArrayForEach(phrase, combined) {
    cJSON* text = cJSON_GetObjectItemCaseSensitive(phrase, "text");
    if (!cJSON_IsString(text) || !text->valuestring || !text->valuestring[0]) continue;
    const size_t length = strlen(text->valuestring);
    if (used && used + 1 < transcript_size) transcript[used++] = ' ';
    const size_t available = transcript_size > used ? transcript_size - used - 1 : 0;
    const size_t copy = length < available ? length : available;
    if (copy) memcpy(transcript + used, text->valuestring, copy);
    used += copy;
    transcript[used] = 0;
    if (copy != length) break;
  }
  if (json) cJSON_Delete(json);
  if (!used) {
    set_result(result, CloudError::kProtocol, status, "fast transcription response");
    return false;
  }
  set_result(result, CloudError::kNone, status, "");
  return true;
}

bool cloud_transcribe(const Recording& recording, char* transcript, size_t transcript_size,
                      volatile bool* cancelled, CloudResult* result) {
  if (!recording.wav || !recording.data_bytes || *cancelled) {
    set_result(result, CloudError::kCancelled, 0, "cancelled");
    return false;
  }

  char prefix[256] = {};
  char suffix[384] = {};
  const size_t prefix_bytes = build_fast_stt_prefix(prefix, sizeof(prefix));
  const size_t suffix_bytes = build_fast_stt_suffix(suffix, sizeof(suffix));
  size_t request_bytes = 0;
  const size_t wav_bytes = recording.data_bytes + 44;
  if (!prefix_bytes || !suffix_bytes ||
      !multipart_total_length(prefix_bytes, wav_bytes, suffix_bytes, &request_bytes)) {
    return cloud_transcribe_short(recording, transcript, transcript_size, cancelled, result);
  }

  char url[320];
  snprintf(url, sizeof(url), "%s/speechtotext/transcriptions:transcribe?api-version=2025-10-15",
           AZURE_SPEECH_ENDPOINT);
  esp_http_client_handle_t client = client_for(url, nullptr, nullptr);
  char* body = static_cast<char*>(heap_caps_malloc(voice_config::kFastSttResponseBytes,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!client || !body) {
    if (client) esp_http_client_cleanup(client);
    if (body) heap_caps_free(body);
    set_result(result, CloudError::kMemory, 0, "fast transcription allocation");
    return cloud_transcribe_short(recording, transcript, transcript_size, cancelled, result);
  }

  char content_type[96];
  snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", kFastSttBoundary);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Ocp-Apim-Subscription-Key", AZURE_SPEECH_KEY);
  esp_http_client_set_header(client, "Content-Type", content_type);
  esp_http_client_set_header(client, "Accept", "application/json");

  const int64_t started = esp_timer_get_time();
  bool ok = esp_http_client_open(client, static_cast<int>(request_bytes)) == ESP_OK;
  if (ok) ok = write_all_cancelled(client, prefix, prefix_bytes, cancelled);
  if (ok) ok = write_all_cancelled(client, recording.wav, wav_bytes, cancelled);
  if (ok) ok = write_all_cancelled(client, suffix, suffix_bytes, cancelled);
  if (ok) ok = esp_http_client_fetch_headers(client) >= 0;
  const int status = esp_http_client_get_status_code(client);
  const int64_t content_length = esp_http_client_get_content_length(client);
  if (ok) ok = fast_stt_response_fits(content_length, voice_config::kFastSttResponseBytes);
  int body_len = -1;
  if (ok) body_len = esp_http_client_read_response(client, body,
                                                    voice_config::kFastSttResponseBytes - 1);
  if (body_len >= 0) body[body_len] = 0;
  if (ok) ok = body_len >= 0 && esp_http_client_is_complete_data_received(client);
  esp_http_client_cleanup(client);

  const bool parsed = ok && status == 200 &&
                 parse_fast_transcript(body, static_cast<size_t>(body_len), transcript,
                                       transcript_size, status, result);
  heap_caps_free(body);
  ESP_LOGI(kTag, "Fast STT completed in %lld ms; status=%d",
           static_cast<long long>((esp_timer_get_time() - started) / 1000), status);
  if (*cancelled) {
    set_result(result, CloudError::kCancelled, status, "cancelled");
    return false;
  }
  if (parsed) return true;

  ESP_LOGW(kTag, "Fast STT unavailable; using short-audio fallback");
  return cloud_transcribe_short(recording, transcript, transcript_size, cancelled, result);
}

struct SseContext {
  char line[1024];
  size_t used;
  char sentence[voice_config::kSentenceBytes];
  size_t sentence_used;
  uint32_t turn_id;
  SentenceCallback callback;
  void* callback_context;
  bool complete;
  bool failed;
  uint8_t terminal_count;
  volatile bool* cancelled;
};

bool flush_sentence(SseContext* context) {
  while (context->sentence_used && (context->sentence[context->sentence_used - 1] == ' ' ||
                                   context->sentence[context->sentence_used - 1] == '\n')) --context->sentence_used;
  context->sentence[context->sentence_used] = 0;
  const bool ok = !context->sentence_used || context->callback(context->sentence, context->turn_id, context->callback_context);
  context->sentence_used = 0;
  context->terminal_count = 0;
  return ok;
}

void consume_sse_data(SseContext* context, const char* data) {
  const size_t data_length = strlen(data);
  if (data_length >= 6 && !memcmp(data, "[DONE]", 6) &&
      (data_length == 6 || data[6] == '\r')) {
    context->complete = true;
    return;
  }
  cJSON* json = cJSON_Parse(data);
  cJSON* type = json ? cJSON_GetObjectItemCaseSensitive(json, "type") : nullptr;
  if (cJSON_IsString(type) && !strcmp(type->valuestring, "response.output_text.delta")) {
    cJSON* delta = cJSON_GetObjectItemCaseSensitive(json, "delta");
    if (cJSON_IsString(delta) && delta->valuestring) {
      for (const char* p = delta->valuestring; *p; ++p) {
        if (sse_should_flush_text_segment(context->sentence_used, *p,
                                          voice_config::kTtsTextSoftLimitBytes) &&
            !flush_sentence(context)) context->failed = true;
        if (context->sentence_used + 2 >= sizeof(context->sentence) && !flush_sentence(context)) context->failed = true;
        context->sentence[context->sentence_used++] = *p;
        if (*p == '.' || *p == '!' || *p == '?') {
          ++context->terminal_count;
          if (sse_should_flush_tts_segment(context->terminal_count)) {
            if (!flush_sentence(context)) context->failed = true;
          }
        }
      }
    }
  } else if (cJSON_IsString(type) && !strcmp(type->valuestring, "response.completed")) {
    context->complete = true;
  } else if (cJSON_IsString(type) && (!strcmp(type->valuestring, "error") || !strcmp(type->valuestring, "response.failed"))) {
    context->failed = true;
  }
  if (json) cJSON_Delete(json);
}

esp_err_t sse_event(esp_http_client_event_t* event) {
  auto* context = static_cast<SseContext*>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_DATA || !context) return ESP_OK;
  for (int i = 0; i < event->data_len; ++i) {
    const char character = static_cast<const char*>(event->data)[i];
    if (character != '\n' && context->used + 1 < sizeof(context->line)) {
      context->line[context->used++] = character;
      continue;
    }
    context->line[context->used] = 0;
    const SseTerminalEvent terminal = classify_sse_event_line(context->line);
    if (terminal == SseTerminalEvent::kCompleted) context->complete = true;
    if (terminal == SseTerminalEvent::kFailed) context->failed = true;
    if (!strncmp(context->line, "data: ", 6)) consume_sse_data(context, context->line + 6);
    context->used = 0;
    if (context->failed || *context->cancelled) return ESP_FAIL;
  }
  return ESP_OK;
}

bool cloud_stream_response(const char* transcript, uint32_t turn_id, SentenceCallback callback, void* context_data, volatile bool* cancelled, CloudResult* result) {
  char escaped[1024]; json_escape(transcript, escaped, sizeof(escaped)); char request[1600]; snprintf(request, sizeof(request), "{\"model\":\"%s\",\"stream\":true,\"store\":false,\"max_output_tokens\":%d,\"instructions\":\"You are Luna, a concise helpful voice assistant. Start with one useful spoken sentence of six to ten words. Then continue in short plain spoken sentences without Markdown.\",\"input\":\"%s\"}", AZURE_AI_MODEL, voice_config::kMaxAnswerTokens, escaped);
  SseContext sse = {}; sse.turn_id = turn_id; sse.callback = callback; sse.callback_context = context_data; sse.cancelled = cancelled; esp_http_client_handle_t client = client_for(AZURE_AI_ENDPOINT, sse_event, &sse); if (!client) { set_result(result, CloudError::kMemory, 0, "client allocation"); return false; }
  esp_http_client_set_method(client, HTTP_METHOD_POST); esp_http_client_set_header(client, "api-key", AZURE_AI_API_KEY); esp_http_client_set_header(client, "Content-Type", "application/json"); esp_http_client_set_header(client, "Accept", "text/event-stream"); esp_http_client_set_post_field(client, request, strlen(request)); esp_err_t err = esp_http_client_perform(client); int status = esp_http_client_get_status_code(client); esp_http_client_cleanup(client);
  if (sse.sentence_used && !sse.failed && !*cancelled) sse.failed = !flush_sentence(&sse);
  if (err != ESP_OK || status != 200 || sse.failed || !sse.complete) { set_result(result, *cancelled ? CloudError::kCancelled : (err != ESP_OK ? CloudError::kNetwork : classify(status)), status, "responses stream failed"); return false; }
  set_result(result, CloudError::kNone, status, ""); return true;
}

struct TtsContext { TtsAudioCallback callback; void* context; uint32_t turn_id; volatile bool* cancelled; bool failed; };
esp_err_t tts_event(esp_http_client_event_t* event) { auto* context = static_cast<TtsContext*>(event->user_data); if (event->event_id == HTTP_EVENT_ON_DATA && (!*context->cancelled) && !context->callback(static_cast<const uint8_t*>(event->data), event->data_len, context->turn_id, context->context)) context->failed = true; return context->failed || *context->cancelled ? ESP_FAIL : ESP_OK; }
bool cloud_stream_tts(const char* text, uint32_t turn_id, TtsAudioCallback callback, void* context_data, volatile bool* cancelled, CloudResult* result) {
  char escaped[1400]; if (!xml_escape(text, escaped, sizeof(escaped))) { set_result(result, CloudError::kProtocol, 0, "text too long"); return false; } char ssml[1800]; const char* voice = contains_devanagari(text) ? TTS_VOICE_HINDI : TTS_VOICE_ENGLISH; snprintf(ssml, sizeof(ssml), "<speak version='1.0' xml:lang='en-IN' xmlns='http://www.w3.org/2001/10/synthesis'><voice name='%s'><prosody rate='+5%%'>%s</prosody></voice></speak>", voice, escaped);
  TtsContext tts = {callback, context_data, turn_id, cancelled, false}; esp_http_client_handle_t client = client_for(AZURE_TTS_ENDPOINT, tts_event, &tts); if (!client) { set_result(result, CloudError::kMemory, 0, "client allocation"); return false; } esp_http_client_set_method(client, HTTP_METHOD_POST); esp_http_client_set_header(client, "Ocp-Apim-Subscription-Key", AZURE_SPEECH_KEY); esp_http_client_set_header(client, "Content-Type", "application/ssml+xml"); esp_http_client_set_header(client, "X-Microsoft-OutputFormat", "raw-24khz-16bit-mono-pcm"); esp_http_client_set_post_field(client, ssml, strlen(ssml)); esp_err_t err = esp_http_client_perform(client); int status = esp_http_client_get_status_code(client); esp_http_client_cleanup(client); if (err != ESP_OK || status != 200 || tts.failed) { set_result(result, *cancelled ? CloudError::kCancelled : (err != ESP_OK ? CloudError::kNetwork : classify(status)), status, "TTS failed"); return false; } set_result(result, CloudError::kNone, status, ""); return true;
}
const char* cloud_error_name(CloudError error) { switch (error) { case CloudError::kNone: return "none"; case CloudError::kCancelled: return "cancelled"; case CloudError::kNetwork: return "network"; case CloudError::kAuthentication: return "authentication"; case CloudError::kRateLimited: return "rate_limited"; case CloudError::kService: return "service"; case CloudError::kProtocol: return "protocol"; case CloudError::kMemory: return "memory"; } return "unknown"; }
