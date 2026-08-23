#include "sse_protocol.h"

#include <assert.h>

int main() {
  assert(classify_sse_event_line("event: response.completed") == SseTerminalEvent::kCompleted);
  assert(classify_sse_event_line("event: response.completed\r") == SseTerminalEvent::kCompleted);
  assert(classify_sse_event_line("event: response.failed") == SseTerminalEvent::kFailed);
  assert(classify_sse_event_line("event: error") == SseTerminalEvent::kFailed);
  assert(classify_sse_event_line("data: {\"type\":\"response.completed\"}") == SseTerminalEvent::kNone);
  assert(!sse_should_flush_tts_segment(0));
  assert(sse_should_flush_tts_segment(1));
  assert(sse_should_flush_tts_segment(2));
  assert(!sse_should_flush_text_segment(159, ' ', 160));
  assert(sse_should_flush_text_segment(160, ' ', 160));
  assert(!sse_should_flush_text_segment(200, 'x', 160));
  return 0;
}
