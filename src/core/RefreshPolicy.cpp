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

}  // namespace pstryk
