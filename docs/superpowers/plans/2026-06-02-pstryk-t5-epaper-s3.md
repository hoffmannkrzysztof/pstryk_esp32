# Pstryk e-paper (LilyGo T5 4.7″ S3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a battery-powered LilyGo T5 4.7″ ePaper S3 board variant that wakes hourly (tighter at midday), fetches Pstryk prices, paints one static 960×540 grayscale screen, and deep-sleeps — reusing the entire board-independent core.

**Architecture:** The core (`PstrykClient`, `PriceData`, `PriceLogic`, `TimeService`, `Settings`, `WiFiProvisioner`, `PriceView`) is reused verbatim. New, board-specific pieces: `EpdRenderer` (implements the existing `IRenderer` over a vendored epdiy grayscale framebuffer), `EpdDashboard` (single-screen Layout-C paint), and `SleepCycle` (the deep-sleep orchestrator that replaces `App`'s loop). Two new pure, host-tested helpers: `RefreshPolicy::secondsUntilNextWake` and `core/Battery`. `main.cpp` picks `App` vs `SleepCycle` by a build macro; PlatformIO `build_src_filter` keeps each board's render layer separate.

**Tech Stack:** PlatformIO / Arduino-ESP32 (ESP32-S3R8), vendored LilyGo `LilyGo-EPD47` (esp32s3 branch) e-paper driver, ArduinoJson, WiFiManager, Unity (native host tests). Spec: `docs/superpowers/specs/2026-06-02-pstryk-t5-epaper-s3-design.md`.

**Implementation notes / refinements vs spec:**
- PCF8563 RTC is accessed **directly over `Wire`** (BCD registers) rather than via SensorLib — fewer deps, deterministic. It stores **local** wall time, so `mktime`/`localtime_r` (with the already-installed Warsaw TZ) handle conversion and DST.
- The hero price is drawn as a custom **7-segment block number** via `IRenderer::fillRect` (no scalable-font dependency); all other text uses the bundled `FiraSans` font, which the old LilyGo driver renders as dark glyphs (perfect for a white-background e-paper UI).
- Tasks 3–5 are device firmware that cannot be host-tested; their per-task verification is **`pio run -e t5_epaper_s3` compiles+links cleanly**, with on-device behavior validated in the Task 6 smoke checklist.

---

## File structure

| File | Responsibility | Status |
|------|----------------|--------|
| `src/core/RefreshPolicy.{h,cpp}` | + `secondsUntilNextWake(now,hasTomorrow)` pure wake scheduler | modify |
| `src/core/Battery.{h,cpp}` | pure battery voltage→% helpers | create |
| `test/test_refresh/test_refresh.cpp` | + wake-schedule tests | modify |
| `test/test_battery/test_battery.cpp` | battery helper tests | create |
| `board/T5-ePaper-S3.json` | board definition (16MB QIO flash, OPI PSRAM, native USB) | create |
| `lib/EPD47/` | vendored LilyGo esp32s3 EPD driver + `library.json` | create |
| `src/render/EpdRenderer.{h,cpp}` | `IRenderer` over epdiy grayscale framebuffer | create |
| `src/render/EpdDashboard.{h,cpp}` | single-screen Layout-C paint (hero + dual charts + status) | create |
| `src/app/SleepCycle.{h,cpp}` | deep-sleep orchestrator (battery, RTC, wifi, fetch, paint, sleep, button) | create |
| `src/main.cpp` | pick `App` vs `SleepCycle` by board macro | modify |
| `platformio.ini` | + `[env:t5_epaper_s3]`; `build_src_filter`/`lib_ignore` on existing envs | modify |

---

## Task 1: `RefreshPolicy::secondsUntilNextWake` (pure, TDD)

**Files:**
- Modify: `src/core/RefreshPolicy.h`, `src/core/RefreshPolicy.cpp`
- Test: `test/test_refresh/test_refresh.cpp`

Semantics: return **seconds** to sleep. Base = time to the next top-of-hour + 5 s guard. If local hour is in **[12,16)** and tomorrow is not yet held, cap the result at **1800 s** (30 min) so tomorrow's prices appear sooner.

- [ ] **Step 1: Write the failing tests** — append to `test/test_refresh/test_refresh.cpp` (before `main`):

```cpp
void test_wake_to_top_of_hour_plus_guard() {
  time_t now = parseIso8601Utc("2026-06-02T07:20:00Z");  // 09:20 local, 40 min to 10:00
  // 40*60 remaining to top of hour, +5 s guard
  TEST_ASSERT_EQUAL_UINT32(40u * 60u + 5u, secondsUntilNextWake(now, false));
}

void test_wake_exactly_on_hour_waits_full_hour() {
  time_t now = parseIso8601Utc("2026-06-02T07:00:00Z");  // 09:00 local exactly
  TEST_ASSERT_EQUAL_UINT32(60u * 60u + 5u, secondsUntilNextWake(now, false));
}

void test_wake_capped_at_30min_midday_without_tomorrow() {
  time_t now = parseIso8601Utc("2026-06-02T11:05:00Z");  // 13:05 local, 55 min to 14:00
  // would be 55min, but midday + no tomorrow caps to 30min
  TEST_ASSERT_EQUAL_UINT32(30u * 60u, secondsUntilNextWake(now, false));
}

void test_wake_not_capped_midday_when_tomorrow_present() {
  time_t now = parseIso8601Utc("2026-06-02T11:05:00Z");  // 13:05 local
  TEST_ASSERT_EQUAL_UINT32(55u * 60u + 5u, secondsUntilNextWake(now, true));
}

void test_wake_not_capped_before_noon() {
  time_t now = parseIso8601Utc("2026-06-02T09:05:00Z");  // 11:05 local, 55 min to 12:00
  TEST_ASSERT_EQUAL_UINT32(55u * 60u + 5u, secondsUntilNextWake(now, false));
}
```

And register them in `main` (add `RUN_TEST(...)` lines for each):

```cpp
  RUN_TEST(test_wake_to_top_of_hour_plus_guard);
  RUN_TEST(test_wake_exactly_on_hour_waits_full_hour);
  RUN_TEST(test_wake_capped_at_30min_midday_without_tomorrow);
  RUN_TEST(test_wake_not_capped_midday_when_tomorrow_present);
  RUN_TEST(test_wake_not_capped_before_noon);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `pio test -e native -f test_refresh`
Expected: FAIL — `secondsUntilNextWake` not declared.

- [ ] **Step 3: Declare in `src/core/RefreshPolicy.h`** — add below `nextRefreshMs`:

```cpp
// Seconds to deep-sleep until the next refresh: time to the next top-of-hour
// (+5 s guard), capped at 30 min during 12:00-16:00 local while tomorrow is absent.
uint32_t secondsUntilNextWake(time_t now, bool hasTomorrow);
```

- [ ] **Step 4: Implement in `src/core/RefreshPolicy.cpp`** — append inside the namespace:

```cpp
uint32_t secondsUntilNextWake(time_t now, bool hasTomorrow) {
  long secsIntoHour = (long)(now % 3600);
  uint32_t toTop = (uint32_t)(3600 - secsIntoHour);  // 1..3600
  uint32_t wake = toTop + 5u;                         // small guard past the turn
  int h = localHour(now);
  if (h >= 12 && h < 16 && !hasTomorrow && wake > 30u * 60u) wake = 30u * 60u;
  return wake;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `pio test -e native -f test_refresh`
Expected: PASS (all 9 tests in the file).

- [ ] **Step 6: Commit**

```bash
git add src/core/RefreshPolicy.h src/core/RefreshPolicy.cpp test/test_refresh/test_refresh.cpp
git commit -m "feat(core): add secondsUntilNextWake deep-sleep scheduler"
```

---

## Task 2: `core/Battery` voltage→percent (pure, TDD)

**Files:**
- Create: `src/core/Battery.h`, `src/core/Battery.cpp`
- Test: `test/test_battery/test_battery.cpp`

`batteryVoltsFromPinMv(pinMv)` applies the 2:1 divider (pin volts ×2). `batteryPercent(volts)` maps a Li-ion cell linearly: 4.20 V→100%, 3.30 V→0%, clamped.

- [ ] **Step 1: Write the failing test** — create `test/test_battery/test_battery.cpp`:

```cpp
#include <unity.h>
#include "core/Battery.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_volts_from_pin_mv_applies_2to1_divider() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.20f, batteryVoltsFromPinMv(2100.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, batteryVoltsFromPinMv(0.0f));
}

void test_percent_endpoints_and_midpoint() {
  TEST_ASSERT_EQUAL_INT(100, batteryPercent(4.20f));
  TEST_ASSERT_EQUAL_INT(0, batteryPercent(3.30f));
  TEST_ASSERT_EQUAL_INT(50, batteryPercent(3.75f));
}

void test_percent_clamps() {
  TEST_ASSERT_EQUAL_INT(100, batteryPercent(4.50f));
  TEST_ASSERT_EQUAL_INT(0, batteryPercent(3.00f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_volts_from_pin_mv_applies_2to1_divider);
  RUN_TEST(test_percent_endpoints_and_midpoint);
  RUN_TEST(test_percent_clamps);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_battery`
Expected: FAIL — `core/Battery.h` not found.

- [ ] **Step 3: Create `src/core/Battery.h`**:

```cpp
#pragma once

namespace pstryk {

// Pin millivolts (already-calibrated ADC reading at GPIO14) -> battery volts.
// The board has a 2:1 divider, so battery = pin * 2.
float batteryVoltsFromPinMv(float pinMv);

// Li-ion cell volts -> 0..100 %, linear between 3.30 V (0%) and 4.20 V (100%).
int batteryPercent(float volts);

}  // namespace pstryk
```

- [ ] **Step 4: Create `src/core/Battery.cpp`**:

```cpp
#include "core/Battery.h"

namespace pstryk {

float batteryVoltsFromPinMv(float pinMv) { return pinMv * 2.0f / 1000.0f; }

int batteryPercent(float volts) {
  const float lo = 3.30f, hi = 4.20f;
  float pct = (volts - lo) / (hi - lo) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return (int)(pct + 0.5f);
}

}  // namespace pstryk
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_battery`
Expected: PASS (3 tests).

- [ ] **Step 6: Commit**

```bash
git add src/core/Battery.h src/core/Battery.cpp test/test_battery/test_battery.cpp
git commit -m "feat(core): add pure battery voltage->percent helpers"
```

---

## Task 3: Board setup, vendored EPD driver, `EpdRenderer`, walking skeleton

This task makes `[env:t5_epaper_s3]` build and link end-to-end: it adds the board JSON, vendors the LilyGo driver, wires the PlatformIO env with source filters, implements the **complete** `EpdRenderer`, and adds a **minimal** `SleepCycle` that paints a boot message and deep-sleeps for an hour. After this task the board can be flashed and will show text.

**Files:**
- Create: `board/T5-ePaper-S3.json`, `lib/EPD47/` (vendored), `src/render/EpdRenderer.h`, `src/render/EpdRenderer.cpp`, `src/app/SleepCycle.h`, `src/app/SleepCycle.cpp`
- Modify: `platformio.ini`, `src/main.cpp`

- [ ] **Step 1: Vendor the LilyGo EPD driver**

Run (from repo root):

```bash
git clone --depth 1 -b esp32s3 https://github.com/Xinyuan-LilyGO/LilyGo-EPD47.git /tmp/lilygo-epd47
mkdir -p lib/EPD47/src
cp /tmp/lilygo-epd47/src/*.c /tmp/lilygo-epd47/src/*.h lib/EPD47/src/ 2>/dev/null
cp -r /tmp/lilygo-epd47/src/firasans.h lib/EPD47/src/ 2>/dev/null
# Copy the whole src tree to be safe (driver, i2s/rmt buses, panel, fonts):
cp -r /tmp/lilygo-epd47/src/. lib/EPD47/src/
cp /tmp/lilygo-epd47/LICENSE lib/EPD47/ 2>/dev/null || true
ls lib/EPD47/src
```

Expected: `epd_driver.h`, `epd_driver.c` (or `ed047tc1.c`), `i2s_data_bus.{c,h}`, `rmt_pulse.{c,h}`, `firasans.h`, plus supporting headers. **The vendored `epd_driver.h` is the source of truth for exact function signatures** used below (LilyGo's old API: `epd_init`, `epd_poweron`, `epd_poweroff`, `epd_clear`, `epd_fill_rect`, `epd_draw_rect`, `epd_draw_line`, `epd_fill_circle`, `epd_draw_grayscale_image`, `epd_full_screen`, `EPD_WIDTH`, `EPD_HEIGHT`, and font fns `write_string`/`writeln`/`get_text_bounds` with the `GFXfont FiraSans`).

- [ ] **Step 2: Create `lib/EPD47/library.json`**:

```json
{
  "name": "EPD47",
  "version": "0.1.0-lilygo-esp32s3",
  "description": "Vendored LilyGo-EPD47 (esp32s3 branch) e-paper driver for ED047TC1 on ESP32-S3. Source: github.com/Xinyuan-LilyGO/LilyGo-EPD47 (epdiy-derived).",
  "frameworks": "arduino",
  "platforms": "espressif32",
  "build": { "srcDir": "src" }
}
```

- [ ] **Step 3: Create `board/T5-ePaper-S3.json`**:

```json
{
  "build": {
    "arduino": { "partitions": "default_16MB.csv", "memory_type": "qio_opi" },
    "core": "esp32",
    "extra_flags": [
      "-DARDUINO_USB_MODE=1",
      "-DARDUINO_USB_CDC_ON_BOOT=1",
      "-DBOARD_HAS_PSRAM"
    ],
    "f_cpu": "240000000L",
    "f_flash": "80000000L",
    "flash_mode": "qio",
    "hwids": [["0x303A", "0x1001"]],
    "mcu": "esp32s3",
    "memory_type": "qio_opi",
    "psram_type": "opi",
    "variant": "esp32s3"
  },
  "connectivity": ["wifi", "bluetooth"],
  "frameworks": ["arduino"],
  "name": "LilyGo T5 4.7 ePaper S3",
  "upload": {
    "flash_size": "16MB",
    "maximum_ram_size": 327680,
    "maximum_size": 16777216,
    "require_upload_port": true,
    "speed": 921600
  },
  "url": "https://www.lilygo.cc/products/t5-4-7-inch-e-paper-v2-3",
  "vendor": "LilyGo"
}
```

- [ ] **Step 4: Edit `platformio.ini`** — add `build_src_filter` + `lib_ignore` to the existing `[env:tdisplay_long]` (so it does not compile the e-paper sources or the EPD lib), and add the new env. The `[env:native]` block stays unchanged.

In `[env:tdisplay_long]`, add these two keys (anywhere in the block):

```ini
build_src_filter =
    +<*>
    -<render/EpdRenderer.cpp>
    -<render/EpdDashboard.cpp>
    -<app/SleepCycle.cpp>
lib_ignore = EPD47
```

Then append a new env at the end of the file:

```ini
[env:t5_epaper_s3]
platform = espressif32
board = T5-ePaper-S3
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
board_build.partitions = default_16MB.csv
board_build.arduino.memory_type = qio_opi
board_build.memory_type = qio_opi
build_flags =
    -std=gnu++17
    -I src
    -DCORE_DEBUG_LEVEL=3
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DLILYGO_T5_EPD47_S3
    -DPSTRYK_BOARD_EPAPER
build_src_filter =
    +<*>
    -<render/LongRenderer.cpp>
    -<render/Pages.cpp>
    -<app/App.cpp>
lib_deps =
    bblanchon/ArduinoJson@^7.2.0
    tzapu/WiFiManager@^2.0.17
```

- [ ] **Step 5: Create `src/render/EpdRenderer.h`**:

```cpp
#pragma once
#include "render/IRenderer.h"
#include <cstdint>

namespace pstryk {

// IRenderer over the vendored LilyGo EPD47 driver. Draws into a 4bpp grayscale
// framebuffer in PSRAM; present() does a full panel refresh. Colors are grayscale
// luminance 0(black)..255(white). Text renders as dark glyphs (bundled FiraSans);
// the `size` argument is advisory (single bundled font), large numerics are drawn
// by EpdDashboard via fillRect, so text() here is used for labels/messages.
class EpdRenderer : public IRenderer {
 public:
  bool begin() override;
  int  width() override  { return 960; }
  int  height() override { return 540; }

  uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) override;
  void clear(uint16_t color) override;
  void fillRect(int x, int y, int w, int h, uint16_t color) override;
  void drawRect(int x, int y, int w, int h, uint16_t color) override;
  void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) override;
  void drawLine(int x0, int y0, int x1, int y1, uint16_t color) override;
  void text(int x, int y, const char* s, uint16_t color, int size) override;
  int  textWidth(const char* s, int size) override;
  int  textHeight(int size) override;
  void present() override;

 private:
  uint8_t* fb_ = nullptr;  // EPD_WIDTH*EPD_HEIGHT/2 bytes in PSRAM
};

}  // namespace pstryk
```

- [ ] **Step 6: Create `src/render/EpdRenderer.cpp`**:

```cpp
#include "render/EpdRenderer.h"
#include <Arduino.h>
#include <cstring>
extern "C" {
#include "epd_driver.h"
#include "firasans.h"   // const GFXfont FiraSans
}

namespace pstryk {

static const int FB_BYTES = EPD_WIDTH * EPD_HEIGHT / 2;

bool EpdRenderer::begin() {
  epd_init();
  fb_ = (uint8_t*)ps_calloc(sizeof(uint8_t), FB_BYTES);
  if (!fb_) return false;
  std::memset(fb_, 0xFF, FB_BYTES);  // white
  return true;
}

uint16_t EpdRenderer::rgb(uint8_t r, uint8_t g, uint8_t b) {
  // luminance 0..255 (BT.601); stored directly as the epd grayscale value
  return (uint16_t)((r * 77 + g * 150 + b * 29) >> 8);
}

void EpdRenderer::clear(uint16_t color) {
  uint8_t g4 = (uint8_t)(color & 0xFF) >> 4;
  std::memset(fb_, (g4 << 4) | g4, FB_BYTES);
}

void EpdRenderer::fillRect(int x, int y, int w, int h, uint16_t c) {
  epd_fill_rect(x, y, w, h, (uint8_t)(c & 0xFF), fb_);
}
void EpdRenderer::drawRect(int x, int y, int w, int h, uint16_t c) {
  epd_draw_rect(x, y, w, h, (uint8_t)(c & 0xFF), fb_);
}
void EpdRenderer::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  uint8_t col = (uint8_t)(c & 0xFF);
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  epd_fill_rect(x + r, y, w - 2 * r, h, col, fb_);
  epd_fill_rect(x, y + r, w, h - 2 * r, col, fb_);
  epd_fill_circle(x + r, y + r, r, col, fb_);
  epd_fill_circle(x + w - r - 1, y + r, r, col, fb_);
  epd_fill_circle(x + r, y + h - r - 1, r, col, fb_);
  epd_fill_circle(x + w - r - 1, y + h - r - 1, r, col, fb_);
}
void EpdRenderer::drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
  epd_draw_line(x0, y0, x1, y1, (uint8_t)(c & 0xFF), fb_);
}

void EpdRenderer::text(int x, int y, const char* s, uint16_t /*color*/, int /*size*/) {
  // FiraSans renders dark glyphs; baseline-ish placement: shift down by ~ascent.
  int cx = x, cy = y + textHeight(1);
  write_string((GFXfont*)&FiraSans, (char*)s, &cx, &cy, fb_);
}

int EpdRenderer::textWidth(const char* s, int /*size*/) {
  int x = 0, y = 0, x1 = 0, y1 = 0, w = 0, h = 0;
  get_text_bounds((GFXfont*)&FiraSans, (char*)s, &x, &y, &x1, &y1, &w, &h);
  return w;
}
int EpdRenderer::textHeight(int /*size*/) {
  return FiraSans.advance_y;  // line height of the bundled font
}

void EpdRenderer::present() {
  Rect_t area = epd_full_screen();
  epd_poweron();
  epd_clear();  // reset panel to white -> removes ghosting before the new image
  epd_draw_grayscale_image(area, fb_);
  epd_poweroff();
}

}  // namespace pstryk
```

> If the vendored header names a symbol differently (e.g. `writeln` instead of `write_string`, or `FiraSans.advance_y` is `.AdvanceY`), match the header. These are the only external symbols this file touches.

- [ ] **Step 7: Create minimal `src/app/SleepCycle.h`**:

```cpp
#pragma once

namespace pstryk {

// Deep-sleep orchestrator for the e-paper board. setup() runs one full
// wake->fetch->paint->sleep cycle and ends in deep sleep; loop() is never reached.
class SleepCycle {
 public:
  void setup();
  void loop() {}
};

}  // namespace pstryk
```

- [ ] **Step 8: Create minimal `src/app/SleepCycle.cpp`** (walking skeleton — boot message + 1 h sleep):

```cpp
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
```

- [ ] **Step 9: Edit `src/main.cpp`** to select the orchestrator by board macro:

```cpp
#include <Arduino.h>

#if defined(PSTRYK_BOARD_EPAPER)
#include "app/SleepCycle.h"
pstryk::SleepCycle app;
#else
#include "app/App.h"
pstryk::App app;
#endif

void setup() { app.setup(); }
void loop()  { app.loop(); }
```

- [ ] **Step 10: Verify the e-paper env builds and links**

Run: `pio run -e t5_epaper_s3`
Expected: SUCCESS — compiles `EpdRenderer`, `SleepCycle`, the vendored EPD47 lib, and links. (`renderMessage` comes from the reused `render/Pages.cpp`, which IS compiled for this env — it only uses `IRenderer`.)

- [ ] **Step 11: Verify the Long board env still builds (regression)**

Run: `pio run -e tdisplay_long`
Expected: SUCCESS — the new e-paper sources and the EPD47 lib are excluded by `build_src_filter`/`lib_ignore`.

- [ ] **Step 12: Run the host tests (regression)**

Run: `pio test -e native`
Expected: PASS (all existing + Task 1/2 tests).

- [ ] **Step 13: Commit**

```bash
git add board/T5-ePaper-S3.json lib/EPD47 platformio.ini src/render/EpdRenderer.h src/render/EpdRenderer.cpp src/app/SleepCycle.h src/app/SleepCycle.cpp src/main.cpp
git commit -m "feat(epaper): board JSON, vendored EPD driver, EpdRenderer, walking skeleton"
```

---

## Task 4: `EpdDashboard` — the single-screen Layout-C paint

**Files:**
- Create: `src/render/EpdDashboard.h`, `src/render/EpdDashboard.cpp`
- Modify: `src/app/SleepCycle.cpp` (call the dashboard with a hardcoded sample view for an on-device smoke; replaced by real data in Task 5)

Layout (960×540, white bg, black ink, gray fills): **status bar** (top, ~36 px), **hero** (left: 7-segment current price + "zł/kWh" + below/above-avg pill; right column: Następna / Sprzedaż PV / Średnia dziś), and **two charts** (Dziś | Jutro) with average line, above/below shading, ▼min/▲max tags, current-hour ring. Jutro half shows "Jutro: brak danych jeszcze" until present.

- [ ] **Step 1: Create `src/render/EpdDashboard.h`**:

```cpp
#pragma once
#include "render/IRenderer.h"
#include "view/PriceView.h"

namespace pstryk {

struct EpdStatus {
  const char* clockHHMM = "";   // last-update time
  int  batteryPct = -1;         // -1 = unknown
  bool batteryLow = false;
  bool wifiOk = true;
  bool stale = false;           // last fetch failed / data old
};

// Paint the whole 960x540 e-paper dashboard and flush. If !view.hasData, paints
// a "Brak danych" message instead.
void drawDashboard(IRenderer& r, const PriceView& view, const EpdStatus& st);

}  // namespace pstryk
```

- [ ] **Step 2: Create `src/render/EpdDashboard.cpp`**:

```cpp
#include "render/EpdDashboard.h"
#include "render/Pages.h"      // renderMessage for the no-data case
#include "core/Format.h"       // formatPln(float, char[8])
#include <cstdio>
#include <cstring>
#include <vector>

namespace pstryk {

namespace {

struct Pal {
  uint16_t bg, ink, mid, light, dark;
  explicit Pal(IRenderer& r)
    : bg(r.rgb(255,255,255)), ink(r.rgb(0,0,0)), mid(r.rgb(140,140,140)),
      light(r.rgb(210,210,210)), dark(r.rgb(40,40,40)) {}
};

// 7-segment masks, bit0=a(top) b c d(bottom) e f g(middle).
const uint8_t SEG[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};

void drawDigit(IRenderer& r, int x, int y, int w, int h, int t, uint16_t c, uint8_t m) {
  int midY = y + (h - t) / 2;
  if (m & 0x01) r.fillRect(x, y, w, t, c);                       // a
  if (m & 0x02) r.fillRect(x + w - t, y, t, h / 2, c);           // b
  if (m & 0x04) r.fillRect(x + w - t, y + h / 2, t, h / 2, c);   // c
  if (m & 0x08) r.fillRect(x, y + h - t, w, t, c);               // d
  if (m & 0x10) r.fillRect(x, y + h / 2, t, h / 2, c);           // e
  if (m & 0x20) r.fillRect(x, y, t, h / 2, c);                   // f
  if (m & 0x40) r.fillRect(x, midY, w, t, c);                    // g
}

// Render a price string (digits, ',' and '.') as big 7-seg blocks. Returns end x.
int drawBigPrice(IRenderer& r, const Pal& p, int x, int y, int h, const char* s) {
  int w = (int)(h * 0.58f), t = (int)(h * 0.13f), gap = (int)(h * 0.16f);
  int cx = x;
  for (const char* q = s; *q; ++q) {
    if (*q == ',' || *q == '.') {
      r.fillRect(cx, y + h - t, t, t, p.ink);                    // comma block
      cx += t + gap;
    } else if (*q >= '0' && *q <= '9') {
      drawDigit(r, cx, y, w, h, t, p.ink, SEG[*q - '0']);
      cx += w + gap;
    } else {
      cx += w / 2;
    }
  }
  return cx;
}

void hourLabel(int hour, char* out) { std::snprintf(out, 6, "%02d:00", hour); }

// Chart in [x,y,w,h]: avg dashed line, above-avg dark / below-avg light bars,
// min ▼ / max ▲ tags, current hour ringed.
void drawChart(IRenderer& r, const Pal& p, const std::vector<Bar>& bars,
               float avg, int liveIdx, int x, int y, int w, int h) {
  if (bars.empty()) { r.text(x, y + h / 2 - 10, "brak danych jeszcze", p.mid, 1); return; }
  float maxP = 0.0001f, minP = 1e9f;
  int minI = 0, maxI = 0, n = (int)bars.size();
  for (int i = 0; i < n; ++i) {
    if (bars[i].price > maxP) { maxP = bars[i].price; maxI = i; }
    if (bars[i].price < minP) { minP = bars[i].price; minI = i; }
  }
  float top = maxP * 1.12f;
  int gap = 2, bw = (w - (n - 1) * gap) / n; if (bw < 1) bw = 1;
  for (int i = 0; i < n; ++i) {
    int bh = (int)((bars[i].price / top) * h);
    int bx = x + i * (bw + gap), by = y + h - bh;
    uint16_t c = (bars[i].price >= avg) ? p.dark : p.light;
    r.fillRect(bx, by, bw, bh, c);
    if (i == liveIdx) r.drawRect(bx - 1, by - 1, bw + 2, bh + 2, p.ink);
    if (i == minI) r.text(bx - 2, by - 14, "v", p.ink, 1);
    if (i == maxI) r.text(bx - 2, by - 14, "^", p.ink, 1);
  }
  // dashed average line
  int ay = y + h - (int)((avg / top) * h);
  for (int dx = x; dx < x + w; dx += 10) r.drawLine(dx, ay, dx + 5, ay, p.ink);
}

}  // namespace

void drawDashboard(IRenderer& r, const PriceView& v, const EpdStatus& st) {
  Pal p(r);
  r.clear(p.bg);
  const int W = r.width();

  if (!v.hasData) { renderMessage(r, "Brak danych", st.clockHHMM); return; }

  // --- status bar ---
  char status[64];
  std::snprintf(status, sizeof(status), "Pstryk  %s%s", st.clockHHMM,
                st.stale ? "  * nieaktualne" : "");
  r.text(20, 8, status, p.ink, 1);
  char bat[20];
  if (st.batteryPct >= 0) std::snprintf(bat, sizeof(bat), "%s %d%%",
                                        st.batteryLow ? "! BAT" : "BAT", st.batteryPct);
  else std::strcpy(bat, st.wifiOk ? "WiFi" : "WiFi?");
  r.text(W - r.textWidth(bat, 1) - 20, 8, bat, p.ink, 1);
  r.drawLine(20, 40, W - 20, 40, p.light);

  // --- hero ---
  char buf[8];
  char hl[6]; hourLabel(v.currentHour, hl);
  char head[24]; std::snprintf(head, sizeof(head), "TERAZ %s", hl);
  r.text(28, 56, head, p.ink, 1);
  formatPln(v.currentBuy, buf);
  int endx = drawBigPrice(r, p, 28, 92, 150, buf);
  r.text(endx + 16, 200, "zl/kWh", p.ink, 1);
  // below/above average pill
  const char* tag = v.currentBelowAvg ? "ponizej sredniej" : "powyzej sredniej";
  int pw = r.textWidth(tag, 1) + 24;
  r.drawRect(28, 250, pw, 30, p.ink);
  r.text(40, 257, tag, p.ink, 1);

  // hero right column
  int rx = 560;
  r.drawLine(rx - 24, 56, rx - 24, 280, p.light);
  char nh[6]; hourLabel(v.nextHour, nh);
  char line[28];
  formatPln(v.nextBuy, buf);
  std::snprintf(line, sizeof(line), "Nastepna %s", nh);
  r.text(rx, 64, line, p.mid, 1);
  std::snprintf(line, sizeof(line), "%s %s", buf,
                v.nextTrend == Trend::Up ? "^" : (v.nextTrend == Trend::Down ? "v" : "-"));
  r.text(rx, 84, line, p.ink, 1);
  formatPln(v.currentSell, buf);
  r.text(rx, 130, "Sprzedaz PV", p.mid, 1); r.text(rx, 150, buf, p.ink, 1);
  formatPln(v.todayAvg, buf);
  r.text(rx, 196, "Srednia dzis", p.mid, 1); r.text(rx, 216, buf, p.ink, 1);

  // --- charts ---
  int cy = 330, ch = 150, half = (W - 60) / 2;
  r.text(28, cy - 22, "Dzis", p.ink, 1);
  drawChart(r, p, v.today, v.todayAvg, v.liveIndex, 28, cy, half, ch);
  int rxc = 28 + half + 24;
  r.text(rxc, cy - 22, "Jutro", p.ink, 1);
  if (v.hasTomorrow)
    drawChart(r, p, v.tomorrow, v.tomorrowAvg, -1, rxc, cy, half, ch);
  else
    r.text(rxc, cy + ch / 2 - 10, "Jutro: brak danych jeszcze", p.mid, 1);

  r.present();
}

}  // namespace pstryk
```

- [ ] **Step 3: Smoke-wire it in `src/app/SleepCycle.cpp`** — replace the `renderMessage(...)` line in `setup()` with a hardcoded sample dashboard so the layout can be eyeballed on device:

```cpp
#include "render/EpdDashboard.h"
// ... inside setup(), after gfx.begin() succeeds:
  PriceView v;
  v.hasData = true;
  v.currentBuy = 0.49f; v.currentSell = 0.18f; v.currentHour = 14;
  v.currentBelowAvg = true; v.nextTrend = Trend::Up; v.nextBuy = 0.51f;
  v.nextHour = 15; v.todayAvg = 0.60f; v.liveIndex = 14;
  for (int i = 0; i < 24; ++i) { Bar b; b.hour = i; b.price = 0.40f + 0.02f * ((i * 7) % 13); v.today.push_back(b); }
  v.hasTomorrow = false;
  EpdStatus st; st.clockHHMM = "14:00"; st.batteryPct = 87; st.wifiOk = true;
  drawDashboard(gfx, v, st);
```

(Keep `#include "view/PriceView.h"` available — it is pulled in via `EpdDashboard.h`.)

- [ ] **Step 4: Verify the e-paper env builds**

Run: `pio run -e t5_epaper_s3`
Expected: SUCCESS.

- [ ] **Step 5: Verify host tests + Long env still green (regression)**

Run: `pio test -e native && pio run -e tdisplay_long`
Expected: PASS / SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/render/EpdDashboard.h src/render/EpdDashboard.cpp src/app/SleepCycle.cpp
git commit -m "feat(epaper): single-screen dashboard (hero + dual charts, treatment 2)"
```

---

## Task 5: `SleepCycle` — full wake→fetch→paint→sleep cycle

**Files:**
- Modify: `src/app/SleepCycle.h`, `src/app/SleepCycle.cpp`

Full cycle order (battery before Wi-Fi due to the ADC2/Wi-Fi conflict): button-hold reconfigure → battery read → settings/provision → Wi-Fi → NTP+PCF8563 → fetch → buildView → paint → compute next wake → arm timer (+button ext0) → deep sleep. PCF8563 over `Wire` (addr 0x51, SDA 18 / SCL 17), storing **local** time.

- [ ] **Step 1: Replace `src/app/SleepCycle.h`**:

```cpp
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

  EpdRenderer gfx_;
  Settings    settings_;
};

}  // namespace pstryk
```

- [ ] **Step 2: Replace `src/app/SleepCycle.cpp`**:

```cpp
#include "app/SleepCycle.h"
#include "render/EpdDashboard.h"
#include "render/Pages.h"
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
#include <esp_adc_cal.h>
#include <cstdio>

namespace pstryk {

static const int      PIN_BTN  = 21;   // user button (active LOW)
static const int      PIN_BATT = 14;   // battery ADC (ADC2)
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
  // POWER_EN is asserted inside epd power-on; gfx_.begin() already called epd_init,
  // and present() toggles power. Assert power for the ADC read window.
  extern void epd_poweron(); extern void epd_poweroff();  // from epd_driver.h (C)
  esp_adc_cal_characteristics_t ch;
  esp_adc_cal_characterize(ADC_UNIT_2, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 1100, &ch);
  long acc = 0;
  for (int i = 0; i < 16; ++i) acc += analogRead(PIN_BATT);
  uint32_t raw = acc / 16;
  uint32_t pinMv = esp_adc_cal_raw_to_voltage(raw, &ch);
  float volts = batteryVoltsFromPinMv((float)pinMv);
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
  // Wake on button press (GPIO21 LOW). ext0 wake-on-low:
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN, 0);
  esp_deep_sleep_start();
}

