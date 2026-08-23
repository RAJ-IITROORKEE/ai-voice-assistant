#include "tts_pipeline_policy.h"

TtsSegmentRoute tts_segment_route(size_t accepted_segment_count) {
  return accepted_segment_count == 0 ? TtsSegmentRoute::kDirect : TtsSegmentRoute::kLookahead;
}

bool tts_clip_fits_handoff(size_t clip_bytes, size_t ring_bytes, size_t retained_bytes) {
  return retained_bytes <= ring_bytes && clip_bytes <= ring_bytes - retained_bytes;
}
