# Pstryk Price Display (T-Display-S3-Long) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build firmware for the LilyGo T-Display-S3-Long that fetches Pstryk hourly electricity prices over WiFi and shows them on four auto-rotating Polish-language pages.

**Architecture:** A board-independent **core** (pure C++, host-unit-tested: JSON parsing, time/DST, price logic, refresh cadence, formatting) plus a thin device layer (NVS storage, HTTPS client, WiFiManager provisioning, Arduino_GFX renderer, app state machine). The core talks to rendering only through a plain `PriceView` struct, and rendering happens behind an `IRenderer` interface — so a future AMOLED/e-ink board is "write a new renderer."

**Tech Stack:** PlatformIO + Arduino-ESP32, Arduino_GFX 1.3.7 (AXS15231B over QSPI), WiFiManager, ArduinoJson 7. Host tests via PlatformIO `native` env + Unity.

**Spec:** `docs/superpowers/specs/2026-06-02-pstryk-tdisplay-long-design.md`

---

## Conventions

- All core code lives in `namespace pstryk` under `src/core/` and `src/view/` and compiles for BOTH the device env and the `native` test env.
- Device-only code (`src/storage/`, `src/net/`, `src/render/`, `src/app/`, `src/main.cpp`) is excluded from the `native` build via `build_src_filter`.
- Prices are PLN/kWh (`float`). Display formatting is Polish (comma decimal, e.g. `0,52`).
- Times: API timestamps are UTC; all display labels use `Europe/Warsaw` (POSIX TZ `CET-1CEST,M3.5.0,M10.5.0/3`).
- Run device builds with `pio run -e tdisplay_long`; upload with `-t upload`; host tests with `pio test -e native`.

## File map

| File | Responsibility | Build |
|------|----------------|-------|
| `platformio.ini` | envs, deps, src filter | both |
| `board/T-Display-Long.json` | custom board definition | device |
| `src/core/PriceData.h` | `PriceFrame` / `PriceData` types | both |
| `src/core/Format.{h,cpp}` | `formatPln()` Polish number formatting | both |
| `src/core/TimeService.{h,cpp}` | ISO8601 parse, UTC→Warsaw, day ordinals, ISO format | both |
| `src/core/PstrykParse.{h,cpp}` | `parsePricing(json)→PriceData` | both |
| `src/view/PriceView.h` | display-ready struct (core→render seam) | both |
| `src/core/PriceLogic.{h,cpp}` | `buildView(PriceData, now)→PriceView` | both |
| `src/core/RefreshPolicy.{h,cpp}` | `computeWindow()`, `nextRefreshMs()` | both |
| `src/storage/Settings.{h,cpp}` | NVS load/save of WiFi + API key | device |
| `src/net/PstrykClient.{h,cpp}` | HTTPS GET + 429 handling → `parsePricing` | device |
| `src/net/WiFiProvisioner.{h,cpp}` | WiFiManager + API-key field | device |
| `src/render/IRenderer.h` | abstract drawing interface | device |
| `src/render/pins_config.h` | GPIO map | device |
| `src/render/LongRenderer.{h,cpp}` | Arduino_GFX impl (QSPI canvas) | device |
| `src/render/Pages.{h,cpp}` | 4 page layouts + status strip + message screen | device |
| `src/app/App.{h,cpp}` | state machine, rotation + refresh scheduling | device |
| `src/main.cpp` | `setup()`/`loop()` → `App` | device |
| `test/test_*/...` | Unity host tests | native |

---

## Task 1: Project scaffold (build + host-test harness)

**Files:**
- Create: `board/T-Display-Long.json`
- Create: `platformio.ini`
- Create: `src/main.cpp`
- Create: `src/core/PriceData.h`
- Create: `test/test_smoke/test_smoke.cpp`

- [ ] **Step 1: Create the custom board definition**

Create `board/T-Display-Long.json`:

```json
{
  "build": {
    "arduino": { "partitions": "huge_app.csv" },
    "core": "esp32",
    "extra_flags": [
      "-DARDUINO_USB_MODE=1",
      "-DARDUINO_USB_CDC_ON_BOOT=1",
      "-DBOARD_HAS_PSRAM",
      "-DARDUINO_RUNNING_CORE=1",
      "-DARDUINO_EVENT_RUNNING_CORE=1"
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
  "name": "LilyGo T-Display-S3 Long",
  "upload": {
    "flash_size": "16MB",
    "maximum_ram_size": 327680,
    "maximum_size": 16777216,
    "require_upload_port": true,
    "speed": 921600
  },
  "url": "https://www.lilygo.cc/products/t-display-s3-long",
  "vendor": "LilyGo"
}
```

- [ ] **Step 2: Create `platformio.ini`**

```ini
[platformio]
boards_dir = board

[env:tdisplay_long]
platform = espressif32
board = T-Display-Long
framework = arduino
monitor_speed = 115200
monitor_filters = esp32_exception_decoder
board_build.partitions = huge_app.csv
build_flags =
    -std=gnu++17
    -I src
    -DCORE_DEBUG_LEVEL=3
lib_deps =
    moononournation/GFX Library for Arduino@1.3.7
    tzapu/WiFiManager@^2.0.17
    bblanchon/ArduinoJson@^7.2.0

[env:native]
platform = native
test_framework = unity
build_src_filter = +<core/> +<view/>
test_build_src = yes
build_flags =
    -std=gnu++17
    -I src
    -DPSTRYK_NATIVE
lib_deps =
    bblanchon/ArduinoJson@^7.2.0
```

- [ ] **Step 3: Create the core data types**

Create `src/core/PriceData.h`:

```cpp
#pragma once
#include <ctime>
#include <vector>

namespace pstryk {

struct PriceFrame {
  time_t start = 0;        // UTC epoch seconds, start of the hour
  float  buy   = 0.0f;     // price_gross, PLN/kWh (VAT incl.)
  float  sell  = 0.0f;     // price_prosumer_gross, PLN/kWh
  bool   isLive = false;
  bool   isCheap = false;
  bool   isExpensive = false;
};

struct PriceData {
  std::vector<PriceFrame> frames;  // ordered by start ascending
};

}  // namespace pstryk
```

- [ ] **Step 4: Create a trivial host test to prove the harness works**

Create `test/test_smoke/test_smoke.cpp`:

```cpp
#include <unity.h>
#include "core/PriceData.h"

void setUp() {}
void tearDown() {}

void test_pricedata_defaults() {
  pstryk::PriceFrame f;
  TEST_ASSERT_EQUAL_INT(0, (int)f.start);
  TEST_ASSERT_FALSE(f.isLive);
  pstryk::PriceData d;
  TEST_ASSERT_EQUAL_UINT(0, d.frames.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pricedata_defaults);
  return UNITY_END();
}
```

- [ ] **Step 5: Create a minimal `src/main.cpp` so the device env links**

```cpp
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("pstryk boot");
}

void loop() {
  Serial.println("alive");
  delay(2000);
}
```

- [ ] **Step 6: Run host tests — expect PASS**

Run: `pio test -e native -f test_smoke`
Expected: `test_pricedata_defaults` PASS, `1 Tests 0 Failures`.

- [ ] **Step 7: Build the device firmware — expect success**

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS`. (Do not upload yet.)

- [ ] **Step 8: Commit**

```bash
git add platformio.ini board/ src/ test/
git commit -m "chore: scaffold PlatformIO project + host-test harness"
```

---

## Task 2: Polish number formatting (`Format`)

**Files:**
- Create: `src/core/Format.h`, `src/core/Format.cpp`
- Test: `test/test_format/test_format.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_format/test_format.cpp`:

```cpp
#include <unity.h>
#include "core/Format.h"

void setUp() {}
void tearDown() {}

void test_two_decimals_comma() {
  char b[8];
  pstryk::formatPln(0.52f, b);
  TEST_ASSERT_EQUAL_STRING("0,52", b);
}

