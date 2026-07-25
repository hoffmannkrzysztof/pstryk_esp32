#include "app/App.h"
#include "core/TimeService.h"
#include "core/PriceLogic.h"
#include "core/RefreshPolicy.h"
#include "render/pins_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include "net/OtaUpdater.h"
#include "net/OtaRollback.h"
#include <esp_task_wdt.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <cstdio>

namespace pstryk {

static const uint32_t kRotateMs = 7000;
static const uint32_t kRedrawMs = 1000;
static const uint32_t kStaleSec = 90 * 60;
static const time_t   kTimeValid = 1700000000;  // sanity threshold for a synced clock

// Pages in rotation order; Jutro is skipped unless tomorrow is held.
static const Page kPages[] = {Page::Teraz, Page::Chart, Page::Extremes, Page::Jutro};

// Consecutive failed display inits. NOINIT (not DATA) so it survives the restart
// it triggers but carries no meaning across a power cycle; the increment below
// normalises whatever garbage a cold boot leaves here.
RTC_NOINIT_ATTR static uint32_t g_bootFail;

#ifdef PSTRYK_OTA_BOOTSTRAP
// Installer image: provision Wi-Fi (+ API key into NVS for the real firmware), then
// force-install the latest signed release for this board and reboot into it. On any
// failure it shows a message and restarts to retry.
void App::runBootstrap() {
  bool needPortal = !settings_.isComplete();
  if (needPortal) {
    char pskLine[40];
    std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                  WiFiProvisioner::portalPassword());
    renderMessage(gfx_, "Konfiguracja", pskLine);
  } else {
    renderMessage(gfx_, "Instalacja", "Pobieranie najnowszej wersji...");
  }
  if (!provisioner_.ensureConnected(settings_, /*forcePortal=*/needPortal)) {
    // Restarting in a loop here makes the portal unreachable (the BOOT poll lives
    // in loop(), which the installer never reaches). There is always a human at
    // the USB port during bootstrap, so escalate straight to the portal.
    renderMessage(gfx_, "WiFi", "Blad polaczenia");
    delay(3000);
    if (!provisioner_.ensureConnected(settings_, /*forcePortal=*/true)) {
      renderMessage(gfx_, "WiFi", "Blad konfiguracji");
      delay(3000);
      ESP.restart();
    }
  }
  OtaUpdater().runOnce(/*force=*/true);  // reboots into the installed release on success
  renderMessage(gfx_, "Blad instalacji", "Sprobuje za chwile");  // only reached on failure
  delay(10000);
  ESP.restart();
}
#endif

// Poll BOOT while waiting out a Wi-Fi backoff in setup(). loop() -- which
// normally polls it -- is not running yet, and without this the captive portal is
// unreachable for the whole time setup() is stuck retrying.
bool App::waitForPortalRequest(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (digitalRead(PIN_BUTTON_BOOT) == LOW) {
      delay(50);
      if (digitalRead(PIN_BUTTON_BOOT) == LOW) return true;
    }
    delay(20);
  }
  return false;
}

void App::setup() {
  Serial.begin(115200);
  delay(200);
  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);   // never relied on the strapping default
  // A failed framebuffer allocation leaves _framebuffer == nullptr and the very
  // next renderMessage() writes 640x180 pixels from address 0: StoreProhibited,
  // reboot, repeat -- a blank screen and no diagnostics. Nothing can be shown
  // without a framebuffer, so say why over Serial and stop instead of looping.
  if (!gfx_.begin()) {
    g_bootFail = (g_bootFail > 8u) ? 1u : g_bootFail + 1u;   // NOINIT: garbage-safe
    Serial.printf("[boot] display init failed (attempt %lu, psram=%d, heap=%lu)\n",
                  (unsigned long)g_bootFail, (int)psramFound(),
                  (unsigned long)esp_get_free_heap_size());
    if (g_bootFail >= 3u) { Serial.println("[boot] giving up"); while (true) delay(1000); }
    delay(2000);
    ESP.restart();
  }
  g_bootFail = 0;
  // A working display + PSRAM is health enough for a just-OTA'd image, and this
  // has to happen BEFORE the Wi-Fi gate below: with verifyRollbackLater() the core
  // does not self-confirm, so any restart while still PENDING_VERIFY rolls the
  // image back. Confirming after Wi-Fi meant one ~20 s AP outage in the first boot
  // after an update silently reverted it. Same order as SleepCycle::setup().
  confirmRunningImageValid();
  renderMessage(gfx_, "Pstryk", "Uruchamianie...");

  settings_.load();

#ifdef PSTRYK_OTA_BOOTSTRAP
  runBootstrap();   // installer image: fetch+flash the latest release, reboot into it
  return;
