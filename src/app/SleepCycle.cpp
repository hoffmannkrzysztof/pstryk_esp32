#include "app/SleepCycle.h"
#include "render/EpdRenderer.h"
#include "render/EpdDashboard.h"
#include "net/PstrykClient.h"
#include "net/WiFiProvisioner.h"
#include "net/OtaUpdater.h"
#include "net/OtaRollback.h"
#include "core/OtaPolicy.h"
#include "core/TimeService.h"
#include "core/PriceLogic.h"
#include "core/RefreshPolicy.h"
#include "core/Battery.h"
#include "core/PriceData.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <esp_attr.h>
#include "driver/rtc_io.h"
#include <sys/time.h>
#include <cstdio>
#include <cstring>
#include <cmath>
extern "C" {
#include "epd_driver.h"   // epd_poweron / epd_poweroff
}

namespace pstryk {

// NTP sync handshake: configTzTime() starts SNTP asynchronously; this flag (set
// by the notification callback) lets setup() block until a REAL sync lands,
// instead of assuming a plausible-looking clock is correct.
static volatile bool s_ntpSynced = false;
static void onNtpSync(struct timeval*) { s_ntpSynced = true; }

static const int      PIN_BTN  = 21;   // user button (active LOW)
static const int      PIN_BATT = 14;   // battery ADC (ADC2) via 2:1 divider
static const uint8_t  PCF8563_ADDR = 0x51;
static const int      I2C_SDA = 18, I2C_SCL = 17;
static const time_t   kTimeValid = 1700000000;

// Consecutive transient (network/parse) fetch failures, retained across deep sleep.
// Lets a one-off cold-wake blip keep the last good e-paper image instead of wiping
// it to an error screen; the error surfaces once after a sustained outage, and the
// retry cadence doubles (backoffSeconds) so an outage costs ~1 wake/h, not 1/min.
RTC_DATA_ATTR static uint32_t g_consecFail = 0;
// Consecutive wakes that could not associate with Wi-Fi at all; same backoff curve.
RTC_DATA_ATTR static uint32_t g_wifiFail = 0;
// Set once the API-key error screen has been painted, so identical wakes don't
// re-flash the same message (every drawMessage is a full panel clear+draw).
RTC_DATA_ATTR static uint8_t g_authShown = 0;
// Last OTA-manifest check (epoch s), retained across deep sleep so we poll GitHub
// at most once/day per device rather than every wake.
RTC_DATA_ATTR static uint32_t g_lastOtaCheck = 0;

// --- RTC price cache -------------------------------------------------------
// Day-ahead frames are immutable once published, so they survive deep sleep here
// and most hourly wakes repaint straight from this cache without ever powering
// the radio (see needsNetwork). 52 slots cover the 48 h fetch window plus the
// 25 h DST day (~1.25 KB of the 8 KB RTC slow RAM). Zeroed by any power loss
// (RTC RAM semantics) and explicitly on reconfigure.
struct RtcFrame { int64_t start; float buy, sell; uint8_t flags; };
static const int     kCacheMax = 52;
static const uint8_t kCacheCheap = 1, kCacheExpensive = 2;
RTC_DATA_ATTR static struct { uint32_t count; RtcFrame f[kCacheMax]; } g_cache;
// Epoch of the last GENUINE SNTP sync (never RTC-seeded time). Cache wakes
// require one <24 h old, so NTP keeps re-disciplining the PCF8563 daily and
// stays the authority on every connected cycle (the +2h-bug invariant).
RTC_DATA_ATTR static uint32_t g_lastNtpSync = 0;
// Signature of the last painted dashboard; a wake whose content is identical
// (hunt wakes, drift-early wakes) skips the full panel flash entirely.
RTC_DATA_ATTR static uint32_t g_lastDrawSig = 0;
// Local day ordinal of the last 4-cycle deghost clear; ordinary refreshes use
// the quick 2-cycle clear and the full deghost runs on the first paint of a day.
RTC_DATA_ATTR static int32_t g_lastDeepCleanDay = 0;
// Set once the deep-discharge screen has been painted; cleared on recovery.
RTC_DATA_ATTR static uint8_t g_lowBattParked = 0;

static int bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool SleepCycle::buttonHeld(uint32_t ms) {
  if (digitalRead(PIN_BTN) != LOW) return false;
  uint32_t t0 = millis();
  while (digitalRead(PIN_BTN) == LOW) {
    if (millis() - t0 >= ms) return true;
    delay(20);
  }
  return false;
}

int SleepCycle::readBatteryPercent(bool& low, float& volts) {
  epd_poweron();             // POWER_EN gates the battery divider
  delay(10);
  long acc = 0;
  for (int i = 0; i < 16; ++i) acc += analogReadMilliVolts(PIN_BATT);
  epd_poweroff();
  float pinMv = acc / 16.0f;
  volts = batteryVoltsFromPinMv(pinMv);
  low = volts < 3.45f;
  return batteryPercent(volts);
}

bool SleepCycle::rtcRead(time_t& out) {
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)PCF8563_ADDR, 7) != 7) return false;
  uint8_t s = Wire.read(), mi = Wire.read(), h = Wire.read();
  uint8_t d = Wire.read(); Wire.read(); uint8_t mo = Wire.read(), y = Wire.read();
  if (s & 0x80) return false;  // VL: low-voltage -> time invalid
  struct tm t = {};
  t.tm_sec = bcd2dec(s & 0x7F); t.tm_min = bcd2dec(mi & 0x7F);
  t.tm_hour = bcd2dec(h & 0x3F); t.tm_mday = bcd2dec(d & 0x3F);
  t.tm_mon = bcd2dec(mo & 0x1F) - 1; t.tm_year = bcd2dec(y) + 100;  // 2000+yy
  t.tm_isdst = -1;
  out = mktime(&t);                 // local time -> epoch (Warsaw TZ already set)
  return out > kTimeValid;
}