void test_rounds() {
  char b[8];
  pstryk::formatPln(1.128f, b);
  TEST_ASSERT_EQUAL_STRING("1,13", b);  // rounds up (not a half-way tie; printf uses round-half-to-even)
}

void test_integerish() {
  char b[8];
  pstryk::formatPln(1.0f, b);
  TEST_ASSERT_EQUAL_STRING("1,00", b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_decimals_comma);
  RUN_TEST(test_rounds);
  RUN_TEST(test_integerish);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_format`
Expected: FAIL/compile error (`core/Format.h` not found).

- [ ] **Step 3: Write minimal implementation**

Create `src/core/Format.h`:

```cpp
#pragma once
namespace pstryk {
// Writes a Polish-formatted price ("0,52") into out (>=8 bytes).
void formatPln(float value, char* out);
}
```

Create `src/core/Format.cpp`:

```cpp
#include "core/Format.h"
#include <cstdio>

namespace pstryk {

void formatPln(float value, char* out) {
  char tmp[8];
  std::snprintf(tmp, sizeof(tmp), "%.2f", value);
  for (int i = 0; tmp[i]; ++i) {
    if (tmp[i] == '.') tmp[i] = ',';
    out[i] = tmp[i];
    out[i + 1] = '\0';
  }
}

}  // namespace pstryk
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_format`
Expected: `3 Tests 0 Failures`.

- [ ] **Step 5: Commit**

```bash
git add src/core/Format.* test/test_format/
git commit -m "feat(core): Polish price formatting"
```

---

## Task 3: Time & timezone service (`TimeService`)

**Files:**
- Create: `src/core/TimeService.h`, `src/core/TimeService.cpp`
- Test: `test/test_time/test_time.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_time/test_time.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "core/TimeService.h"

using namespace pstryk;

void setUp() { timeServiceBegin(); }
void tearDown() {}

void test_iso_roundtrip() {
  time_t t = parseIso8601Utc("2026-06-02T09:00:00Z");
  char out[21];
  formatIso8601Utc(t, out);
  TEST_ASSERT_EQUAL_STRING("2026-06-02T09:00:00Z", out);
}

void test_local_hour_summer_is_utc_plus_2() {
  // June → CEST (UTC+2): 09:00Z == 11:00 local
  TEST_ASSERT_EQUAL_INT(11, localHour(parseIso8601Utc("2026-06-02T09:00:00Z")));
}

void test_local_hour_winter_is_utc_plus_1() {
  // January → CET (UTC+1): 09:00Z == 10:00 local
  TEST_ASSERT_EQUAL_INT(10, localHour(parseIso8601Utc("2026-01-15T09:00:00Z")));
}

void test_day_ordinals_tomorrow() {
  time_t now = parseIso8601Utc("2026-06-02T09:30:00Z");      // 11:30 local, day X
  time_t tmrw = parseIso8601Utc("2026-06-03T06:00:00Z");     // 08:00 local, day X+1
  TEST_ASSERT_EQUAL_INT(localDayOrdinal(now) + 1, localDayOrdinal(tmrw));
}

void test_local_midnight_utc() {
  // Local midnight of 2026-06-02 (CEST, UTC+2) is 2026-06-01T22:00:00Z
  time_t now = parseIso8601Utc("2026-06-02T09:30:00Z");
  char out[21];
  formatIso8601Utc(localMidnightUtc(now), out);
  TEST_ASSERT_EQUAL_STRING("2026-06-01T22:00:00Z", out);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_iso_roundtrip);
  RUN_TEST(test_local_hour_summer_is_utc_plus_2);
  RUN_TEST(test_local_hour_winter_is_utc_plus_1);
  RUN_TEST(test_day_ordinals_tomorrow);
  RUN_TEST(test_local_midnight_utc);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_time`
Expected: FAIL (`core/TimeService.h` not found).

- [ ] **Step 3: Write minimal implementation**

Create `src/core/TimeService.h`:

```cpp
#pragma once
#include <ctime>

namespace pstryk {

// Installs the Europe/Warsaw TZ rule for localtime_r/mktime. Call once at startup
// (host tests call it in setUp; device calls it after NTP config).
void timeServiceBegin();

time_t parseIso8601Utc(const char* s);     // "YYYY-MM-DDTHH:MM:SSZ" -> UTC epoch (0 on error)
void   formatIso8601Utc(time_t t, char* out21);  // -> "YYYY-MM-DDTHH:MM:SSZ" (needs >=21 bytes)

int    localHour(time_t utc);              // 0..23 in Europe/Warsaw
int    localDayOrdinal(time_t utc);        // days since 1970-01-01 of the LOCAL date
time_t localMidnightUtc(time_t utc);       // 00:00 local of utc's day, as UTC epoch

}  // namespace pstryk
```

Create `src/core/TimeService.cpp`:

```cpp
#include "core/TimeService.h"
#include <cstdio>
#include <cstdlib>

namespace pstryk {

static const char* kTz = "CET-1CEST,M3.5.0,M10.5.0/3";

static long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

void timeServiceBegin() {
  setenv("TZ", kTz, 1);
  tzset();
}

time_t parseIso8601Utc(const char* s) {
  int Y, M, D, h, mi, se;
  if (!s || std::sscanf(s, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &mi, &se) != 6) return 0;
  long days = daysFromCivil(Y, M, D);
  return (time_t)days * 86400 + h * 3600 + mi * 60 + se;
}

void formatIso8601Utc(time_t t, char* out21) {
  struct tm g;
  gmtime_r(&t, &g);
  std::strftime(out21, 21, "%Y-%m-%dT%H:%M:%SZ", &g);
}

int localHour(time_t utc) {
  struct tm l;
  localtime_r(&utc, &l);
  return l.tm_hour;
}

int localDayOrdinal(time_t utc) {
  struct tm l;
  localtime_r(&utc, &l);
  return (int)daysFromCivil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
}

time_t localMidnightUtc(time_t utc) {
  struct tm l;
  localtime_r(&utc, &l);
  l.tm_hour = 0; l.tm_min = 0; l.tm_sec = 0; l.tm_isdst = -1;
  return mktime(&l);
}

}  // namespace pstryk
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_time`
Expected: `5 Tests 0 Failures`.

- [ ] **Step 5: Commit**

```bash
git add src/core/TimeService.* test/test_time/
git commit -m "feat(core): Europe/Warsaw time + ISO8601 helpers"
```

---

## Task 4: Pstryk JSON parser (`PstrykParse`)

**Files:**
- Create: `src/core/PstrykParse.h`, `src/core/PstrykParse.cpp`
- Create: `test/fixtures.h`
- Test: `test/test_parse/test_parse.cpp`

- [ ] **Step 1: Create the shared fixture**

Create `test/fixtures.h`:

```cpp
#pragma once
// Minimal but representative unified-metrics pricing response.
// Europe/Warsaw is CEST (UTC+2) on these June dates.
static const char* kPricingJson = R"JSON(
{
  "frames": [
    {"start":"2026-06-02T06:00:00Z","end":"2026-06-02T07:00:00Z","price_gross":0.30,"price_prosumer_gross":0.18,"is_cheap":true,"is_expensive":false},
    {"start":"2026-06-02T07:00:00Z","end":"2026-06-02T08:00:00Z","price_gross":0.52,"price_prosumer_gross":0.31,"is_live":true,"is_cheap":false,"is_expensive":false},
    {"start":"2026-06-02T08:00:00Z","end":"2026-06-02T09:00:00Z","price_gross":0.48,"price_prosumer_gross":0.29,"is_cheap":false,"is_expensive":false},
    {"start":"2026-06-02T09:00:00Z","end":"2026-06-02T10:00:00Z","price_gross":1.12,"price_prosumer_gross":0.40,"is_cheap":false,"is_expensive":true},
    {"start":"2026-06-03T06:00:00Z","end":"2026-06-03T07:00:00Z","price_gross":0.40,"price_prosumer_gross":0.20,"is_cheap":false,"is_expensive":false},
    {"start":"2026-06-03T07:00:00Z","end":"2026-06-03T08:00:00Z","price_gross":0.19,"price_prosumer_gross":0.10,"is_cheap":true,"is_expensive":false}
  ],
  "summary": {}
}
)JSON";

