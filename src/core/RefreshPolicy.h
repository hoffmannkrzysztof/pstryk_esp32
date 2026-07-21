#pragma once
#include <ctime>
#include <cstdint>

namespace pstryk {

struct Window {
  char start[21];  // "YYYY-MM-DDTHH:MM:SSZ"
  char end[21];
};

// Query window: local midnight today -> +48h (so tomorrow comes along once published).
Window computeWindow(time_t now);

// 30 min normally; 20 min (cap-safe max of 3/hr) from 12:00 local until tomorrow is held.
uint32_t nextRefreshMs(time_t now, bool hasTomorrow);

// Seconds to deep-sleep until the next refresh: time to the next top-of-hour
// (+5 s guard), capped at 30 min during 12:00-16:00 local while tomorrow is absent.
uint32_t secondsUntilNextWake(time_t now, bool hasTomorrow);

// Escalating retry delay after `consecFails` consecutive failures (1-based):
// 60, 120, 240, ... doubling, capped at 3600 s. A flat short retry drains the
// battery during a sustained outage (~30-70 s awake per attempt); doubling keeps
// the first retry quick but converges to one attempt per hour.
uint32_t backoffSeconds(uint32_t consecFails);

}  // namespace pstryk
