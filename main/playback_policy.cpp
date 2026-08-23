#include "playback_policy.h"

bool playback_turn_matches(uint32_t active_turn, uint32_t writer_turn) {
  return active_turn != 0 && active_turn == writer_turn;
}

bool playback_should_start(size_t buffered_bytes, size_t prebuffer_bytes, bool finishing) {
  if (!buffered_bytes) return false;
  return finishing || buffered_bytes >= prebuffer_bytes;
}
