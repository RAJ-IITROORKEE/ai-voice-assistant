#pragma once

#include <stddef.h>
#include <stdint.h>

enum class TtsSegmentRoute : uint8_t {
  kDirect,
  kLookahead,
};

// The opening segment reaches the playback ring immediately. Later segments
// are synthesized ahead of time, then handed off in their original order.
TtsSegmentRoute tts_segment_route(size_t accepted_segment_count);

// A complete look-ahead clip can be copied without waiting when it fits in
// the ring space left after the retained playback lead.
bool tts_clip_fits_handoff(size_t clip_bytes, size_t ring_bytes, size_t retained_bytes);