// Same response shape but with only today's frames (tomorrow not yet published).
static const char* kPricingTodayOnlyJson = R"JSON(
{"frames":[
  {"start":"2026-06-02T06:00:00Z","end":"2026-06-02T07:00:00Z","price_gross":0.30,"price_prosumer_gross":0.18,"is_cheap":true},
  {"start":"2026-06-02T07:00:00Z","end":"2026-06-02T08:00:00Z","price_gross":0.52,"price_prosumer_gross":0.31,"is_live":true}
],"summary":{}}
)JSON";
```

- [ ] **Step 2: Write the failing test**

Create `test/test_parse/test_parse.cpp`:

```cpp
#include <unity.h>
#include "core/PstrykParse.h"
#include "core/TimeService.h"
#include "../fixtures.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_parses_all_frames() {
  PriceData d;
  TEST_ASSERT_TRUE(parsePricing(kPricingJson, d));
  TEST_ASSERT_EQUAL_UINT(6, d.frames.size());
}

void test_maps_fields() {
  PriceData d;
  parsePricing(kPricingJson, d);
  const PriceFrame& f = d.frames[1];
  TEST_ASSERT_EQUAL(parseIso8601Utc("2026-06-02T07:00:00Z"), f.start);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.52f, f.buy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.31f, f.sell);
  TEST_ASSERT_TRUE(f.isLive);
}

void test_flags_default_false() {
  PriceData d;
  parsePricing(kPricingJson, d);
  TEST_ASSERT_FALSE(d.frames[2].isLive);
  TEST_ASSERT_TRUE(d.frames[3].isExpensive);
}

void test_today_only() {
  PriceData d;
  TEST_ASSERT_TRUE(parsePricing(kPricingTodayOnlyJson, d));
  TEST_ASSERT_EQUAL_UINT(2, d.frames.size());
}

void test_malformed_returns_false() {
  PriceData d;
  TEST_ASSERT_FALSE(parsePricing("not json", d));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_all_frames);
  RUN_TEST(test_maps_fields);
  RUN_TEST(test_flags_default_false);
  RUN_TEST(test_today_only);
  RUN_TEST(test_malformed_returns_false);
  return UNITY_END();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `pio test -e native -f test_parse`
Expected: FAIL (`core/PstrykParse.h` not found).

- [ ] **Step 4: Write minimal implementation**

Create `src/core/PstrykParse.h`:

```cpp
#pragma once
#include "core/PriceData.h"

namespace pstryk {
// Parses a unified-metrics pricing response. Returns false on JSON error.
bool parsePricing(const char* json, PriceData& out);
}
```

Create `src/core/PstrykParse.cpp`:

```cpp
#include "core/PstrykParse.h"
#include "core/TimeService.h"
#include <ArduinoJson.h>

namespace pstryk {

bool parsePricing(const char* json, PriceData& out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) return false;
  JsonArray frames = doc["frames"].as<JsonArray>();
  if (frames.isNull()) return false;

  out.frames.clear();
  for (JsonObject fr : frames) {
    PriceFrame f;
    f.start       = parseIso8601Utc(fr["start"] | "");
    f.buy         = fr["price_gross"]          | 0.0f;
    f.sell        = fr["price_prosumer_gross"] | 0.0f;
    f.isLive      = fr["is_live"]      | false;
    f.isCheap     = fr["is_cheap"]     | false;
    f.isExpensive = fr["is_expensive"] | false;
    out.frames.push_back(f);
  }
  return true;
}

}  // namespace pstryk
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_parse`
Expected: `5 Tests 0 Failures`.

- [ ] **Step 6: Commit**

```bash
git add src/core/PstrykParse.* test/test_parse/ test/fixtures.h
git commit -m "feat(core): parse Pstryk unified-metrics pricing JSON"
```

---

## Task 5: Display-ready view + price logic (`PriceView`, `PriceLogic`)

**Files:**
- Create: `src/view/PriceView.h`
- Create: `src/core/PriceLogic.h`, `src/core/PriceLogic.cpp`
- Test: `test/test_logic/test_logic.cpp`

- [ ] **Step 1: Create the view struct (the core→render seam)**

Create `src/view/PriceView.h`:

```cpp
#pragma once
#include <vector>

namespace pstryk {

enum class Trend { Up, Down, Flat };

struct Bar {
  int   hour = 0;          // local hour 0..23
  float price = 0.0f;      // buy price PLN/kWh
  bool  isLive = false;
  bool  isCheap = false;
  bool  isExpensive = false;
};

struct PriceView {
  bool  hasData = false;

  // Page: Teraz
  float currentBuy = 0, currentSell = 0;
  int   currentHour = 0;
  bool  currentBelowAvg = true;
  Trend nextTrend = Trend::Flat;
  float nextBuy = 0;
  int   nextHour = 0;
  float todayAvg = 0;

  // Page: Wykres 24h + Najtaniej/Najdrozej (today)
  std::vector<Bar> today;
  int   liveIndex = -1;          // index into `today` of the live hour, or -1
  Bar   todayCheapest, todayExpensive;

  // Page: Jutro
  bool  hasTomorrow = false;
  std::vector<Bar> tomorrow;
  float tomorrowAvg = 0;
  Bar   tomorrowCheapest, tomorrowExpensive;
};

}  // namespace pstryk
```

- [ ] **Step 2: Write the failing test**

Create `test/test_logic/test_logic.cpp`:

```cpp
#include <unity.h>
#include "core/PriceLogic.h"
#include "core/PstrykParse.h"
#include "core/TimeService.h"
#include "../fixtures.h"

using namespace pstryk;

void setUp() { timeServiceBegin(); }
void tearDown() {}

static PriceView viewFromFixture(const char* json) {
  PriceData d;
  parsePricing(json, d);
  // "now" = 2026-06-02T07:30:00Z == 09:30 local, inside the is_live frame.
  return buildView(d, parseIso8601Utc("2026-06-02T07:30:00Z"));
}

void test_current_from_is_live() {
  PriceView v = viewFromFixture(kPricingJson);
  TEST_ASSERT_TRUE(v.hasData);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.52f, v.currentBuy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.31f, v.currentSell);
  TEST_ASSERT_EQUAL_INT(9, v.currentHour);  // 07:00Z -> 09:00 local
}

void test_today_split_and_extremes() {
  PriceView v = viewFromFixture(kPricingJson);
  TEST_ASSERT_EQUAL_UINT(4, v.today.size());        // 4 today frames
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.30f, v.todayCheapest.price);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.12f, v.todayExpensive.price);
  TEST_ASSERT_EQUAL_INT(1, v.liveIndex);            // 2nd today bar is live
}

void test_today_average() {
  PriceView v = viewFromFixture(kPricingJson);
  // (0.30+0.52+0.48+1.12)/4 = 0.605
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.605f, v.todayAvg);
  TEST_ASSERT_TRUE(v.currentBelowAvg);              // 0.52 < 0.605
}

void test_next_hour_trend() {
  PriceView v = viewFromFixture(kPricingJson);
  // current 0.52 -> next today bar 0.48 -> Down
  TEST_ASSERT_EQUAL_INT((int)Trend::Down, (int)v.nextTrend);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.48f, v.nextBuy);
}

void test_tomorrow_present() {
  PriceView v = viewFromFixture(kPricingJson);
  TEST_ASSERT_TRUE(v.hasTomorrow);
  TEST_ASSERT_EQUAL_UINT(2, v.tomorrow.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.19f, v.tomorrowCheapest.price);
}

void test_tomorrow_absent() {
  PriceView v = viewFromFixture(kPricingTodayOnlyJson);
  TEST_ASSERT_FALSE(v.hasTomorrow);
  TEST_ASSERT_EQUAL_UINT(0, v.tomorrow.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_current_from_is_live);
  RUN_TEST(test_today_split_and_extremes);
  RUN_TEST(test_today_average);
  RUN_TEST(test_next_hour_trend);
  RUN_TEST(test_tomorrow_present);
  RUN_TEST(test_tomorrow_absent);
  return UNITY_END();
}
```

