#include "core/RefreshPolicy.h"
#include "core/TimeService.h"

namespace pstryk {

Window computeWindow(time_t now) {
  Window w;
  time_t midnight = localMidnightUtc(now);
  formatIso8601Utc(midnight, w.start);
  // End on local midnight of day+2, computed through the CALENDAR. A flat
  // +48*3600 is not two local days across a DST change: on 2026-10-25 (25 h) it
  // ended at 23:00 local on the 26th, and with the half-open [start,end) window
  // the API never returned the 23:00-24:00 frame -- so "Jutro" charted 23 bars and
  // tomorrowCheapest could miss the cheapest hour of the day. +50 h lands inside
  // day+2 for a 23 h, 24 h or 25 h day, and the second localMidnightUtc() snaps it
  // back to the boundary. Unchanged on ordinary days.
  formatIso8601Utc(localMidnightUtc(midnight + 50 * 3600), w.end);
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

bool needsNetwork(time_t now, bool buttonWake, RtcCacheView cache,
                  uint32_t ntpAgeSec, bool otaDue) {
  if (buttonWake || otaDue) return true;
  if (!cache.coversNow) return true;
  if (ntpAgeSec > 24u * 3600u) return true;            // let NTP re-discipline the RTC daily
  if (localHour(now) >= 12 && !cache.hasTomorrow) return true;   // tomorrow-hunt
  if (dstChangesWithin(now, 2 * 3600)) return true;    // RTC wall clock ambiguous here
  return false;
}

}  // namespace pstryk
