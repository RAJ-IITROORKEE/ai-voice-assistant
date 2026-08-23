#include "sse_protocol.h"

#include <string.h>

SseTerminalEvent classify_sse_event_line(const char* line) {
  if (!line) return SseTerminalEvent::kNone;
  size_t length = strlen(line);
  while (length && (line[length - 1] == '\r' || line[length - 1] == ' ')) --length;
  constexpr char kCompleted[] = "event: response.completed";
  constexpr char kFailed[] = "event: response.failed";
  constexpr char kError[] = "event: error";
  if (length == sizeof(kCompleted) - 1 && !memcmp(line, kCompleted, length)) return SseTerminalEvent::kCompleted;
  if ((length == sizeof(kFailed) - 1 && !memcmp(line, kFailed, length)) ||
      (length == sizeof(kError) - 1 && !memcmp(line, kError, length))) return SseTerminalEvent::kFailed;
  return SseTerminalEvent::kNone;
}

bool sse_should_flush_tts_segment(uint8_t terminal_count) {
  return terminal_count > 0;
}

bool sse_should_flush_text_segment(size_t current_bytes, char next_character, size_t soft_limit) {
  return soft_limit > 0 && current_bytes >= soft_limit && next_character == ' ';
}