#endif

  if (!settings_.isComplete()) {
    // First boot: ensureConnected will open the portal -- show its password.
    char pskLine[40];
    std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                  WiFiProvisioner::portalPassword());
    renderMessage(gfx_, "Konfiguracja", pskLine);
  } else {
    renderMessage(gfx_, "WiFi", "Laczenie...");
  }
  // Never restart unconditionally on a failed join. The only BOOT poll that
  // reopens the captive portal lives in loop(), which setup() never reaches, so a
  // persistent mismatch (changed Wi-Fi password, replaced router, board moved to
  // another network) left this display cycling an error screen every ~19 s with no
  // way back short of a USB reflash -- Settings::clear() is called from nowhere.
  // Retry on the shared backoff curve, polling BOOT throughout: a transient outage
  // still self-heals unattended, and a present human can force the portal at once.
  for (uint32_t attempt = 1; !provisioner_.ensureConnected(settings_, false); ++attempt) {
    char l2[24];
    std::snprintf(l2, sizeof(l2), "Blad polaczenia (%lu)", (unsigned long)attempt);
    renderMessage(gfx_, "WiFi", l2);
    if (waitForPortalRequest(backoffSeconds(attempt) * 1000u)) {
      char pskLine[40];
      std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                    WiFiProvisioner::portalPassword());
      renderMessage(gfx_, "Konfiguracja", pskLine);
      if (provisioner_.ensureConnected(settings_, /*forcePortal=*/true)) break;
      attempt = 0;                  // portal abandoned: back to plain joins
    }
  }

  renderMessage(gfx_, "Czas", "Synchronizacja...");
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  timeServiceBegin();
  for (int i = 0; i < 40 && time(nullptr) < kTimeValid; ++i) delay(250);

  doFetch();
  uint32_t now = millis();
  nextRotateAtMs_ = now + kRotateMs;
  lastRedrawMs_ = 0;  // force immediate first redraw
  nextOtaCheckAtMs_ = now + 6u * 60u * 60u * 1000u;   // first OTA check ~6 h after boot

  // Subscribe loopTask to the task watchdog. On this 24/7 board loopTask is not
  // watched by default (the TWDT observes only CPU0's idle task), so a silent
  // wedge inside TLS/HTTP/OTA would freeze the wall display forever while it
  // keeps showing a healthy-looking frame with stale prices. 180 s covers the
  // worst legitimate iteration (double fetch attempt + full OTA transaction);
  // the panic handler reboots and the board self-recovers in ~2 min.
  esp_task_wdt_config_t wdtCfg = {};
  wdtCfg.timeout_ms = 180000;
  wdtCfg.idle_core_mask = 1 << 0;   // keep the default CPU0-idle watch
  wdtCfg.trigger_panic = true;
  esp_task_wdt_reconfigure(&wdtCfg);
  esp_task_wdt_add(nullptr);
}

void App::doFetch() {
  time_t now = time(nullptr);
  if (now < kTimeValid) {                 // clock not synced yet
    nextFetchAtMs_ = millis() + 2000;     // recheck soon; redraw() shows the sync screen
    return;
  }
  Window w = computeWindow(now);
  PstrykClient client(settings_.apiKey);
  PriceData fresh;
  FetchResult res = client.fetch(w.start, w.end, fresh);

  if (res.status == FetchStatus::Ok) {
    data_ = fresh;
    view_ = buildView(data_, now);
    lastViewHour_ = localHour(now);
    dataGen_++;
    haveData_ = view_.hasData;
    lastFetchOk_ = now;
    authError_ = false;
    failCount_ = 0;
    nextFetchAtMs_ = millis() + nextRefreshMs(now, view_.hasTomorrow);
  } else if (res.status == FetchStatus::AuthError) {
    authError_ = true;                     // sticky; redraw() shows the error screen
    nextFetchAtMs_ = millis() + 5u * 60u * 1000u;   // recheck in 5 min
  } else {
    uint32_t backoff;
    if (res.status == FetchStatus::RateLimited)
      backoff = res.retryAfterSec > 0 ? (uint32_t)res.retryAfterSec * 1000u
                                      : 20u * 60u * 1000u;  // cap-safe default (<=3/hr)
    else
      backoff = backoffSeconds(++failCount_) * 1000u;  // network/parse: 60 s -> 1 h
    nextFetchAtMs_ = millis() + backoff;
  }
}

void App::advancePage() {
  for (int i = 0; i < 4; ++i) {
    pageIdx_ = (pageIdx_ + 1) % 4;
    if (kPages[pageIdx_] != Page::Jutro || view_.hasTomorrow) return;
  }
}

