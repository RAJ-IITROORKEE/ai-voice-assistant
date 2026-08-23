#include "multipart_form.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main() {
  char prefix[256] = {};
  char suffix[384] = {};
  const size_t prefix_bytes = build_fast_stt_prefix(prefix, sizeof(prefix));
  const size_t suffix_bytes = build_fast_stt_suffix(suffix, sizeof(suffix));

  assert(prefix_bytes > 0);
  assert(strstr(prefix, "name=\"audio\"") != nullptr);
  assert(strstr(prefix, "filename=\"speech.wav\"") != nullptr);
  assert(strstr(prefix, "Content-Type: audio/wav\r\n\r\n") != nullptr);
  assert(suffix_bytes > 0);
  assert(strstr(suffix, "name=\"definition\"") != nullptr);
  assert(strstr(suffix, "application/json") != nullptr);
  assert(strstr(suffix, "--\r\n") != nullptr);

  size_t total = 0;
  assert(multipart_total_length(prefix_bytes, 1920044, suffix_bytes, &total));
  assert(total == prefix_bytes + 1920044 + suffix_bytes);
  assert(!multipart_total_length(SIZE_MAX, 1, 0, &total));
  assert(fast_stt_response_fits(-1, 65536));
  assert(fast_stt_response_fits(65535, 65536));
  assert(!fast_stt_response_fits(65536, 65536));
  assert(!fast_stt_response_fits(1, 0));
  return 0;
}
