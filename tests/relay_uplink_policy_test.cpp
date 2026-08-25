#include "relay_uplink_policy.h"

#include <assert.h>

int main() {
  assert(relay_uplink_frames_for_duration(0, 20) == 0);
  assert(relay_uplink_frames_for_duration(1, 20) == 1);
  assert(relay_uplink_frames_for_duration(10000, 20) == 500);
  assert(relay_uplink_frames_for_duration(10001, 20) == 501);

  assert(!relay_uplink_backlog_supported(499, 10000, 20));
  assert(relay_uplink_backlog_supported(500, 10000, 20));
  assert(relay_uplink_backlog_supported(512, 10000, 20));
  return 0;
}
