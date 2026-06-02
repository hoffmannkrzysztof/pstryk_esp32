#include "app/SleepCycle.h"
#include "render/EpdRenderer.h"
#include "render/EpdDashboard.h"
#include "net/PstrykClient.h"
#include "net/WiFiProvisioner.h"
#include "core/TimeService.h"
#include "core/PriceLogic.h"
#include "core/RefreshPolicy.h"
#include "core/Battery.h"
#include "core/PriceData.h"
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include <cstdio>
#include <cstring>
extern "C" {
#include "epd_driver.h"   // epd_poweron / epd_poweroff
}

namespace pstryk {

static const int      PIN_BTN  = 21;   // user button (active LOW)
static const int      PIN_BATT = 14;   // battery ADC (ADC2) via 2:1 divider
static const uint8_t  PCF8563_ADDR = 0x51;
static const int      I2C_SDA = 18, I2C_SCL = 17;
static const time_t   kTimeValid = 1700000000;

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

int SleepCycle::readBatteryPercent(bool& low) {
  epd_poweron();             // POWER_EN gates the battery divider
  delay(10);
  long acc = 0;
  for (int i = 0; i < 16; ++i) acc += analogReadMilliVolts(PIN_BATT);
  epd_poweroff();
  float pinMv = acc / 16.0f;
  float volts = batteryVoltsFromPinMv(pinMv);
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
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN, 0);  // button (active low) wake
  esp_deep_sleep_start();
}

void SleepCycle::setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(PIN_BTN, INPUT_PULLUP);
  timeServiceBegin();                 // install Warsaw TZ for mktime/localtime
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!gfx_.begin()) { sleepFor(3600); return; }

  // 1) reconfigure if button held at boot, or if no saved config
  settings_.load();
  if (buttonHeld(3000) || !settings_.isComplete()) {
    drawMessage(gfx_, "Konfiguracja", "Polacz z 'Pstryk-Setup'");
    WiFiProvisioner prov;
    prov.ensureConnected(settings_, /*forcePortal=*/true);
    sleepFor(2);                      // wake immediately to run a normal cycle
    return;
  }

  // 2) battery BEFORE Wi-Fi (ADC2 conflicts with Wi-Fi)
  bool batLow = false;
  int batPct = readBatteryPercent(batLow);

  // 3) seed clock from RTC so we have time even if Wi-Fi fails
  time_t rtcEpoch;
  if (rtcRead(rtcEpoch)) {
    struct timeval tv = { rtcEpoch, 0 }; settimeofday(&tv, nullptr);
  }

  // 4) Wi-Fi
  EpdStatus st; st.batteryPct = batPct; st.batteryLow = batLow;
  WiFiProvisioner prov;
  bool wifi = prov.ensureConnected(settings_, /*forcePortal=*/false);
  st.wifiOk = wifi;

  PriceView view;
  if (wifi) {
    // 5) NTP -> system clock + PCF8563
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com");
    for (int i = 0; i < 40 && time(nullptr) < kTimeValid; ++i) delay(250);
    if (time(nullptr) > kTimeValid) rtcWrite(time(nullptr));
  }

  time_t now = time(nullptr);
  if (now < kTimeValid) {             // no clock at all -> short retry
    drawMessage(gfx_, "Synchronizacja czasu...", "");
    sleepFor(120);
    return;
  }

  uint32_t nextWake = 3600;
  if (wifi) {
    // 6) fetch
    Window w = computeWindow(now);
    PstrykClient client(settings_.apiKey);
    PriceData data;
    FetchResult res = client.fetch(w.start, w.end, data);
    if (res.status == FetchStatus::Ok) {
      view = buildView(data, now);
      char clk[6]; std::snprintf(clk, sizeof(clk), "%02d:%02d", localHour(now),
                                 (int)((now % 3600) / 60));
      static char clkbuf[6]; std::memcpy(clkbuf, clk, sizeof(clkbuf));
      st.clockHHMM = clkbuf;
      drawDashboard(gfx_, view, st);            // 7) paint
      nextWake = secondsUntilNextWake(now, view.hasTomorrow);
    } else if (res.status == FetchStatus::AuthError) {
      drawMessage(gfx_, "Blad klucza API", "Przytrzymaj przycisk, aby zmienic");
      nextWake = 1800;
    } else if (res.status == FetchStatus::RateLimited) {
      drawMessage(gfx_, "Limit zapytan", "Sprobuje pozniej");
      nextWake = res.retryAfterSec > 0 ? (uint32_t)res.retryAfterSec : 1200;
    } else {
      char l2[24]; std::snprintf(l2, sizeof(l2), "%02d:%02d", localHour(now),
                                 (int)((now % 3600) / 60));
      drawMessage(gfx_, "Blad pobierania", l2);
      nextWake = 300;
    }
  } else {
    drawMessage(gfx_, "Brak Wi-Fi", "Sprobuje ponownie");
    nextWake = 300;
  }

  // 8) sleep
  WiFi.disconnect(true, false);
  sleepFor(nextWake);
}

}  // namespace pstryk