- [ ] **Step 3: Run test to verify it fails**

Run: `pio test -e native -f test_logic`
Expected: FAIL (`core/PriceLogic.h` not found).

- [ ] **Step 4: Write minimal implementation**

Create `src/core/PriceLogic.h`:

```cpp
#pragma once
#include "core/PriceData.h"
#include "view/PriceView.h"
#include <ctime>

namespace pstryk {
// Builds a display-ready PriceView from parsed data and the current UTC time.
PriceView buildView(const PriceData& data, time_t now);
}
```

Create `src/core/PriceLogic.cpp`:

```cpp
#include "core/PriceLogic.h"
#include "core/TimeService.h"

namespace pstryk {

static Bar toBar(const PriceFrame& f) {
  Bar b;
  b.hour = localHour(f.start);
  b.price = f.buy;
  b.isLive = f.isLive;
  b.isCheap = f.isCheap;
  b.isExpensive = f.isExpensive;
  return b;
}

static void extremes(const std::vector<Bar>& bars, Bar& cheap, Bar& exp) {
  if (bars.empty()) { cheap = Bar{}; exp = Bar{}; return; }
  cheap = exp = bars[0];
  for (const Bar& b : bars) {
    if (b.price < cheap.price) cheap = b;
    if (b.price > exp.price)   exp = b;
  }
}

PriceView buildView(const PriceData& data, time_t now) {
  PriceView v;
  if (data.frames.empty()) return v;

  const int todayOrd = localDayOrdinal(now);

  // Split into today / tomorrow by local date.
  for (const PriceFrame& f : data.frames) {
    int ord = localDayOrdinal(f.start);
    if (ord == todayOrd)        v.today.push_back(toBar(f));
    else if (ord == todayOrd + 1) v.tomorrow.push_back(toBar(f));
  }
  if (v.today.empty() && v.tomorrow.empty()) return v;
  v.hasData = true;

  // Today aggregates.
  if (!v.today.empty()) {
    float sum = 0;
    for (size_t i = 0; i < v.today.size(); ++i) {
      sum += v.today[i].price;
      if (v.today[i].isLive) v.liveIndex = (int)i;
    }
    v.todayAvg = sum / v.today.size();
    extremes(v.today, v.todayCheapest, v.todayExpensive);
  }

  // Current frame: prefer is_live; else the frame whose hour contains `now`.
  int cur = v.liveIndex;
  if (cur < 0) {
    for (size_t i = 0; i < data.frames.size(); ++i) {
      if (now >= data.frames[i].start && now < data.frames[i].start + 3600) {
        // map into today vector if present
        for (size_t j = 0; j < v.today.size(); ++j)
          if (v.today[j].hour == localHour(data.frames[i].start)) { cur = (int)j; break; }
        break;
      }
    }
  }
  if (cur >= 0 && cur < (int)v.today.size()) {
    v.currentBuy = v.today[cur].price;
    v.currentHour = v.today[cur].hour;
    v.currentBelowAvg = v.currentBuy <= v.todayAvg;
    // sell price: find matching source frame by hour
    for (const PriceFrame& f : data.frames)
      if (localDayOrdinal(f.start) == todayOrd && localHour(f.start) == v.currentHour) {
        v.currentSell = f.sell; break;
      }
    // next hour trend
    if (cur + 1 < (int)v.today.size()) {
      v.nextBuy = v.today[cur + 1].price;
      v.nextHour = v.today[cur + 1].hour;
      float d = v.nextBuy - v.currentBuy;
      v.nextTrend = (d > 0.001f) ? Trend::Up : (d < -0.001f ? Trend::Down : Trend::Flat);
    }
  }

  // Tomorrow aggregates.
  if (!v.tomorrow.empty()) {
    v.hasTomorrow = true;
    float sum = 0;
    for (const Bar& b : v.tomorrow) sum += b.price;
    v.tomorrowAvg = sum / v.tomorrow.size();
    extremes(v.tomorrow, v.tomorrowCheapest, v.tomorrowExpensive);
  }
  return v;
}

}  // namespace pstryk
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_logic`
Expected: `6 Tests 0 Failures`.

- [ ] **Step 6: Commit**

```bash
git add src/view/PriceView.h src/core/PriceLogic.* test/test_logic/
git commit -m "feat(core): build display-ready PriceView from price data"
```

---

## Task 6: Refresh policy (`RefreshPolicy`)

**Files:**
- Create: `src/core/RefreshPolicy.h`, `src/core/RefreshPolicy.cpp`
- Test: `test/test_refresh/test_refresh.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_refresh/test_refresh.cpp`:

```cpp
#include <unity.h>
#include "core/RefreshPolicy.h"
#include "core/TimeService.h"

using namespace pstryk;

void setUp() { timeServiceBegin(); }
void tearDown() {}

void test_window_is_local_midnight_plus_48h() {
  time_t now = parseIso8601Utc("2026-06-02T09:30:00Z");
  Window w = computeWindow(now);
  TEST_ASSERT_EQUAL_STRING("2026-06-01T22:00:00Z", w.start);  // local midnight (CEST)
  TEST_ASSERT_EQUAL_STRING("2026-06-03T22:00:00Z", w.end);    // +48h
}

void test_base_cadence_30min_before_noon() {
  time_t now = parseIso8601Utc("2026-06-02T07:00:00Z");  // 09:00 local
  TEST_ASSERT_EQUAL_UINT32(30u * 60u * 1000u, nextRefreshMs(now, false));
}

void test_awaiting_tomorrow_20min_after_noon() {
  time_t now = parseIso8601Utc("2026-06-02T12:00:00Z");  // 14:00 local, no tomorrow yet
  TEST_ASSERT_EQUAL_UINT32(20u * 60u * 1000u, nextRefreshMs(now, false));
}

void test_after_noon_with_tomorrow_back_to_30min() {
  time_t now = parseIso8601Utc("2026-06-02T12:00:00Z");  // 14:00 local, tomorrow present
  TEST_ASSERT_EQUAL_UINT32(30u * 60u * 1000u, nextRefreshMs(now, true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_window_is_local_midnight_plus_48h);
  RUN_TEST(test_base_cadence_30min_before_noon);
  RUN_TEST(test_awaiting_tomorrow_20min_after_noon);
  RUN_TEST(test_after_noon_with_tomorrow_back_to_30min);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_refresh`
Expected: FAIL (`core/RefreshPolicy.h` not found).

- [ ] **Step 3: Write minimal implementation**

Create `src/core/RefreshPolicy.h`:

```cpp
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

}  // namespace pstryk
```

Create `src/core/RefreshPolicy.cpp`:

```cpp
#include "core/RefreshPolicy.h"
#include "core/TimeService.h"

namespace pstryk {

Window computeWindow(time_t now) {
  Window w;
  time_t midnight = localMidnightUtc(now);
  formatIso8601Utc(midnight, w.start);
  formatIso8601Utc(midnight + 48 * 3600, w.end);
  return w;
}

uint32_t nextRefreshMs(time_t now, bool hasTomorrow) {
  if (localHour(now) >= 12 && !hasTomorrow) return 20u * 60u * 1000u;
  return 30u * 60u * 1000u;
}

}  // namespace pstryk
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_refresh`
Expected: `4 Tests 0 Failures`.

- [ ] **Step 5: Run the FULL host suite to confirm nothing regressed**