void SleepCycle::rtcWrite(time_t epoch) {
  struct tm t; localtime_r(&epoch, &t);
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(0x02);
  Wire.write(dec2bcd(t.tm_sec)); Wire.write(dec2bcd(t.tm_min)); Wire.write(dec2bcd(t.tm_hour));
  Wire.write(dec2bcd(t.tm_mday)); Wire.write(dec2bcd(t.tm_wday));
  Wire.write(dec2bcd(t.tm_mon + 1)); Wire.write(dec2bcd((t.tm_year + 1900) % 100));
  Wire.endTransmission();
}

void SleepCycle::sleepFor(uint32_t seconds) {
  // Wait (bounded) for the button to be released, so a still-pressed button does
  // not immediately re-trigger the level-sensitive ext0 wake.
  uint32_t t0 = millis();
  while (digitalRead(PIN_BTN) == LOW && millis() - t0 < 5000) delay(10);
  // Retain the pull-up on the wake pin through deep sleep (RTC domain) so a
  // floating GPIO cannot spuriously wake the board.
  rtc_gpio_pullup_en((gpio_num_t)PIN_BTN);
  rtc_gpio_pulldown_dis((gpio_num_t)PIN_BTN);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN, 0);  // button (active low) wake
  // Zero the EPD CFG shift register for the night. epd_poweroff() alone leaves
  // ep_scan_direction latched high in the (always-3.3V-powered) register, which
  // keeps driving the unpowered panel through deep sleep; epd_poweroff_all()
  // clears every output (LilyGo's own pre-deep-sleep pattern for this board).
  // Ordered rail shutdown first -- idempotent if the panel is already off.
  epd_poweroff();
  epd_poweroff_all();
  esp_deep_sleep_start();
}

static void cacheStore(const PriceData& data) {
  uint32_t n = 0;
  for (const PriceFrame& fr : data.frames) {
    if (n >= (uint32_t)kCacheMax) break;
    RtcFrame& c = g_cache.f[n++];
    c.start = (int64_t)fr.start;
    c.buy = fr.buy;
    c.sell = fr.sell;
    c.flags = (uint8_t)((fr.isCheap ? kCacheCheap : 0) |
                        (fr.isExpensive ? kCacheExpensive : 0));
  }
  g_cache.count = n;
}

static void cacheLoad(PriceData& out) {
  out.frames.clear();
  for (uint32_t i = 0; i < g_cache.count && i < (uint32_t)kCacheMax; ++i) {
    const RtcFrame& c = g_cache.f[i];
    PriceFrame fr;
    fr.start = (time_t)c.start;
    fr.buy = c.buy;
    fr.sell = c.sell;
    fr.isCheap = (c.flags & kCacheCheap) != 0;
    fr.isExpensive = (c.flags & kCacheExpensive) != 0;
    out.frames.push_back(fr);
  }
}

static RtcCacheView cacheView(time_t now) {
  RtcCacheView cv;
  int tomorrowOrd = localDayOrdinal(now) + 1;
  for (uint32_t i = 0; i < g_cache.count && i < (uint32_t)kCacheMax; ++i) {
    time_t s = (time_t)g_cache.f[i].start;
    if (now >= s && now < s + 3600) cv.coversNow = true;
    if (localDayOrdinal(s) == tomorrowOrd) cv.hasTomorrow = true;
  }
  return cv;
}

