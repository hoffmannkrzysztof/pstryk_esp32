#include "app/App.h"
#include "core/TimeService.h"
#include "core/PriceLogic.h"
#include "core/RefreshPolicy.h"
#include "render/pins_config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>

namespace pstryk {

static const uint32_t kRotateMs = 7000;
static const uint32_t kRedrawMs = 1000;
static const uint32_t kStaleSec = 90 * 60;

// Pages in rotation order; Jutro is skipped unless tomorrow is held.
static const Page kPages[] = {Page::Teraz, Page::Chart, Page::Extremes, Page::Jutro};

void App::setup() {
  Serial.begin(115200);
  delay(200);
  gfx_.begin();
  renderMessage(gfx_, "Pstryk", "Uruchamianie...");

  settings_.load();

  renderMessage(gfx_, "WiFi", "Laczenie / konfiguracja...");
  if (!provisioner_.ensureConnected(settings_, /*forcePortal=*/false)) {
    renderMessage(gfx_, "WiFi", "Blad polaczenia");
    delay(3000);
    ESP.restart();
  }

  renderMessage(gfx_, "Czas", "Synchronizacja...");
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
  timeServiceBegin();
  for (int i = 0; i < 40 && time(nullptr) < 1700000000; ++i) delay(250);

  doFetch();
  uint32_t now = millis();
  nextRotateAtMs_ = now + kRotateMs;
  lastRedrawMs_ = 0;  // force immediate first redraw
}

void App::doFetch() {
  time_t now = time(nullptr);
  Window w = computeWindow(now);
  PstrykClient client(settings_.apiKey);
  PriceData fresh;
  FetchResult res = client.fetch(w.start, w.end, fresh);

  if (res.status == FetchStatus::Ok) {
    data_ = fresh;
    view_ = buildView(data_, now);
    haveData_ = view_.hasData;
    lastFetchOk_ = now;
    nextFetchAtMs_ = millis() + nextRefreshMs(now, view_.hasTomorrow);
  } else if (res.status == FetchStatus::AuthError) {
    renderMessage(gfx_, "Blad klucza API", "Przytrzymaj BOOT, aby zmienic");
    delay(4000);
    nextFetchAtMs_ = millis() + 60u * 1000u;  // retry in a minute
  } else {
    // Rate-limited / network / parse: keep last data, back off.
    uint32_t backoff = (res.status == FetchStatus::RateLimited && res.retryAfterSec > 0)
                         ? (uint32_t)res.retryAfterSec * 1000u
                         : 60u * 1000u;
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
  bool stale = haveData_ && lastFetchOk_ > 0 &&
               (time(nullptr) - lastFetchOk_) > (time_t)kStaleSec;
  char clock[6] = "";
  time_t now = time(nullptr);
  if (now > 1700000000) std::snprintf(clock, sizeof(clock), "%02d:%02d", localHour(now),
                                      (int)((now % 3600) / 60));
  // Count pages currently in rotation (Jutro optional).
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