Run: `pio test -e native`
Expected: all suites pass (`test_smoke, test_format, test_time, test_parse, test_logic, test_refresh`).

- [ ] **Step 6: Commit**

```bash
git add src/core/RefreshPolicy.* test/test_refresh/
git commit -m "feat(core): refresh window + cadence policy"
```

---

## Task 7: Persistent settings (`Settings`, NVS) — device

**Files:**
- Create: `src/storage/Settings.h`, `src/storage/Settings.cpp`

> Device-only (uses `Preferences`). Not in the native build. Verified by the on-device smoke test in Task 13.

- [ ] **Step 1: Write the header**

Create `src/storage/Settings.h`:

```cpp
#pragma once
#include <Arduino.h>

namespace pstryk {

struct Settings {
  String ssid;
  String pass;
  String apiKey;

  bool isComplete() const { return ssid.length() > 0 && apiKey.length() > 0; }

  void load();   // read from NVS namespace "pstryk"
  void save();   // persist to NVS
  void clear();  // wipe (used by re-provisioning)
};

}  // namespace pstryk
```

- [ ] **Step 2: Write the implementation**

Create `src/storage/Settings.cpp`:

```cpp
#include "storage/Settings.h"
#include <Preferences.h>

namespace pstryk {

static const char* kNs = "pstryk";

void Settings::load() {
  Preferences p;
  p.begin(kNs, /*readOnly=*/true);
  ssid   = p.getString("ssid", "");
  pass   = p.getString("pass", "");
  apiKey = p.getString("apiKey", "");
  p.end();
}

void Settings::save() {
  Preferences p;
  p.begin(kNs, false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.putString("apiKey", apiKey);
  p.end();
}

void Settings::clear() {
  Preferences p;
  p.begin(kNs, false);
  p.clear();
  p.end();
  ssid = ""; pass = ""; apiKey = "";
}

}  // namespace pstryk
```

- [ ] **Step 3: Build device firmware to verify it compiles**

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/storage/
git commit -m "feat(storage): NVS-backed settings (wifi + api key)"
```

---

## Task 8: HTTPS API client (`PstrykClient`) — device

**Files:**
- Create: `src/net/PstrykClient.h`, `src/net/PstrykClient.cpp`

> Device-only. Splits transport from the (already-tested) pure parser.

- [ ] **Step 1: Write the header**

Create `src/net/PstrykClient.h`:

```cpp
#pragma once
#include <Arduino.h>
#include "core/PriceData.h"

namespace pstryk {

enum class FetchStatus { Ok, RateLimited, AuthError, NetworkError, ParseError };

struct FetchResult {
  FetchStatus status = FetchStatus::NetworkError;
  int httpCode = 0;
  int retryAfterSec = 0;   // populated on RateLimited if header present
};

class PstrykClient {
 public:
  explicit PstrykClient(const String& apiKey) : apiKey_(apiKey) {}
  // Fetches the window [start,end) (UTC ISO strings) and fills `out` on success.
  FetchResult fetch(const char* startIso, const char* endIso, PriceData& out);

 private:
  String apiKey_;
};

}  // namespace pstryk
```

- [ ] **Step 2: Write the implementation**

Create `src/net/PstrykClient.cpp`:

```cpp
#include "net/PstrykClient.h"
#include "core/PstrykParse.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

namespace pstryk {

static String urlEncodeIso(const char* iso) {
  // Only ':' needs encoding in our ISO8601 strings.
  String s;
  for (const char* p = iso; *p; ++p) s += (*p == ':') ? String("%3A") : String(*p);
  return s;
}

FetchResult PstrykClient::fetch(const char* startIso, const char* endIso, PriceData& out) {
  FetchResult r;
  String url =
      "https://api.pstryk.pl/integrations/meter-data/unified-metrics/"
      "?metrics=pricing&resolution=hour&window_start=" + urlEncodeIso(startIso) +
      "&window_end=" + urlEncodeIso(endIso);

  WiFiClientSecure client;
  client.setInsecure();  // v1: skip cert validation (personal device)

  HTTPClient https;
  https.setConnectTimeout(8000);
  https.setTimeout(8000);
  if (!https.begin(client, url)) {
    r.status = FetchStatus::NetworkError;
    return r;
  }
  https.addHeader("Authorization", apiKey_);  // raw key, no "Bearer"
  https.addHeader("Accept", "application/json");
  const char* collect[] = {"Retry-After"};
  https.collectHeaders(collect, 1);

  int code = https.GET();
  r.httpCode = code;

  if (code == 200) {
    String body = https.getString();
    https.end();
    if (parsePricing(body.c_str(), out)) {
      r.status = FetchStatus::Ok;
    } else {
      r.status = FetchStatus::ParseError;
    }
    return r;
  }

  if (code == 429) {
    r.retryAfterSec = https.header("Retry-After").toInt();
    r.status = FetchStatus::RateLimited;
  } else if (code == 401 || code == 403) {
    r.status = FetchStatus::AuthError;
  } else {
    r.status = FetchStatus::NetworkError;
  }
  https.end();
  return r;
}

}  // namespace pstryk
```

- [ ] **Step 3: Build device firmware to verify it compiles**

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/net/PstrykClient.*
git commit -m "feat(net): Pstryk HTTPS client with 429/auth handling"
```

---

## Task 9: WiFi provisioning (`WiFiProvisioner`) — device

**Files:**
- Create: `src/net/WiFiProvisioner.h`, `src/net/WiFiProvisioner.cpp`

> Device-only. Wraps WiFiManager and adds a custom API-key field.

- [ ] **Step 1: Write the header**

Create `src/net/WiFiProvisioner.h`:

```cpp
#pragma once
#include <Arduino.h>
#include "storage/Settings.h"

namespace pstryk {

class WiFiProvisioner {
 public:
  // Connects using saved settings. If incomplete or `forcePortal`, opens the
  // captive portal "Pstryk-Setup" (blocking) to capture WiFi + API key, then
  // writes them into `s` and persists. Returns true once connected.
  bool ensureConnected(Settings& s, bool forcePortal);
};

}  // namespace pstryk
```

- [ ] **Step 2: Write the implementation**

Create `src/net/WiFiProvisioner.cpp`:

```cpp
#include "net/WiFiProvisioner.h"
#include <WiFi.h>
#include <WiFiManager.h>

namespace pstryk {

bool WiFiProvisioner::ensureConnected(Settings& s, bool forcePortal) {
  WiFiManager wm;
  wm.setConfigPortalTimeout(0);  // stay in portal until configured

  WiFiManagerParameter apiKeyParam("apikey", "Pstryk API key (sk-...)",
                                   s.apiKey.c_str(), 80);
  wm.addParameter(&apiKeyParam);

  bool ok;
  if (forcePortal || !s.isComplete()) {
    ok = wm.startConfigPortal("Pstryk-Setup");
  } else {
    WiFi.begin(s.ssid.c_str(), s.pass.c_str());
    // autoConnect falls back to the portal if saved creds fail.
    ok = wm.autoConnect("Pstryk-Setup");
  }

  if (ok) {
    // Capture whatever WiFiManager negotiated/entered.
    s.ssid = WiFi.SSID();
    s.pass = WiFi.psk();
    String entered = apiKeyParam.getValue();
    if (entered.length() > 0) s.apiKey = entered;
    s.save();
  }
  return ok && WiFi.status() == WL_CONNECTED;
}

}  // namespace pstryk
```