void App::redraw() {
  time_t now = time(nullptr);
  bool timeOk = now >= kTimeValid;
  bool stale = timeOk && haveData_ && lastFetchOk_ > 0 &&
               (now - lastFetchOk_) > (time_t)kStaleSec;
  int minute = timeOk ? (int)((now % 3600) / 60) : -1;

  // Dirty check: the 1 s tick is only the CADENCE; actually repainting and
  // QSPI-flushing the full 640x180 frame is worth doing only when something
  // visible changed (minute rollover, page rotation, new data, state flips).
  // Previously 59-60 of every 60 frames were pixel-identical.
  uint32_t sig = ((uint32_t)(minute + 1))
               | ((uint32_t)pageIdx_ << 8)
               | ((uint32_t)(timeOk ? 1 : 0) << 12)
               | ((uint32_t)(authError_ ? 1 : 0) << 13)
               | ((uint32_t)(stale ? 1 : 0) << 14)
               | ((uint32_t)(haveData_ ? 1 : 0) << 15)
               | ((uint32_t)dataGen_ << 16);
  if (sig == lastUiSig_) return;
  lastUiSig_ = sig;

  if (!timeOk) { renderMessage(gfx_, "Czas", "Synchronizacja..."); return; }
  if (authError_) {
    renderMessage(gfx_, "Blad klucza API", "Przytrzymaj BOOT, aby zmienic");
    return;
  }
  char clock[6];
  std::snprintf(clock, sizeof(clock), "%02d:%02d", localHour(now), minute);
  int dotCount = view_.hasTomorrow ? 4 : 3;
  int dotIdx = pageIdx_ < dotCount ? pageIdx_ : dotCount - 1;
  renderPage(gfx_, kPages[pageIdx_], view_, stale, clock, dotIdx, dotCount);
}

void App::loop() {
  uint32_t now = millis();
  esp_task_wdt_reset();

  // BOOT held -> re-open captive portal.
  if (digitalRead(PIN_BUTTON_BOOT) == LOW) {
    delay(50);
    if (digitalRead(PIN_BUTTON_BOOT) == LOW) {
      char pskLine[40];
      std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                    WiFiProvisioner::portalPassword());
      renderMessage(gfx_, "Konfiguracja", pskLine);
      esp_task_wdt_delete(nullptr);   // the portal blocks intentionally (up to 10 min)
      provisioner_.ensureConnected(settings_, /*forcePortal=*/true);
      ESP.restart();
    }
  }

  // Hour boundary: re-derive the view from the in-RAM data so TERAZ, the trend
  // arrow and the chart ring move to the new hour immediately -- buildView gets
  // the live frame and the day split from `wall`, so this also flips the day at
  // midnight when tomorrow's frames were fetched earlier. Previously the view
  // was rebuilt only on a successful fetch, so the headline showed the previous
  // hour's price for up to ~30 min after every HH:00.
  time_t wall = time(nullptr);
  if (haveData_ && wall >= kTimeValid && localHour(wall) != lastViewHour_) {
    view_ = buildView(data_, wall);
    lastViewHour_ = localHour(wall);
    dataGen_++;                       // view content changed without a fetch
    haveData_ = view_.hasData;
    if (pageIdx_ >= (view_.hasTomorrow ? 4 : 3)) pageIdx_ = 0;
    lastRedrawMs_ = 0;                // repaint with the new hour now
  }

  // Night dimming: this is a wall display; 23:00-06:00 local it drops to ~12%
  // backlight duty (still readable, far less glare and panel wear).
  if (wall >= kTimeValid) {
    int h = localHour(wall);
    gfx_.setBacklight((h >= 23 || h < 6) ? 30 : 255);
  }

  if ((int32_t)(now - nextFetchAtMs_) >= 0) doFetch();

  if ((int32_t)(now - nextOtaCheckAtMs_) >= 0) {
    // Re-check daily, aligned with the e-paper board's dueForOtaCheck policy
    // (the first check still runs ~6 h after boot, set in setup()).
    nextOtaCheckAtMs_ = now + 24u * 60u * 60u * 1000u;
    if (WiFi.status() == WL_CONNECTED) {
      // A slow-link OTA download may legitimately exceed the WDT window; it
      // either reboots into the new image or returns here.
      esp_task_wdt_delete(nullptr);
      OtaUpdater().runOnce();
      esp_task_wdt_add(nullptr);
    }
  }

  if ((int32_t)(now - nextRotateAtMs_) >= 0) {
    advancePage();
    nextRotateAtMs_ = now + kRotateMs;
    lastRedrawMs_ = 0;  // redraw immediately after a page change
  }

  if (now - lastRedrawMs_ >= kRedrawMs) {
    redraw();
    lastRedrawMs_ = now;
  }

  // Bounded sleep (vTaskDelay under the hood): lets the idle task run WFI
  // instead of pinning a 240 MHz core at 100% forever. 20 ms still catches the
  // BOOT hold (debounced 50 ms above) and every timer above ticks in seconds.
  delay(20);
}

}  // namespace pstryk
