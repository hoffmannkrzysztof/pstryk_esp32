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

}  // namespace pstryk