- [ ] **Step 3: Build device firmware to verify it compiles**

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/net/WiFiProvisioner.*
git commit -m "feat(net): WiFiManager captive portal with API-key field"
```

---

## Task 10: Renderer interface + Arduino_GFX implementation — device

**Files:**
- Create: `src/render/pins_config.h`
- Create: `src/render/IRenderer.h`
- Create: `src/render/LongRenderer.h`, `src/render/LongRenderer.cpp`

> Device-only. Draws to an off-screen canvas in PSRAM and flushes to the QSPI panel to avoid flicker on per-second redraws.

- [ ] **Step 1: Create the pin map**

Create `src/render/pins_config.h`:

```cpp
#pragma once
// LilyGo T-Display-S3-Long (AXS15231B over QSPI). From official pins_config.h.
#define TFT_QSPI_CS   12
#define TFT_QSPI_SCK  17
#define TFT_QSPI_D0   13
#define TFT_QSPI_D1   18
#define TFT_QSPI_D2   21
#define TFT_QSPI_D3   14
#define TFT_QSPI_RST  16
#define TFT_BL         1

#define PANEL_NATIVE_W 180
#define PANEL_NATIVE_H 640
#define SCREEN_W       640   // landscape (rotation applied)
#define SCREEN_H       180

#define PIN_BUTTON_BOOT 0
```

- [ ] **Step 2: Create the abstract renderer**

Create `src/render/IRenderer.h`:

```cpp
#pragma once
#include <cstdint>

namespace pstryk {

class IRenderer {
 public:
  virtual ~IRenderer() = default;
  virtual bool begin() = 0;
  virtual int  width() = 0;
  virtual int  height() = 0;

  virtual uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) = 0;

  virtual void clear(uint16_t color) = 0;
  virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;
  virtual void drawRect(int x, int y, int w, int h, uint16_t color) = 0;
  virtual void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color) = 0;
  virtual void drawLine(int x0, int y0, int x1, int y1, uint16_t color) = 0;

  // Text anchored at top-left (x,y); `size` scales the built-in 6x8 font.
  virtual void text(int x, int y, const char* s, uint16_t color, int size) = 0;
  virtual int  textWidth(const char* s, int size) = 0;
  virtual int  textHeight(int size) = 0;

  virtual void present() = 0;  // push canvas to the panel
};

}  // namespace pstryk
```

- [ ] **Step 3: Create the Arduino_GFX implementation header**

Create `src/render/LongRenderer.h`:

```cpp
#pragma once
#include "render/IRenderer.h"

class Arduino_DataBus;
class Arduino_GFX;
class Arduino_Canvas;

namespace pstryk {

class LongRenderer : public IRenderer {
 public:
  bool begin() override;
  int  width() override  { return 640; }
  int  height() override { return 180; }

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
  Arduino_DataBus* bus_ = nullptr;
  Arduino_GFX*     panel_ = nullptr;
  Arduino_Canvas*  canvas_ = nullptr;  // 640x180 framebuffer in PSRAM
};

}  // namespace pstryk
```

- [ ] **Step 4: Create the implementation**

Create `src/render/LongRenderer.cpp`:

```cpp
#include "render/LongRenderer.h"
#include "render/pins_config.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <cstring>

namespace pstryk {

bool LongRenderer::begin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // backlight on

  bus_ = new Arduino_ESP32QSPI(
      TFT_QSPI_CS, TFT_QSPI_SCK, TFT_QSPI_D0, TFT_QSPI_D1, TFT_QSPI_D2, TFT_QSPI_D3);
  // rotation 1 => landscape 640x180; IPS=false per LilyGo example.
  panel_ = new Arduino_AXS15231(bus_, TFT_QSPI_RST, /*rotation=*/1, /*ips=*/false,
                                PANEL_NATIVE_W, PANEL_NATIVE_H);
  // Off-screen canvas (uses PSRAM) -> flush to panel; avoids flicker.
  canvas_ = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel_);
  if (!canvas_->begin(GFX_SKIP_OUTPUT_BEGIN)) return false;
  if (!panel_->begin(32000000)) return false;   // <=32 MHz QSPI ceiling
  canvas_->setUTF8Print(true);
  return true;
}

uint16_t LongRenderer::rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);  // RGB565
}
void LongRenderer::clear(uint16_t c) { canvas_->fillScreen(c); }
void LongRenderer::fillRect(int x, int y, int w, int h, uint16_t c) { canvas_->fillRect(x, y, w, h, c); }
void LongRenderer::drawRect(int x, int y, int w, int h, uint16_t c) { canvas_->drawRect(x, y, w, h, c); }
void LongRenderer::fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) { canvas_->fillRoundRect(x, y, w, h, r, c); }
void LongRenderer::drawLine(int x0, int y0, int x1, int y1, uint16_t c) { canvas_->drawLine(x0, y0, x1, y1, c); }

void LongRenderer::text(int x, int y, const char* s, uint16_t color, int size) {
  canvas_->setTextColor(color);
  canvas_->setTextSize(size);
  canvas_->setCursor(x, y);
  canvas_->print(s);
}
int LongRenderer::textWidth(const char* s, int size) { return (int)std::strlen(s) * 6 * size; }
int LongRenderer::textHeight(int size) { return 8 * size; }

void LongRenderer::present() { canvas_->flush(); }

}  // namespace pstryk
```

- [ ] **Step 5: Temporarily exercise the renderer from `main.cpp`**

Replace `src/main.cpp` with a render smoke test:

```cpp
#include <Arduino.h>
#include "render/LongRenderer.h"

pstryk::LongRenderer gfx;

void setup() {
  Serial.begin(115200);
  delay(300);
  if (!gfx.begin()) { Serial.println("renderer begin FAILED"); return; }
  uint16_t bg = gfx.rgb(0x0a, 0x0e, 0x14);
  uint16_t fg = gfx.rgb(0xe8, 0xed, 0xf4);
  uint16_t green = gfx.rgb(0x34, 0xd3, 0x99);
  gfx.clear(bg);
  gfx.text(20, 20, "PSTRYK", fg, 3);
  gfx.text(20, 70, "0,52 zl/kWh", green, 5);
  gfx.fillRoundRect(20, 140, 600, 20, 6, green);
  gfx.present();
}

void loop() { delay(1000); }
```

- [ ] **Step 6: Build, upload, and visually verify**

Run: `pio run -e tdisplay_long -t upload && pio device monitor`
Expected: device prints boot logs; the long screen shows "PSTRYK", a large green "0,52 zl/kWh", and a green bar — correctly oriented as a wide landscape bar (not mirrored/rotated wrong). If orientation is wrong, change the `rotation` arg (try 3) in `LongRenderer::begin`.

- [ ] **Step 7: Commit**

```bash
git add src/render/ src/main.cpp
git commit -m "feat(render): Arduino_GFX QSPI renderer (canvas) for the Long panel"
```

---

## Task 11: Page layouts (`Pages`) — device

**Files:**
- Create: `src/render/Pages.h`, `src/render/Pages.cpp`

> Device-only. Pure drawing from a `PriceView` via `IRenderer`. Built-in font + `setTextSize` (deterministic; nicer fonts are deferred polish per the spec).

- [ ] **Step 1: Write the header**

Create `src/render/Pages.h`:

```cpp
#pragma once
#include "render/IRenderer.h"
#include "view/PriceView.h"

namespace pstryk {

enum class Page { Teraz, Chart, Extremes, Jutro };

// Draws one page (incl. status strip) and flushes. `clockHHMM` may be "" if unknown.
void renderPage(IRenderer& r, Page page, const PriceView& v,
                bool stale, const char* clockHHMM, int pageDotIndex, int pageDotCount);

// Full-screen status/error message (boot, provisioning, errors).
void renderMessage(IRenderer& r, const char* line1, const char* line2);

}  // namespace pstryk
```

- [ ] **Step 2: Write the implementation**

Create `src/render/Pages.cpp`:

```cpp
#include "render/Pages.h"
#include "core/Format.h"
#include <cstdio>
#include <cstring>

