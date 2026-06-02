#include "app/SleepCycle.h"
#include "render/EpdRenderer.h"
#include "render/Pages.h"   // renderMessage(IRenderer&, ...)
#include <Arduino.h>
#include <esp_sleep.h>

namespace pstryk {

void SleepCycle::setup() {
  Serial.begin(115200);
  delay(100);
  EpdRenderer gfx;
  if (gfx.begin()) {
    renderMessage(gfx, "Pstryk e-paper", "Uruchamianie...");
  }
  esp_sleep_enable_timer_wakeup((uint64_t)3600 * 1000000ULL);  // 1 h
  esp_deep_sleep_start();
}

}  // namespace pstryk