// FNV-1a over everything the dashboard actually shows. The status-bar clock is
// deliberately EXCLUDED: it reads the wake time anyway, and a clock-only change
// is not worth a multi-second full-panel flash (the existing semantic is that
// the timestamp shows the last repaint, valid for the whole hour).
static uint32_t viewSignature(const PriceView& v, const EpdStatus& st) {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t x) { h ^= x; h *= 16777619u; };
  auto mixBar = [&mix](const Bar& b) {
    mix((uint32_t)b.hour);
    mix((uint32_t)(int32_t)lroundf(b.price * 100.0f));
    mix((uint32_t)((b.isCheap ? 1 : 0) | (b.isExpensive ? 2 : 0)));
  };
  for (const Bar& b : v.today) mixBar(b);
  for (const Bar& b : v.tomorrow) mixBar(b);
  mix((uint32_t)v.liveIndex);
  mix((uint32_t)(v.hasTomorrow ? 1 : 0));
  mix((uint32_t)st.batteryPct);
  mix((uint32_t)(st.batteryLow ? 1 : 0));
  return h == 0 ? 1u : h;   // 0 is reserved for "invalid -> must draw"
}

// Message screens replace the dashboard, so they invalidate its signature --
// the next dashboard paint must never be skipped after an error screen.
static void msgScreen(IRenderer& g, const char* line1, const char* line2) {
  g_lastDrawSig = 0;
  drawMessage(g, line1, line2);
}

#ifdef PSTRYK_OTA_BOOTSTRAP
// Installer image: provision Wi-Fi (+ API key, saved to NVS for the real firmware),
// then force-install the latest signed release for this board and reboot into it.
// OtaUpdater::runOnce(force=true) reboots on success; on failure we show a message
// and retry on the next wake.
void SleepCycle::runBootstrap() {
  settings_.load();
  bool needPortal = !settings_.isComplete();
  if (needPortal) {
    char pskLine[40];
    std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                  WiFiProvisioner::portalPassword());
    msgScreen(gfx_, "Konfiguracja", pskLine);
  } else {
    msgScreen(gfx_, "Instalacja", "Pobieranie najnowszej wersji...");
  }
  WiFiProvisioner prov;
  if (!prov.ensureConnected(settings_, /*forcePortal=*/needPortal)) {
    msgScreen(gfx_, "Brak Wi-Fi", "Sprobuje ponownie");
    sleepFor(60);
    return;
  }
  if (needPortal) msgScreen(gfx_, "Instalacja", "Pobieranie najnowszej wersji...");
  OtaUpdater().runOnce(/*force=*/true);  // reboots into the installed release on success
  msgScreen(gfx_, "Blad instalacji", "Sprobuje ponownie");  // only reached on failure
  sleepFor(60);
}
#endif

void SleepCycle::setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(PIN_BTN, INPUT_PULLUP);
  timeServiceBegin();                 // install Warsaw TZ for mktime/localtime
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!gfx_.begin()) { sleepFor(3600); return; }
  // Reaching here means PSRAM + display init succeeded -> a just-OTA'd image is
  // healthy enough to keep. Confirm now (before Wi-Fi) so a transient network blip
  // on a later wake can't trigger a false rollback on this sleeping board.
  confirmRunningImageValid();

#ifdef PSTRYK_OTA_BOOTSTRAP
  runBootstrap();   // installer image: fetch+flash the latest release, reboot into it
  return;