namespace pstryk {

// Palette (RGB888 -> renderer.rgb()).
struct Palette {
  uint16_t bg, text, muted, green, red, amber, barbg, line;
  explicit Palette(IRenderer& r)
    : bg(r.rgb(0x0a,0x0e,0x14)), text(r.rgb(0xe8,0xed,0xf4)),
      muted(r.rgb(0x8b,0x97,0xa8)), green(r.rgb(0x34,0xd3,0x99)),
      red(r.rgb(0xf8,0x71,0x71)), amber(r.rgb(0xfb,0xbf,0x24)),
      barbg(r.rgb(0x3b,0x4a,0x66)), line(r.rgb(0x1e,0x27,0x38)) {}
};

static void hourLabel(int hour, char* out) { std::snprintf(out, 6, "%02d:00", hour); }

static void drawStrip(IRenderer& r, const Palette& p, const char* left,
                      bool stale, int dotIdx, int dotCount) {
  r.text(16, 8, left, p.muted, 1);
  if (stale) r.text(r.textWidth(left, 1) + 28, 8, "* nieaktualne", p.amber, 1);
  // page dots, right-aligned
  int dotR = 6, gap = 8, total = dotCount * (dotR + gap);
  int x = r.width() - 16 - total;
  for (int i = 0; i < dotCount; ++i) {
    uint16_t c = (i == dotIdx) ? p.text : p.barbg;
    r.fillRoundRect(x + i * (dotR + gap), 9, dotR, dotR, dotR / 2, c);
  }
}

// Generic bar chart in the rect [x,y,w,h]; rings the live bar.
static void drawChart(IRenderer& r, const Palette& p, const std::vector<Bar>& bars,
                      int x, int y, int w, int h) {
  if (bars.empty()) { r.text(x, y + h / 2, "Brak danych", p.muted, 2); return; }
  float maxP = 0.0001f;
  for (const Bar& b : bars) if (b.price > maxP) maxP = b.price;
  maxP *= 1.1f;
  int n = (int)bars.size();
  int gap = 3;
  int bw = (w - (n - 1) * gap) / n; if (bw < 1) bw = 1;
  for (int i = 0; i < n; ++i) {
    const Bar& b = bars[i];
    int bh = (int)((b.price / maxP) * h);
    int bx = x + i * (bw + gap);
    int by = y + h - bh;
    uint16_t c = b.isCheap ? p.green : (b.isExpensive ? p.red : p.barbg);
    r.fillRect(bx, by, bw, bh, c);
    if (b.isLive) r.drawRect(bx - 1, by - 1, bw + 2, bh + 2, p.text);
  }
}

static void pageTeraz(IRenderer& r, const Palette& p, const PriceView& v) {
  char hl[6], buf[8];
  hourLabel(v.currentHour, hl);
  char lbl[24]; std::snprintf(lbl, sizeof(lbl), "TERAZ %s-%02d:00", hl, v.currentHour + 1);
  r.text(16, 34, lbl, p.muted, 1);

  uint16_t tag = v.currentBelowAvg ? p.green : p.red;
  r.text(16 + r.textWidth(lbl, 1) + 14, 34,
         v.currentBelowAvg ? "ponizej sredniej" : "powyzej sredniej", tag, 1);

  formatPln(v.currentBuy, buf);
  r.text(16, 56, buf, p.text, 8);                 // big price ~48x64 chars
  int after = 16 + r.textWidth(buf, 8) + 12;
  r.text(after, 64, "zl/kWh", p.muted, 2);
  uint16_t tc = (v.nextTrend == Trend::Up) ? p.red : (v.nextTrend == Trend::Down ? p.green : p.muted);
  const char* arrow = (v.nextTrend == Trend::Up) ? "^" : (v.nextTrend == Trend::Down ? "v" : "-");
  r.text(after, 96, arrow, tc, 4);

  // right column
  int rx = 430;
  r.drawLine(rx - 16, 44, rx - 16, 168, p.line);
  formatPln(v.currentSell, buf);
  r.text(rx, 44, "SPRZEDAZ (PV)", p.muted, 1);  r.text(rx, 56, buf, p.text, 3);
  formatPln(v.todayAvg, buf);
  r.text(rx, 92, "SREDNIA DZIS", p.muted, 1);   r.text(rx, 104, buf, p.text, 3);
  char nh[6]; hourLabel(v.nextHour, nh);
  char nl[18]; std::snprintf(nl, sizeof(nl), "NASTEPNA %s", nh);
  formatPln(v.nextBuy, buf);
  r.text(rx, 140, nl, p.muted, 1);              r.text(rx, 152, buf, tc, 3);
}

static void pageChart(IRenderer& r, const Palette& p, const PriceView& v) {
  drawChart(r, p, v.today, 16, 40, 608, 110);
  r.text(16, 156, "00", p.muted, 1);
  r.text(16 + 608 / 4, 156, "06", p.muted, 1);
  r.text(16 + 608 / 2, 156, "12", p.muted, 1);
  r.text(16 + 3 * 608 / 4, 156, "18", p.muted, 1);
  r.text(600, 156, "23", p.muted, 1);
}

static void pageExtremes(IRenderer& r, const Palette& p, const PriceView& v) {
  char hl[6], buf[8];
  // cheapest box
  r.fillRoundRect(16, 44, 296, 120, 12, r.rgb(0x10,0x2a,0x22));
  r.text(32, 56, "v NAJTANIEJ DZIS", p.green, 1);
  hourLabel(v.todayCheapest.hour, hl); r.text(32, 80, hl, p.text, 5);
  formatPln(v.todayCheapest.price, buf);
  char z1[16]; std::snprintf(z1, sizeof(z1), "%s zl/kWh", buf);
  r.text(32, 132, z1, p.muted, 2);
  // most expensive box
  r.fillRoundRect(328, 44, 296, 120, 12, r.rgb(0x2a,0x12,0x12));
  r.text(344, 56, "^ NAJDROZEJ DZIS", p.red, 1);
  hourLabel(v.todayExpensive.hour, hl); r.text(344, 80, hl, p.text, 5);
  formatPln(v.todayExpensive.price, buf);
  char z2[16]; std::snprintf(z2, sizeof(z2), "%s zl/kWh", buf);
  r.text(344, 132, z2, p.muted, 2);
}

static void pageJutro(IRenderer& r, const Palette& p, const PriceView& v) {
  drawChart(r, p, v.tomorrow, 16, 36, 608, 90);
  char hl[6], buf[8], line[40];
  hourLabel(v.tomorrowCheapest.hour, hl); formatPln(v.tomorrowCheapest.price, buf);
  std::snprintf(line, sizeof(line), "v %s  %s zl", hl, buf);
  r.text(16, 138, "Najtaniej", p.muted, 1); r.text(16, 152, line, p.green, 2);
  hourLabel(v.tomorrowExpensive.hour, hl); formatPln(v.tomorrowExpensive.price, buf);
  std::snprintf(line, sizeof(line), "^ %s  %s zl", hl, buf);
  r.text(330, 138, "Najdrozej", p.muted, 1); r.text(330, 152, line, p.red, 2);
}

void renderPage(IRenderer& r, Page page, const PriceView& v,
                bool stale, const char* clockHHMM, int pageDotIndex, int pageDotCount) {
  Palette p(r);
  r.clear(p.bg);

  char left[40];
  switch (page) {
    case Page::Teraz:    std::snprintf(left, sizeof(left), "%s  Pstryk", clockHHMM); break;
    case Page::Chart:    std::snprintf(left, sizeof(left), "Dzis"); break;
    case Page::Extremes: std::snprintf(left, sizeof(left), "Dzis"); break;
    case Page::Jutro:    std::snprintf(left, sizeof(left), "Jutro"); break;
  }
  drawStrip(r, p, left, stale, pageDotIndex, pageDotCount);

  if (!v.hasData) { r.text(16, 80, "Brak danych", p.muted, 3); r.present(); return; }
  switch (page) {
    case Page::Teraz:    pageTeraz(r, p, v); break;
    case Page::Chart:    pageChart(r, p, v); break;
    case Page::Extremes: pageExtremes(r, p, v); break;
    case Page::Jutro:    pageJutro(r, p, v); break;
  }
  r.present();
}

void renderMessage(IRenderer& r, const char* line1, const char* line2) {
  Palette p(r);
  r.clear(p.bg);
  r.text(16, 60, line1, p.text, 3);
  if (line2 && line2[0]) r.text(16, 110, line2, p.muted, 2);
  r.present();
}

}  // namespace pstryk
```

- [ ] **Step 3: Exercise pages with mock data from `main.cpp`**

Replace `src/main.cpp`:

```cpp
#include <Arduino.h>
#include "render/LongRenderer.h"
#include "render/Pages.h"