void SleepCycle::setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(PIN_BTN, INPUT_PULLUP);
  timeServiceBegin();                 // install Warsaw TZ for mktime/localtime
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!gfx_.begin()) { sleepFor(3600); return; }

  // 1) reconfigure if button held at boot
  settings_.load();
  if (buttonHeld(3000) || !settings_.isComplete()) {
    renderMessage(gfx_, "Konfiguracja", "Polacz z 'Pstryk-Setup'");
    WiFiProvisioner prov;
    prov.ensureConnected(settings_, /*forcePortal=*/!settings_.isComplete() ? true : true);
    sleepFor(2);                      // reboot-ish: wake immediately to run a normal cycle
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
    renderMessage(gfx_, "Synchronizacja czasu...", "");
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
      static char clkbuf[6]; std::memcpy(clkbuf, clk, 6);
      st.clockHHMM = clkbuf;
      drawDashboard(gfx_, view, st);            // 7) paint
      nextWake = secondsUntilNextWake(now, view.hasTomorrow);
    } else if (res.status == FetchStatus::AuthError) {
      renderMessage(gfx_, "Blad klucza API", "Przytrzymaj przycisk, aby zmienic");
      nextWake = 1800;
    } else if (res.status == FetchStatus::RateLimited) {
      renderMessage(gfx_, "Limit zapytan", "Sprobuje pozniej");
      nextWake = res.retryAfterSec > 0 ? (uint32_t)res.retryAfterSec : 1200;
    } else {
      char l2[24]; std::snprintf(l2, sizeof(l2), "%02d:%02d", localHour(now),
                                 (int)((now % 3600) / 60));
      renderMessage(gfx_, "Blad pobierania", l2);
      nextWake = 300;
    }
  } else {
    renderMessage(gfx_, "Brak Wi-Fi", "Sprobuje ponownie");
    nextWake = 300;
  }

  // 8) sleep
  WiFi.disconnect(true, false);
  sleepFor(nextWake);
}

}  // namespace pstryk
```

> The `extern "C"` `epd_poweron/off` decls in `readBatteryPercent` mirror the vendored `epd_driver.h`; if the battery reads 0, assert `epd_poweron()` immediately before the `analogRead` loop and `epd_poweroff()` after (POWER_EN gates the divider). Validate the divider/`vref` on device (Task 6) and tune `batteryVoltsFromPinMv` if the % is implausible.

- [ ] **Step 3: Verify the e-paper env builds**

Run: `pio run -e t5_epaper_s3`
Expected: SUCCESS.

- [ ] **Step 4: Regression — host tests + Long env**

Run: `pio test -e native && pio run -e tdisplay_long`
Expected: PASS / SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/app/SleepCycle.h src/app/SleepCycle.cpp
git commit -m "feat(epaper): full deep-sleep cycle (battery, PCF8563, fetch, paint, wake)"
```

