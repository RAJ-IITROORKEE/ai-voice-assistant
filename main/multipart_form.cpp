#include "multipart_form.h"

#include <stdint.h>
#include <stdio.h>

namespace {
constexpr char kDefinition[] =
    "{\"profanityFilterMode\":\"Masked\",\"phraseList\":{\"phrases\":[\"ESP32\",\"M5Stack\",\"Atom VoiceS3R\",\"Azure\",\"Luna\"]}}";
}

size_t build_fast_stt_prefix(char* output, size_t capacity) {
  if (!output || !capacity) return 0;
  const int length = snprintf(output, capacity,
      "--%s\r\n"
      "Content-Disposition: form-data; name=\"audio\"; filename=\"speech.wav\"\r\n"
      "Content-Type: audio/wav\r\n\r\n",
      kFastSttBoundary);
  return length > 0 && static_cast<size_t>(length) < capacity ? static_cast<size_t>(length) : 0;
}

size_t build_fast_stt_suffix(char* output, size_t capacity) {
  if (!output || !capacity) return 0;
  const int length = snprintf(output, capacity,
      "\r\n--%s\r\n"
      "Content-Disposition: form-data; name=\"definition\"\r\n"
      "Content-Type: application/json\r\n\r\n"
      "%s\r\n--%s--\r\n",
      kFastSttBoundary, kDefinition, kFastSttBoundary);
  return length > 0 && static_cast<size_t>(length) < capacity ? static_cast<size_t>(length) : 0;
}

bool multipart_total_length(size_t prefix_bytes, size_t audio_bytes, size_t suffix_bytes,
                             size_t* total_bytes) {
  if (!total_bytes || prefix_bytes > SIZE_MAX - audio_bytes ||
      prefix_bytes + audio_bytes > SIZE_MAX - suffix_bytes) return false;
  *total_bytes = prefix_bytes + audio_bytes + suffix_bytes;
  return true;
}

bool fast_stt_response_fits(int64_t content_length, size_t capacity) {
  return capacity > 0 && (content_length < 0 || static_cast<uint64_t>(content_length) < capacity);
}