pstryk::LongRenderer gfx;
pstryk::PriceView mockView() {
  pstryk::PriceView v; v.hasData = true;
  v.currentBuy = 0.52; v.currentSell = 0.31; v.currentHour = 14;
  v.currentBelowAvg = true; v.nextTrend = pstryk::Trend::Down; v.nextBuy = 0.48; v.nextHour = 15;
  v.todayAvg = 0.61; v.liveIndex = 2;
  for (int h = 0; h < 24; ++h) {
    pstryk::Bar b; b.hour = h; b.price = 0.2f + 0.03f * h; b.isLive = (h == 14);
    b.isCheap = (h < 5); b.isExpensive = (h >= 18 && h <= 20); v.today.push_back(b);
  }
  v.todayCheapest = {3, 0.21f, false, true, false};
  v.todayExpensive = {19, 1.12f, false, false, true};
  v.hasTomorrow = true; v.tomorrow = v.today; v.tomorrowCheapest = {12, 0.19f};
  v.tomorrowExpensive = {19, 1.04f};
  return v;
}

void setup() {
  Serial.begin(115200); delay(300);
  gfx.begin();
}

int page = 0;
void loop() {
  auto v = mockView();
  pstryk::Page pages[] = {pstryk::Page::Teraz, pstryk::Page::Chart,
                          pstryk::Page::Extremes, pstryk::Page::Jutro};
  pstryk::renderPage(gfx, pages[page % 4], v, false, "14:32", page % 4, 4);
  page++;
  delay(3000);
}
```

- [ ] **Step 4: Build, upload, and visually verify all four pages**

Run: `pio run -e tdisplay_long -t upload && pio device monitor`
Expected: screen cycles every 3 s through Teraz → Wykres → Najtaniej/Najdrożej → Jutro, matching the approved mockups (big price, ringed live bar, green/red colour coding, page dots). Tweak coordinates if anything clips.

- [ ] **Step 5: Commit**

```bash
git add src/render/Pages.* src/main.cpp
git commit -m "feat(render): four price pages + status strip + message screen"
```

---

## Task 12: App state machine + wiring (`App`, `main.cpp`) — device

**Files:**
- Create: `src/app/App.h`, `src/app/App.cpp`
- Modify: `src/main.cpp` (replace mock loop with `App`)

> Ties everything together: provisioning, NTP, fetch scheduling (incl. awaiting-tomorrow), page rotation, per-second redraw, stale/error handling, BOOT re-provision.

- [ ] **Step 1: Write the header**

Create `src/app/App.h`:

```cpp
#pragma once
#include "storage/Settings.h"
#include "net/PstrykClient.h"
#include "net/WiFiProvisioner.h"
#include "render/LongRenderer.h"
#include "render/Pages.h"
#include "view/PriceView.h"
#include "core/PriceData.h"

namespace pstryk {

class App {
 public:
  void setup();
  void loop();

 private:
  void doFetch();
  void advancePage();
  void redraw();

  LongRenderer    gfx_;
  Settings        settings_;
  WiFiProvisioner provisioner_;
  PriceData       data_;
  PriceView       view_;

  bool     haveData_ = false;
  time_t   lastFetchOk_ = 0;     // UTC epoch of last successful fetch
  uint32_t nextFetchAtMs_ = 0;
  uint32_t nextRotateAtMs_ = 0;
  uint32_t lastRedrawMs_ = 0;
  int      pageIdx_ = 0;
};

}  // namespace pstryk
```

- [ ] **Step 2: Write the implementation**

Create `src/app/App.cpp`:

```cpp
#include "app/App.h"
#include "core/TimeService.h"
#include "core/PriceLogic.h"
#include "core/RefreshPolicy.h"
#include "render/pins_config.h"
#include <Arduino.h>
#include <WiFi.h>

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
```

- [ ] **Step 3: Replace `src/main.cpp` with the real entry point**

```cpp
#include <Arduino.h>
#include "app/App.h"

pstryk::App app;

void setup() { app.setup(); }
void loop()  { app.loop(); }
```

- [ ] **Step 4: Build the device firmware**

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS`.

- [ ] **Step 5: Confirm the full host suite still passes**

Run: `pio test -e native`
Expected: all suites pass (core untouched, but verify no shared header broke).

- [ ] **Step 6: Commit**

```bash
git add src/app/ src/main.cpp
git commit -m "feat(app): state machine wiring fetch, rotation, redraw, re-provision"
```

---

## Task 13: On-device integration smoke test

**Files:** none (manual verification + notes).

- [ ] **Step 1: Flash and provision**

Run: `pio run -e tdisplay_long -t upload && pio device monitor`
- On first boot the screen shows "Konfiguracja"/"WiFi" and a `Pstryk-Setup` AP appears.
- Join it from a phone, enter home WiFi + your real `sk-...` API key, save.
- Expected: device connects, syncs time, fetches, and shows the **Teraz** page with a real current price.

- [ ] **Step 2: Verify rotation and live data**

- Expected: pages auto-rotate every ~7 s (Teraz → Wykres → Najtaniej/Najdrożej, plus Jutro only if tomorrow's prices are already published). The clock in the status strip is correct local time; the chart's ringed bar matches the current hour.

- [ ] **Step 3: Verify error/stale handling**

- Temporarily power off your router (or block the device). Within the refresh interval + retries, the screen keeps showing the last data and grows a "* nieaktualne" marker after ~90 min (or shorten `kStaleSec` temporarily to verify quickly, then revert).
- Re-enable WiFi; confirm it recovers on the next fetch.

- [ ] **Step 4: Verify re-provisioning**

- Hold BOOT (GPIO 0). Expected: "Konfiguracja" screen + `Pstryk-Setup` portal reopens; changing the key/WiFi and saving reboots into normal operation.

- [ ] **Step 5: Tag the working baseline**

```bash
git tag v0.1-long-working
git commit --allow-empty -m "test: on-device smoke test passed (T-Display-S3-Long)"
```

---

## Notes for the implementer

- **Library API drift:** `Arduino_GFX_Library.h` exposes `Arduino_ESP32QSPI`, `Arduino_AXS15231`, and `Arduino_Canvas`. If class names differ in 1.3.7, check the bundled examples in `Xinyuan-LilyGO/T-Display-S3-Long/examples/GFX_AXS15231B_Image/`. Keep QSPI ≤ 32 MHz.
- **Orientation:** if the panel renders portrait/mirrored, adjust the `rotation` argument (0/1/2/3) in `LongRenderer::begin`.
- **Canvas memory:** 640×180×2 = ~230 KB — requires PSRAM (already enabled via `-DBOARD_HAS_PSRAM`). If `canvas_->begin()` fails, confirm PSRAM init in the boot log.
- **Auth fallback:** if live fetches return 401 with a known-good key, try `addHeader("Authorization", "Bearer " + apiKey_)` (the spec notes both conventions exist).
- **ASCII labels (intentional):** the built-in 6×8 font has no Polish diacritics, so labels are written without them (`sredniej`, `NASTEPNA`, `zl/kWh`). This is a deliberate v1 simplification; rendering proper `ś/ż/ł` ("średniej", "zł/kWh") comes with the nicer-font polish task deferred per spec §11.
- **Deferred (per spec §11):** touch interaction, AMOLED/e-ink renderers, TLS CA pinning, backlight PWM dimming, `full_price` toggle.
```
