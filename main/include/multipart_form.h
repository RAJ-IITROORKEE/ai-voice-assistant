#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr char kFastSttBoundary[] = "----LunaVoiceS3R7MA4YWxkTrZu0gW";

size_t build_fast_stt_prefix(char* output, size_t capacity);
size_t build_fast_stt_suffix(char* output, size_t capacity);
bool multipart_total_length(size_t prefix_bytes, size_t audio_bytes, size_t suffix_bytes,
                             size_t* total_bytes);
bool fast_stt_response_fits(int64_t content_length, size_t capacity);