#endif

  // A held button is caught below and opens the captive portal; a short-press
  // (ext0) wake means "refresh now" and is forced through the network path by
  // needsNetwork(buttonWake=true) in the cache gate.
  // 1) reconfigure if button held at boot, or if no saved config
  settings_.load();
  if (buttonHeld(3000) || !settings_.isComplete()) {
    char pskLine[40];
    std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                  WiFiProvisioner::portalPassword());
    msgScreen(gfx_, "Konfiguracja", pskLine);
    WiFiProvisioner prov;
    prov.ensureConnected(settings_, /*forcePortal=*/true);
    g_cache.count = 0;                // fresh config -> force a fetch next cycle
    sleepFor(2);                      // wake immediately to run a normal cycle
    return;
  }

  // 2) battery BEFORE Wi-Fi (ADC2 conflicts with Wi-Fi)
  bool batLow = false;
  float batVolts = 0.0f;
  int batPct = readBatteryPercent(batLow, batVolts);
  EpdStatus st; st.batteryPct = batPct; st.batteryLow = batLow;

  // Deep-discharge floor: below ~3.35 V both the radio burst and the panel
  // flash abuse the cell. Park with one clear message and re-check every 6 h
  // until a charger brings the voltage back.
  if (batVolts > 0.5f && batVolts < 3.35f) {
    if (!g_lowBattParked) {
      msgScreen(gfx_, "Bateria rozladowana", "Podlacz ladowarke");
      g_lowBattParked = 1;
    }
    sleepFor(6u * 3600u);
    return;
  }
  g_lowBattParked = 0;

  // 3) seed clock from RTC so we have SOME time even if Wi-Fi fails. This is only
  // a fallback -- when Wi-Fi is up, NTP below overrides it (step 5). The RTC must
  // never be the final authority on a connected cycle, or a wrong RTC silently
  // wins (the +2h bug).
  time_t rtcEpoch;
  if (rtcRead(rtcEpoch)) {
    struct timeval tv = { rtcEpoch, 0 }; settimeofday(&tv, nullptr);
  }

  // 3.5) Radio-free wake: when the cached frames already cover this hour and
  // nothing needs the network (fresh NTP, no OTA due, tomorrow present or not
  // needed yet, no DST ambiguity, not a button press), repaint from the cache
  // and go back to sleep without ever calling WiFi.begin() -- the radio phase
  // dominates the energy cost of a wake, and the prices cannot have changed.
  bool buttonWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
  time_t seedNow = time(nullptr);
  if (seedNow > kTimeValid && g_cache.count > 0) {
    uint32_t ntpAge = (g_lastNtpSync > 0 && seedNow > (time_t)g_lastNtpSync)
                          ? (uint32_t)(seedNow - (time_t)g_lastNtpSync)
                          : 0xFFFFFFFFu;
    // A due OTA check does not force the radio on a low cell: a ~2 MB download
    // there risks a brownout mid-flash; the check resumes once charged.
    bool otaDue = !batLow &&
                  dueForOtaCheck(g_lastOtaCheck, (uint32_t)seedNow, 24u * 3600u);
    if (!needsNetwork(seedNow, buttonWake, cacheView(seedNow), ntpAge, otaDue)) {
      PriceData data;
      cacheLoad(data);
      PriceView cachedView = buildView(data, seedNow);
      if (cachedView.hasData) {
        struct tm lt; localtime_r(&seedNow, &lt);
        char clk[6]; std::snprintf(clk, sizeof(clk), "%02d:%02d", lt.tm_hour, lt.tm_min);
        st.clockHHMM = clk;
        st.wifiOk = true;             // radio deliberately off, not a failure
        uint32_t sig = viewSignature(cachedView, st);
        if (sig != g_lastDrawSig) {   // skip the flash when nothing changed
          if (localDayOrdinal(seedNow) != g_lastDeepCleanDay) {
            gfx_.requestDeepClean();  // first paint of the day: full deghost
            g_lastDeepCleanDay = localDayOrdinal(seedNow);
          }
          drawDashboard(gfx_, cachedView, st);
          g_lastDrawSig = sig;
        }
        sleepFor(secondsUntilNextWake(time(nullptr), cachedView.hasTomorrow));
        return;
      }
    }
  }

  // 4) Wi-Fi
  WiFiProvisioner prov;
  bool wifi = prov.ensureConnected(settings_, /*forcePortal=*/false);
  st.wifiOk = wifi;
  if (wifi) g_wifiFail = 0;

  PriceView view;
  if (wifi) {
    // 5) NTP -> system clock + PCF8563. NTP is AUTHORITATIVE on a connected cycle.
    // The old gate `time(nullptr) < kTimeValid` never waited here, because step 3
    // already seeded a plausible (but wrong) RTC time past kTimeValid -- so a +2h
    // RTC silently won and the board rendered the wrong hour's price. Wait for a
    // REAL SNTP sync via the notification callback, then trust it and refresh the
    // RTC from it. (The AMOLED board has no RTC, so it always waited here and was
    // correct -- that was the whole asymmetry.)
    s_ntpSynced = false;
    sntp_set_time_sync_notification_cb(&onNtpSync);
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
    for (int i = 0; i < 60 && !s_ntpSynced; ++i) delay(250);   // up to 15 s
    if (s_ntpSynced && time(nullptr) > kTimeValid) {
      rtcWrite(time(nullptr));
      g_lastNtpSync = (uint32_t)time(nullptr);   // arms the radio-free cache gate
    }
  }

  time_t now = time(nullptr);
  if (now < kTimeValid) {             // no clock at all -> short retry
    msgScreen(gfx_, "Synchronizacja czasu...", "");
    sleepFor(120);
    return;
  }

  uint32_t nextWake = 3600;
  bool okCycle = false;
  if (wifi) {
    // 6) fetch
    Window w = computeWindow(now);
    PstrykClient client(settings_.apiKey);
    PriceData data;
    // In-cycle retry: a cold-wake TLS read timeout (HTTPClient -11) is transient and
    // almost always clears on a fresh connection, so retry before drawing anything.
    FetchResult res;
    for (int attempt = 0; attempt < 3; ++attempt) {
      res = client.fetch(w.start, w.end, data);
      if (res.status == FetchStatus::Ok ||
          res.status == FetchStatus::AuthError ||
          res.status == FetchStatus::RateLimited) break;   // only retry Network/Parse
      if (attempt < 2) delay(2500);                         // brief backoff between attempts
    }
    if (res.status == FetchStatus::Ok) {
      g_consecFail = 0;
      g_authShown = 0;
      okCycle = true;
      cacheStore(data);                         // feed the radio-free wakes
      view = buildView(data, now);
      // Opportunistic OTA BEFORE the paint: it needs Wi-Fi, and on an update it
      // reboots into the new image -- painting first would waste a full panel
      // flash. Rate-limited to once/day; skipped on a low cell (a ~2 MB
      // download there risks a brownout mid-flash).
      if (!batLow && dueForOtaCheck(g_lastOtaCheck, (uint32_t)now, 24u * 3600u)) {
        g_lastOtaCheck = (uint32_t)now;
        OtaUpdater().runOnce();
      }
      // Radio off for the slow (~2-2.5 s) e-paper refresh; nothing below needs it.
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_OFF);
      struct tm lt; localtime_r(&now, &lt);
      char clk[6]; std::snprintf(clk, sizeof(clk), "%02d:%02d", lt.tm_hour, lt.tm_min);
      st.clockHHMM = clk;                       // local buffer lives until setup() returns
      uint32_t sig = viewSignature(view, st);
      if (sig != g_lastDrawSig) {               // 7) paint (skip a no-op reflash)
        if (localDayOrdinal(now) != g_lastDeepCleanDay) {
          gfx_.requestDeepClean();              // first paint of the day: full deghost
          g_lastDeepCleanDay = localDayOrdinal(now);
        }
        drawDashboard(gfx_, view, st);
        g_lastDrawSig = sig;
      }
    } else if (res.status == FetchStatus::AuthError) {
      // The key can only change via the captive portal (button-held), so hourly
      // re-checks are pointless; paint the hint once and check ~4x/day.
      if (!g_authShown) {
        msgScreen(gfx_, "Blad klucza API", "Przytrzymaj przycisk, aby zmienic");
        g_authShown = 1;
      }
      nextWake = 4u * 3600u;
    } else if (res.status == FetchStatus::RateLimited) {
      msgScreen(gfx_, "Limit zapytan", "Sprobuje pozniej");
      nextWake = res.retryAfterSec > 0 ? (uint32_t)res.retryAfterSec : 1200;
    } else {
      // Transient network/parse error AFTER the in-cycle retries. The e-paper holds its
      // last good image at zero power, so don't wipe it for a brief blip -- surface the
      // error once, when the outage is confirmed (3rd straight failure), and double the
      // retry interval so a sustained outage costs ~1 wake/h instead of 1/min.
      g_consecFail++;
      // Repeated failures right after a "successful" join are the classic
      // symptom of a stale fast-connect lease -- drop it so the next wake does
      // a full DHCP join instead of looping on a dead static config.
      if (g_consecFail >= 2) WiFiProvisioner::forgetAp();
      if (g_consecFail == 3) {
        struct tm lt; localtime_r(&now, &lt);
        char l2[24]; std::snprintf(l2, sizeof(l2), "%02d:%02d", lt.tm_hour, lt.tm_min);
        msgScreen(gfx_, "Blad pobierania", l2);
      }
      nextWake = backoffSeconds(g_consecFail);
    }
  } else {
    // Same shape as the fetch-failure path: keep the last good dashboard through a
    // brief router blip, only admit "no Wi-Fi" on the 3rd straight failed wake.
    g_wifiFail++;
    if (g_wifiFail == 3) msgScreen(gfx_, "Brak Wi-Fi", "Sprobuje ponownie");
    nextWake = backoffSeconds(g_wifiFail);
  }

  // 8) sleep. Anchor the wake math to the CURRENT clock on good cycles: the
  // fetch, an OTA check and the paint together take seconds, and computing the
  // interval from the pre-fetch timestamp wakes the board correspondingly late
  // relative to the next hour turn.
  WiFi.disconnect(true, false);
  if (okCycle) nextWake = secondsUntilNextWake(time(nullptr), view.hasTomorrow);
  sleepFor(nextWake);
}

}  // namespace pstryk
