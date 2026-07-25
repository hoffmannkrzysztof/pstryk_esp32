#include "core/Version.h"
#include <cstring>
#include <cstdlib>

namespace pstryk {

// Parse leading "[v]A.B.C" into three ints; stops at the first non-version char.
static void parse3(const char* s, long out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (!s) return;
  if (*s == 'v' || *s == 'V') ++s;
  for (int i = 0; i < 3 && *s; ++i) {
    char* end = nullptr;
    out[i] = std::strtol(s, &end, 10);
    s = end;
    if (*s == '.') ++s;
  }
}

bool isNewer(const char* candidate, const char* current) {
  if (!candidate || !current) return false;
  long c[3], r[3];
  parse3(candidate, c);
  parse3(current, r);
  for (int i = 0; i < 3; ++i) {
    if (c[i] != r[i]) return c[i] > r[i];
  }
  return false;
}

bool isDevVersion(const char* v) {
  if (!v || !*v) return true;
  // EXACT match on the sentinel, not an unanchored substring search. As a substring
  // test, any release tag containing "-dev" (say v1.4.0-dev1) turned self-update off
  // PERMANENTLY on every device that installed it: parse3 ignores the suffix, so
  // isNewer said yes, the board installed it, and from then on shouldApplyUpdate()
  // refused every future manifest -- with no recovery path short of a USB reflash
  // (the field paths all pass force=false, and FIRMWARE_VERSION is a #define, so
  // neither the portal nor a factory reset can change it). The release workflow now
  // also rejects a tag that isn't plain vMAJOR.MINOR.PATCH; this is the second lock.
  return std::strcmp(v, "0.0.0-dev") == 0;
}

}  // namespace pstryk
