#pragma once

#include <stddef.h>
#include <stdint.h>

enum class SseTerminalEvent {
  kNone,
  kCompleted,
  kFailed,
};

// Classifies the SSE event field without depending on the potentially large
// JSON payload sent with a terminal Responses event.
SseTerminalEvent classify_sse_event_line(const char* line);

// Each complete sentence becomes an independently prefetchable TTS segment.
bool sse_should_flush_tts_segment(uint8_t terminal_count);
bool sse_should_flush_text_segment(size_t current_bytes, char next_character, size_t soft_limit);
