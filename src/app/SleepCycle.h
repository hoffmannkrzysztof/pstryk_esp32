#pragma once
#include <ctime>
#include "storage/Settings.h"
#include "render/EpdRenderer.h"

namespace pstryk {

class SleepCycle {
 public:
  void setup();    // runs one full cycle, then deep-sleeps
  void loop() {}

 private:
  bool buttonHeld(uint32_t ms);          // true if BTN held for >= ms
  int  readBatteryPercent(bool& low);    // reads GPIO14 (call before Wi-Fi)
  bool rtcRead(time_t& outLocalEpoch);   // PCF8563 -> epoch (false if invalid)
  void rtcWrite(time_t localEpoch);      // epoch -> PCF8563
  void sleepFor(uint32_t seconds);       // arm timer + button wake, deep sleep
#ifdef PSTRYK_OTA_BOOTSTRAP
  void runBootstrap();                   // installer build: provision, force-install latest, reboot
#endif

  EpdRenderer gfx_;
  Settings    settings_;
};

}  // namespace pstryk