---

## Task 6: On-device smoke test + finalize

Hardware required. Manual checklist — no code unless a defect is found.

- [ ] **Step 1: Confirm the physical board** is the *plain* T5-4.7-S3 (not the Pro). If it is the Pro, stop and revisit the spec (different driver board enum, pins, PMIC).

- [ ] **Step 2: Flash**

Run: `pio run -e t5_epaper_s3 -t upload && pio device monitor -e t5_epaper_s3`
(For manual download mode if needed: hold BOOT, tap RST, release.)
Expected: builds, uploads, boots.

- [ ] **Step 3: Provisioning** — on first boot (or hold GPIO21 ≥3 s + tap RST): the `Pstryk-Setup` AP appears; enter Wi-Fi + API key; device saves and continues.

- [ ] **Step 4: One live cycle** — verify on the panel:
  - status bar shows time + battery %; the % is **plausible** (≈ measure the LiPo with a multimeter; if wildly off, tune `batteryVoltsFromPinMv` / vref per the Task 5 note);
  - hero 7-segment price + "zl/kWh" + below/above pill render crisply;
  - Dziś chart: average dashed line, above-avg dark / below-avg light bars, ▼min/▲max tags, current-hour ring;
  - Jutro shows "brak danych jeszcze" before publication, a real chart after.

