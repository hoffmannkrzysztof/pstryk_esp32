#include "core/RefreshPolicy.h"
#include "core/TimeService.h"

namespace pstryk {

Window computeWindow(time_t now) {
  Window w;
  time_t midnight = localMidnightUtc(now);
  formatIso8601Utc(midnight, w.start);
  formatIso8601Utc(midnight + 48 * 3600, w.end);
  return w;
}

uint32_t nextRefreshMs(time_t now, bool hasTomorrow) {
  if (localHour(now) >= 12 && !hasTomorrow) return 20u * 60u * 1000u;
  return 30u * 60u * 1000u;
}

uint32_t secondsUntilNextWake(time_t now, bool hasTomorrow) {
  long secsIntoHour = (long)(now % 3600);
  uint32_t toTop = (uint32_t)(3600 - secsIntoHour);  // 1..3600
  uint32_t wake = toTop + 5u;                         // small guard past the turn
  int h = localHour(now);
  if (h >= 12 && h < 16 && !hasTomorrow && wake > 30u * 60u) wake = 30u * 60u;
  return wake;
}

uint32_t backoffSeconds(uint32_t consecFails) {
  uint32_t n = consecFails > 0 ? consecFails - 1 : 0;  // first failure -> base delay
  if (n > 6) n = 6;                                    // clamp before shifting
  uint32_t s = 60u << n;
  return s > 3600u ? 3600u : s;
}

}  // namespace pstryk
