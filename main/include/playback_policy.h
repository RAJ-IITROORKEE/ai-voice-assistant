#pragma once

#include <stddef.h>
#include <stdint.h>

// Pure playback decisions shared by the I2S task and host regression tests.
bool playback_turn_matches(uint32_t active_turn, uint32_t writer_turn);
bool playback_should_start(size_t buffered_bytes, size_t prebuffer_bytes, bool finishing);
