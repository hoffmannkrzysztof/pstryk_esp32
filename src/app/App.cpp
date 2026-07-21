#include "app/App.h"
#include "core/TimeService.h"
#include "core/PriceLogic.h"
#include "core/RefreshPolicy.h"
#include "render/pins_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include "net/OtaUpdater.h"
#include "net/OtaRollback.h"
#include <cstdio>

namespace pstryk {

static const uint32_t kRotateMs = 7000;
static const uint32_t kRedrawMs = 1000;
static const uint32_t kStaleSec = 90 * 60;
static const time_t   kTimeValid = 1700000000;  // sanity threshold for a synced clock

// Pages in rotation order; Jutro is skipped unless tomorrow is held.
static const Page kPages[] = {Page::Teraz, Page::Chart, Page::Extremes, Page::Jutro};

#ifdef PSTRYK_OTA_BOOTSTRAP
// Installer image: provision Wi-Fi (+ API key into NVS for the real firmware), then
// force-install the latest signed release for this board and reboot into it. On any
// failure it shows a message and restarts to retry.
void App::runBootstrap() {
  renderMessage(gfx_, "Instalacja", "Pobieranie najnowszej wersji...");
  if (!provisioner_.ensureConnected(settings_, /*forcePortal=*/!settings_.isComplete())) {
    renderMessage(gfx_, "WiFi", "Blad polaczenia");
    delay(3000);
    ESP.restart();
  }
  OtaUpdater().runOnce(/*force=*/true);  // reboots into the installed release on success
  renderMessage(gfx_, "Blad instalacji", "Sprobuje za chwile");  // only reached on failure
  delay(10000);
  ESP.restart();
}
#endif

void App::setup() {
  Serial.begin(115200);
  delay(200);
  gfx_.begin();
  renderMessage(gfx_, "Pstryk", "Uruchamianie...");

  settings_.load();

#ifdef PSTRYK_OTA_BOOTSTRAP
  runBootstrap();   // installer image: fetch+flash the latest release, reboot into it
  return;
#endif

  renderMessage(gfx_, "WiFi", "Laczenie / konfiguracja...");
  if (!provisioner_.ensureConnected(settings_, /*forcePortal=*/false)) {
    renderMessage(gfx_, "WiFi", "Blad polaczenia");
    delay(3000);
    ESP.restart();
  }
  // Display is up and Wi-Fi connected -> a just-OTA'd image is healthy; keep it.
  confirmRunningImageValid();

  renderMessage(gfx_, "Czas", "Synchronizacja...");
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  timeServiceBegin();
  for (int i = 0; i < 40 && time(nullptr) < kTimeValid; ++i) delay(250);

  doFetch();
  uint32_t now = millis();
  nextRotateAtMs_ = now + kRotateMs;
  lastRedrawMs_ = 0;  // force immediate first redraw
  nextOtaCheckAtMs_ = now + 6u * 60u * 60u * 1000u;   // first OTA check ~6 h after boot
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
  if (now < kTimeValid) { renderMessage(gfx_, "Czas", "Synchronizacja..."); return; }
  if (authError_) {
    renderMessage(gfx_, "Blad klucza API", "Przytrzymaj BOOT, aby zmienic");
    return;
  }
  bool stale = haveData_ && lastFetchOk_ > 0 &&
               (now - lastFetchOk_) > (time_t)kStaleSec;
  char clock[6];
  std::snprintf(clock, sizeof(clock), "%02d:%02d", localHour(now),
                (int)((now % 3600) / 60));
  int dotCount = view_.hasTomorrow ? 4 : 3;
  int dotIdx = pageIdx_ < dotCount ? pageIdx_ : dotCount - 1;
  renderPage(gfx_, kPages[pageIdx_], view_, stale, clock, dotIdx, dotCount);
}

void App::loop() {
  uint32_t now = millis();

  // BOOT held -> re-open captive portal.
  if (digitalRead(PIN_BUTTON_BOOT) == LOW) {
    delay(50);
    if (digitalRead(PIN_BUTTON_BOOT) == LOW) {
      renderMessage(gfx_, "Konfiguracja", "Polacz z 'Pstryk-Setup'");
      provisioner_.ensureConnected(settings_, /*forcePortal=*/true);
      ESP.restart();
    }
  }

  if ((int32_t)(now - nextFetchAtMs_) >= 0) doFetch();

  if ((int32_t)(now - nextOtaCheckAtMs_) >= 0) {
    nextOtaCheckAtMs_ = now + 6u * 60u * 60u * 1000u;   // re-check every ~6 h
    if (WiFi.status() == WL_CONNECTED) OtaUpdater().runOnce();  // reboots on update
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
}

}  // namespace pstryk
