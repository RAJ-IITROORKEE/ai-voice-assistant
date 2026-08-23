#include "cloud.h"
#include "audio.h"
#include "voice_config.h"
#include "secrets.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
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
void wifi_event(void*, esp_event_base_t base, int32_t event, void*) {
  if (base == WIFI_EVENT && event == WIFI_EVENT_STA_DISCONNECTED) { s_wifi = false; esp_wifi_connect(); }
  if (base == IP_EVENT && event == IP_EVENT_STA_GOT_IP) s_wifi = true;
}
}

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

bool cloud_transcribe(const Recording& recording, char* transcript, size_t transcript_size, volatile bool* cancelled, CloudResult* result) {
  if (!recording.wav || !recording.data_bytes || *cancelled) { set_result(result, CloudError::kCancelled, 0, "cancelled"); return false; }
  char url[320]; snprintf(url, sizeof(url), "%s/stt/speech/recognition/conversation/cognitiveservices/v1?language=%s&format=simple", AZURE_SPEECH_ENDPOINT, SPEECH_LANGUAGE);
  char body[1024] = {};
  ResponseBody response = {body, sizeof(body), 0, false};
  esp_http_client_handle_t client = client_for(url, response_body_event, &response); if (!client) { set_result(result, CloudError::kMemory, 0, "client allocation"); return false; }
  esp_http_client_set_method(client, HTTP_METHOD_POST); esp_http_client_set_header(client, "Ocp-Apim-Subscription-Key", AZURE_SPEECH_KEY); esp_http_client_set_header(client, "Content-Type", "audio/wav; codecs=audio/pcm; samplerate=16000");
  esp_http_client_set_post_field(client, reinterpret_cast<const char*>(recording.wav), recording.data_bytes + 44);
  esp_err_t err = esp_http_client_perform(client); int status = esp_http_client_get_status_code(client); esp_http_client_cleanup(client);
  if (err != ESP_OK || status != 200 || response.overflow) { set_result(result, err == ESP_OK ? classify(status) : CloudError::kNetwork, status, "speech request failed"); return false; }
  cJSON* json = cJSON_ParseWithLength(body, response.used); cJSON* text = json ? cJSON_GetObjectItemCaseSensitive(json, "DisplayText") : nullptr;
  if (!cJSON_IsString(text) || !text->valuestring) { if (json) cJSON_Delete(json); set_result(result, CloudError::kProtocol, status, "speech response"); return false; }
  snprintf(transcript, transcript_size, "%s", text->valuestring); cJSON_Delete(json); set_result(result, CloudError::kNone, status, ""); return true;
}

struct SseContext { char line[1024]; size_t used; char sentence[voice_config::kSentenceBytes]; size_t sentence_used; uint32_t turn_id; SentenceCallback callback; void* callback_context; bool complete; bool failed; volatile bool* cancelled; };
bool flush_sentence(SseContext* context) { while (context->sentence_used && (context->sentence[context->sentence_used - 1] == ' ' || context->sentence[context->sentence_used - 1] == '\n')) --context->sentence_used; context->sentence[context->sentence_used] = 0; bool ok = !context->sentence_used || context->callback(context->sentence, context->turn_id, context->callback_context); context->sentence_used = 0; return ok; }
esp_err_t sse_event(esp_http_client_event_t* event) { auto* context = static_cast<SseContext*>(event->user_data); if (event->event_id != HTTP_EVENT_ON_DATA || !context) return ESP_OK; for (int i = 0; i < event->data_len; ++i) { char c = static_cast<const char*>(event->data)[i]; if (c != '\n' && context->used + 1 < sizeof(context->line)) { context->line[context->used++] = c; continue; } context->line[context->used] = 0; if (!strncmp(context->line, "data: ", 6)) { const char* data = context->line + 6; if (!strcmp(data, "[DONE]")) context->complete = true; else { cJSON* json = cJSON_Parse(data); cJSON* type = json ? cJSON_GetObjectItemCaseSensitive(json, "type") : nullptr; if (cJSON_IsString(type) && !strcmp(type->valuestring, "response.output_text.delta")) { cJSON* delta = cJSON_GetObjectItemCaseSensitive(json, "delta"); if (cJSON_IsString(delta) && delta->valuestring) for (const char* p = delta->valuestring; *p; ++p) { if (context->sentence_used + 2 >= sizeof(context->sentence)) { if (!flush_sentence(context)) context->failed = true; } context->sentence[context->sentence_used++] = *p; if ((*p == '.' || *p == '!' || *p == '?') && context->sentence_used >= 20 && !flush_sentence(context)) context->failed = true; } } else if (cJSON_IsString(type) && (!strcmp(type->valuestring, "response.completed"))) context->complete = true; else if (cJSON_IsString(type) && (!strcmp(type->valuestring, "error") || !strcmp(type->valuestring, "response.failed"))) context->failed = true; if (json) cJSON_Delete(json); } } context->used = 0; if (context->failed || *context->cancelled) return ESP_FAIL; } return ESP_OK; }

bool cloud_stream_response(const char* transcript, uint32_t turn_id, SentenceCallback callback, void* context_data, volatile bool* cancelled, CloudResult* result) {
  char escaped[1024]; json_escape(transcript, escaped, sizeof(escaped)); char request[1600]; snprintf(request, sizeof(request), "{\"model\":\"%s\",\"stream\":true,\"store\":false,\"max_output_tokens\":%d,\"instructions\":\"You are Luna, a concise helpful voice assistant. Give a short useful first sentence, then continue in plain spoken language without Markdown.\",\"input\":\"%s\"}", AZURE_AI_MODEL, voice_config::kMaxAnswerTokens, escaped);
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
