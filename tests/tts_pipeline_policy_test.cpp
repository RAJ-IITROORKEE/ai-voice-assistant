#include "tts_pipeline_policy.h"

#include <assert.h>

int main() {
  assert(tts_segment_route(0) == TtsSegmentRoute::kDirect);
  assert(tts_segment_route(1) == TtsSegmentRoute::kLookahead);
  assert(tts_segment_route(12) == TtsSegmentRoute::kLookahead);

  assert(tts_clip_fits_handoff(384000, 480000, 57600));
  assert(!tts_clip_fits_handoff(422401, 480000, 57600));
  assert(!tts_clip_fits_handoff(1, 480000, 480001));
  return 0;
}
