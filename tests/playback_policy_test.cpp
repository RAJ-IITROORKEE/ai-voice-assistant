#include "playback_policy.h"

#include <assert.h>

int main() {
  assert(playback_turn_matches(7, 7));
  assert(!playback_turn_matches(7, 8));
  assert(!playback_turn_matches(0, 0));

  assert(!playback_should_start(38398, 38400, false));
  assert(playback_should_start(38400, 38400, false));
  assert(playback_should_start(2, 38400, true));
  assert(!playback_should_start(0, 38400, true));

  // Recovery uses its own small watermark instead of replaying the full
  // initial delay after a transient producer miss.
  assert(!playback_should_start(9598, 9600, false));
  assert(playback_should_start(9600, 9600, false));
  return 0;
}
