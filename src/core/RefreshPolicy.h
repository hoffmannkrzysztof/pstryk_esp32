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

// What the RTC-retained price cache can say about the current wake.
struct RtcCacheView {
  bool coversNow = false;    // a cached frame contains `now`
  bool hasTomorrow = false;  // cached frames include `now`'s local tomorrow
};

// Radio-free-wake gate: day-ahead prices are immutable once published, so a wake
// whose hour is already cached can render and go back to sleep without Wi-Fi.
// Network is required for: a button wake (user asked for fresh data), a cache
// miss, the afternoon tomorrow-hunt, a >24 h old NTP sync (RTC drift check), a
// due OTA-manifest check, and the ambiguous hours around a DST switch.
bool needsNetwork(time_t now, bool buttonWake, RtcCacheView cache,
                  uint32_t ntpAgeSec, bool otaDue);

// True when `later` lies in a different clock hour than `earlier`. The epoch
// hour is the frame index (frames are aligned to UTC hours and Poland's offsets
// are whole hours), and unlike a local-hour comparison it also tells the
// repeated 02:00 of the autumn switch from the first one.
bool hourChanged(time_t earlier, time_t later);

// Consecutive failures of one kind that make an outage "confirmed": from here on
// the e-paper marks cached prices stale and may show the failure message. A
// single blip (a missed scan, one timed-out request) stays invisible.
static const uint32_t kConfirmedFailures = 3;

// What the e-paper shows, relative to one failure kind.
enum class PanelShows : uint8_t {
  Other,         // another message, the setup/welcome screen, or unknown (reset)
  Dashboard,     // prices
  ThisFailure,   // the message for exactly this failure kind
};

// After a failed network attempt -- the n-th of its kind in a row -- should the
// e-paper switch to the failure message? Never while the RTC cache still covers
// the current hour: a stale-marked dashboard from the cache is the better screen.
// A dashboard up while the cache does NOT cover the hour presents a past hour
// as TERAZ, so it goes at the first failure. Over any other screen the message
// waits for a confirmed outage, and it is never repainted while already up.
bool failureMessageDue(uint32_t n, bool cacheCoversNow, PanelShows panel);

// Seconds until the next unattended network attempt is allowed, given the
// earliest-retry epoch recorded after a failure (0 = none). This is what keeps
// the retry policy (backoff, Retry-After) separate from the repaint schedule: a
// wake inside the hold repaints from the cache without the radio. A button
// press bypasses it -- a human asked for fresh data -- and a hold over an hour
// (every retry this firmware schedules is <= 1 h) can only mean the clock
// stepped back after it was set, so it is capped.
uint32_t netHoldLeft(uint32_t retryAtEpoch, time_t now, bool buttonWake);

}  // namespace pstryk