- [ ] **Step 5: Deep-sleep + wake** — confirm current drops to deep-sleep levels and the **image persists** while asleep; device wakes near the top of the hour and repaints. Leave running across 13:00–15:00 to confirm the midday-tighter wake catches tomorrow.

- [ ] **Step 6: Button** — short press wakes + repaints (validate ext0 wake; if it does NOT wake, the hold-button+RST reconfigure path and timer refresh still work — note the result). Hold ≥3 s → captive portal.

- [ ] **Step 7: Error paths** — temporarily set a bad API key (hold-button reconfigure) → "Blad klucza API" screen; disable Wi-Fi → "Brak Wi-Fi" screen, short retry.

- [ ] **Step 8: Record results + final commit**

Append a short "Validated on hardware YYYY-MM-DD: <results, any tuning done>" note to the spec §7, then:

```bash
git add docs/superpowers/specs/2026-06-02-pstryk-t5-epaper-s3-design.md
git commit -m "docs(spec): record e-paper on-device validation results"
```

---

## Self-review notes
- **Spec coverage:** power model + deep sleep (T5, T6); wake schedule incl. midday tighter (T1); Layout C + treatment 2 (T4); battery % + low warning (T2, T4, T5); button short/long (T5); PCF8563 timekeeping (T5); ADC2/Wi-Fi ordering (T5); ghosting full-clear (T3 `present`); no cross-wake persistence — failed fetch shows error screen (T5); reuse of core + `build_src_filter` separation (T3); board JSON / vendored driver / env (T3). All spec sections map to a task.
- **Type consistency:** `secondsUntilNextWake(time_t,bool)→uint32_t`, `batteryVoltsFromPinMv(float)→float`, `batteryPercent(float)→int`, `drawDashboard(IRenderer&,const PriceView&,const EpdStatus&)`, `EpdStatus{clockHHMM,batteryPct,batteryLow,wifiOk,stale}` used consistently across T1/T2/T4/T5. `EpdRenderer` implements every `IRenderer` pure-virtual.
- **Refinements flagged:** PCF8563 via raw `Wire` (not SensorLib); hero via 7-seg `fillRect` (not a scalable font) — both noted in the header block.
```
