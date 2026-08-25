#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t relay_uplink_frames_for_duration(uint32_t duration_ms,
                                                   uint32_t frame_duration_ms) {
  return frame_duration_ms == 0 ? 0 :
      (static_cast<size_t>(duration_ms) + frame_duration_ms - 1) / frame_duration_ms;
}

constexpr bool relay_uplink_backlog_supported(size_t queue_depth, uint32_t duration_ms,
                                              uint32_t frame_duration_ms) {
  return queue_depth >= relay_uplink_frames_for_duration(duration_ms, frame_duration_ms);
}
