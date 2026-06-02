#include "app/SleepCycle.h"
#include "render/EpdRenderer.h"
#include "render/Pages.h"   // renderMessage(IRenderer&, ...)
#include "render/EpdDashboard.h"
#include <Arduino.h>
#include <esp_sleep.h>

namespace pstryk {

void SleepCycle::setup() {
  Serial.begin(115200);
  delay(100);
  EpdRenderer gfx;
  if (gfx.begin()) {
    PriceView v;
    v.hasData = true;
    v.currentBuy = 0.49f; v.currentSell = 0.18f; v.currentHour = 14;
    v.currentBelowAvg = true; v.nextTrend = Trend::Up; v.nextBuy = 0.51f;
    v.nextHour = 15; v.todayAvg = 0.60f; v.liveIndex = 14;
    for (int i = 0; i < 24; ++i) { Bar b; b.hour = i; b.price = 0.40f + 0.02f * ((i * 7) % 13); v.today.push_back(b); }
    v.hasTomorrow = false;
    EpdStatus st; st.clockHHMM = "14:00"; st.batteryPct = 87; st.wifiOk = true;
    drawDashboard(gfx, v, st);
  }
  esp_sleep_enable_timer_wakeup((uint64_t)3600 * 1000000ULL);  // 1 h
  esp_deep_sleep_start();
}

}  // namespace pstryk
