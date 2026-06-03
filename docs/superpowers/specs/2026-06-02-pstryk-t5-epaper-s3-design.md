# Pstryk price display — LilyGo T5 4.7″ ePaper S3

**Date:** 2026-06-02
**Status:** Approved design — ready for implementation planning
**Board (this spec):** LilyGo **T5-4.7-S3** ePaper (plain variant) only
**Companion spec:** `2026-06-02-pstryk-tdisplay-long-design.md` (the LCD board; shares the core)

## 1. Overview

A **battery-powered, single-screen** e-paper module that shows current Pstryk
dynamic electricity prices on a LilyGo **T5 4.7″ ePaper S3** (960×540, 16-level
grayscale). Unlike the always-on T-Display-S3-Long, this board **deep-sleeps
between hourly wakes**: e-paper is bistable, so the panel holds the last image
for free while the board sleeps, giving months of battery life.

Each wake is a one-shot cycle: wake → Wi-Fi → fetch → paint the whole screen →
deep-sleep until the next refresh. Everything the Pstryk app shows for today and
tomorrow is laid out on **one static screen** (no page rotation).

This is the e-ink port the Long-board spec explicitly anticipated: the
**board-independent core is reused verbatim**, and only a new render layer plus a
new (deep-sleep) orchestrator are written.

### Goals
- Glanceable, always-visible today + tomorrow price picture on one screen.
- Multi-month battery life via hourly deep-sleep (image persists while asleep).
- Reuse the entire core (`PriceLogic`, `TimeService`, `PstrykClient`, `Settings`,
  `WiFiProvisioner`, `PriceView`) unchanged; prove the `IRenderer` seam.

### Non-goals (out of scope for v1)
- Touch interaction (the plain board has no touch; the Pro variant does — not targeted).
- Partial/fast e-paper refresh (we do one full refresh per hour; speed is irrelevant).
- Cross-wake data persistence (RAM is lost on deep sleep; a failed fetch shows an
  explicit error screen rather than stale prices — see §8). Deferred, not v1.
- Showing `full_price` (energy + distribution); v1 shows the energy price
  (`price_gross` buy / `price_prosumer_gross` sell), matching the app headline.
- ESP-IDF migration; battery fuel-gauge IC (the Pro's BQ27220); web dashboard.

## 2. Hardware

LilyGo **T5-4.7-S3** ePaper. **Board-variant caveat:** confirm the physical unit
is the *plain* T5-4.7-S3 and **not** the **T5 S3 E-Paper Pro** (adds GT911 touch +
BQ25896/BQ27220 PMIC + PCA9555/TPS65185 and maps to epdiy `epd_board_v7`). All
pins below are for the plain board (LilyGo `LilyGo-EPD47` repo, **`esp32s3`
branch**, `src/utilities.h`). Verify against the actual board before flashing.

- **MCU:** ESP32-**S3R8**, dual-core LX7 @240 MHz, Wi-Fi b/g/n + BLE.
- **Flash:** 16 MB (QIO). **PSRAM:** 8 MB **Octal (OPI)** → `memory_type = qio_opi`.
- **USB:** native USB-C (CDC + JTAG). Flash via USB-CDC.
- **Display:** 4.7″ **ED047TC1**, **960×540**, **16-level grayscale (4 bpp)**,
  parallel bus. Framebuffer = `960×540/2` = **253 KB → must live in PSRAM**
  (`ps_calloc`). Drawn via LilyGo's bundled epdiy-derived `epd_driver` (I2S parallel).
- **RTC:** onboard **PCF8563**, I2C addr **0x51**, on **SDA = GPIO 18, SCL = GPIO 17**.
- **Battery:** Li-ion connector; voltage on **GPIO 14 (ADC2)** through a **2:1
  divider**. **`epd_poweron()` must be asserted before reading** (POWER_EN gates the
  divider). **ADC2 is unusable while Wi-Fi is active** → read battery *before*
  connecting Wi-Fi.
- **Button:** single user button on **GPIO 21** (RTC-capable). **RST** present.
- **Full refresh:** ~0.6 s spec / ~1–1.5 s observed under Arduino. Fine at 1/hour.
- **Ghosting:** documented on this panel → do a **full `epd_clear()` before each
  hourly paint** (we never use partial updates, so no accumulated ghosting).

### Pin map (plain T5-4.7-S3, from `src/utilities.h` / epdiy `lilygo_board_s3.c`)
EPD data D0–D7 = GPIO **5,6,7,15,16,17,18,8** (note: I2C SDA/SCL 18/17 are shared
with the EPD bus — sequence EPD vs I2C access, don't drive both at once).
Battery ADC = **GPIO 14**. User button = **GPIO 21**. PCF8563 = I2C 0x51.

## 3. Data source — Pstryk API (reused unchanged)

Identical to the Long board; see `[[pstryk-api-contract]]` and the Long spec §3.
Summary of what the **core already implements correctly**:
- `GET https://api.pstryk.pl/integrations/meter-data/unified-metrics/?metrics=pricing&resolution=hour&window_start=<UTC ISO>&window_end=<UTC ISO>`.
- **Auth: raw key in `Authorization` (no `Bearer`)**; Bearer fallback on 401/403.
- Response `{ "frames":[...], "summary":{...} }`; **price fields nested under
  `frames[].metrics.pricing.*`** (`price_gross` buy, `price_prosumer_gross` sell,
  `is_cheap`, `is_expensive`, …). **No `is_live`** — the current hour is derived
  from the clock by `PriceLogic`.
- Windows are UTC; device converts to `Europe/Warsaw` locally.
- **Rate limit: 3 req/hr** → `429` + `Retry-After`. Our wake cadence is ≤3 req/hr
  by construction (§5).

No transport or parser changes. `PstrykClient.fetch()` and `parse()` are reused as-is.

## 4. Functional requirements

### The one screen — Layout "C" (hero + dual mini-charts), grayscale treatment "average line"
A single 960×540 static image, repainted once per refresh. All labels Polish; unit `zł/kWh`.

1. **Hero (top):** large current buy price (`price_gross` for the current local
   hour) + `zł/kWh`; a `↓ poniżej / ↑ powyżej średniej` pill; a side column with
   **Następna HH:00** (next-hour price + ▲/▼ trend), **Sprzedaż PV**
   (`price_prosumer_gross`), and **Średnia dziś**.
2. **Two mini-charts (bottom):** **Dziś** (left) and **Jutro** (right), each a 24-hour
   bar chart with:
   - a **dashed horizontal line at that day's average**; bars **above-avg dark /
     below-avg light** (maps directly to `PriceView.currentBelowAvg` logic);
   - the **cheapest hour tagged ▼ (min)** and **priciest ▲ (max)** with hour+price;
   - the **current hour ringed** (today only).
   - **Jutro is omitted** (its half collapsed/blank with a "Jutro: brak danych
     jeszcze" note) until tomorrow's frames exist — same rule as the Long board.
3. **Status bar:** last-update HH:MM, **battery glyph + %**, low-battery warning,
   Wi-Fi/stale markers.

Mockups (reference, not shipped): `.superpowers/brainstorm/65135-1780406254/content/`
(`layout-options.html`, `chart-encoding.html`).

### Config / provisioning (reused `WiFiProvisioner`)
- First boot / no saved config → captive-portal AP `Pstryk-Setup`; user enters
  Wi-Fi SSID/password **and the Pstryk API key**; saved to NVS. Reused unchanged.

### Wake / refresh schedule
- **Primary wake = RTC timer** (`esp_sleep_enable_timer_wakeup`), the reliable path.
- `RefreshPolicy::secondsUntilNextWake(now, hasTomorrow)` (new pure fn):
  - Base: sleep until the **next top-of-hour** (+~5 s guard) so the screen turns
    over when prices change.
  - **12:00–16:00 local and tomorrow not yet held:** cap sleep at **~30 min** so
    tomorrow's prices appear sooner after Pstryk publishes them.
  - One fetch per wake → **≤3 req/hr** always (cap-safe by construction).
- **Button (GPIO 21):**
  - At **every boot/wake**, sample GPIO 21; **held ≥3 s → reconfigure** (re-open
    `Pstryk-Setup`). Works regardless of wake source (hold button + tap RST always works).
  - **ext0/GPIO deep-sleep wake** on GPIO 21 enabled so a **short press wakes →
    fetch + repaint now**. *Flagged to validate on hardware* (the official LilyGo
    example demonstrates only timer wake); if button-wake proves unreliable, the
    hold-button+RST reconfigure path and timer refresh still fully work.

### Battery
- Read GPIO 14 ADC **before Wi-Fi** (ADC2/Wi-Fi conflict), with `epd_poweron()`
  asserted. Convert with the LilyGo formula (`raw/4095 * 2 * 3.3 * vref`, eFuse
  `vref`), map voltage→% on a Li-ion curve (≈4.2 V=100%, ≈3.3 V≈0%, clamp 4.2).
- Show glyph + % in the status bar; **prominent low-battery warning** when low.

## 5. Architecture

```
                 ┌─────────────── reused VERBATIM from the Long board ───────────────┐
  Pstryk API  →  PstrykClient (transport + pure parse) → PriceData → PriceLogic → PriceView
                 TimeService (UTC→Warsaw, DST)   Settings (NVS)   WiFiProvisioner   RefreshPolicy*
                 └────────────────────────────────────────────────────────────────────┘
                                              │ PriceView (display-ready struct)
                                              ▼
  NEW render layer:   IRenderer  ◄── EpdRenderer (epdiy grayscale framebuffer)
                      EpdDashboard.drawDashboard(view, status)   ← single 960×540 page
  NEW orchestrator:   SleepCycle   ← one-shot wake→fetch→paint→deep-sleep (replaces App's loop)
```

**Reused unchanged (core, board-independent):** `PstrykClient`, `PriceData`,
`PriceLogic`, `TimeService`, `Settings`, `WiFiProvisioner`, `PriceView`. The
**only core change** is one *added* pure function `RefreshPolicy::secondsUntilNextWake`
(the existing `nextRefreshMs` stays for the Long board).

**New, board-specific:**
- **`EpdRenderer`** implements the existing **`IRenderer`** interface over the
  vendored LilyGo `epd_driver`:
  - holds a 253 KB grayscale framebuffer in PSRAM; all primitives draw into it.
  - `rgb(r,g,b)` → nearest **grayscale luminance** (0=black…255=white).
  - `width()=960`, `height()=540`.
  - `fillRect/drawRect/drawLine` → `epd_fill_rect/epd_draw_rect/epd_draw_line`;
    `fillRoundRect` composed from rects + `epd_fill_circle`.
  - `text()` uses **real e-paper GFX fonts** (a small label font + a large hero
    font, bundled as epdiy C-array font headers) — **not** a scaled 6×8 bitmap, so
    the hero number is crisp. `textWidth/textHeight` via `epd_get_text_bounds`.
  - `present()` → `epd_poweron()` → `epd_clear()` (ghosting reset) →
    `epd_draw_grayscale_image(full, fb)` → `epd_poweroff()`.
  - Boot/error screens via the reused `renderMessage(IRenderer&, …)` work as-is.
- **`EpdDashboard`** — `drawDashboard(IRenderer&, const PriceView&, const StatusInfo&)`
  paints the whole Layout-C screen (hero + Dziś/Jutro charts + status bar). This is
  the e-paper analog of the Long board's `Pages`; the Long `Pages.cpp` is untouched.
- **`SleepCycle`** — the deep-sleep orchestrator (see §6). Replaces the always-on
  `App` loop for this board. `App` (Long) is unchanged.

**Build selection:** `main.cpp` chooses `App` vs `SleepCycle` by a board macro
(`-DPSTRYK_BOARD_EPAPER`). Each PlatformIO env uses `build_src_filter` to compile
only its render layer + orchestrator (mirroring how `[env:native]` already filters
`+<core/> +<view/>`), so the Long renderer and the e-paper renderer never co-compile.

## 6. Runtime — one-shot deep-sleep cycle (`SleepCycle`)

```
WAKE (timer or button/ext0)  ── RTC_DATA_ATTR survives sleep; clock from PCF8563/NTP
  1. read wake cause; sample BUTTON(GPIO21): held ≥3 s → PROVISION → save → sleep
  2. epd_poweron(); read BATTERY (GPIO14, ADC2) BEFORE Wi-Fi
  3. load Settings (NVS); no Wi-Fi/key → PROVISION (captive portal) → save → sleep
  4. CONNECT Wi-Fi (retry/backoff; hard fail → paint "Brak Wi-Fi" + short sleep, retry)
  5. SYNC TIME: NTP (configTzTime). On success → settimeofday + write PCF8563.
               On failure → seed system clock from PCF8563 (backstop).
               Clock still invalid → paint "Synchronizacja czasu…" → short sleep.
  6. FETCH one GET (window today 00:00 local → +48 h) → parse → PriceData → PriceView
  7. PAINT full screen (hero + Dziś/Jutro + status); single full refresh (no ghosting)
  8. nextWake = RefreshPolicy::secondsUntilNextWake(now, view.hasTomorrow)
     enable timer wake (+ ext0 on GPIO21); epd_poweroff(); esp_deep_sleep_start()
On FETCH error (4): 429 → short sleep honoring Retry-After (cap-safe). Auth 401/403
  → paint "Błędny klucz API — przytrzymaj przycisk, aby zmienić" → sleep. Network/
  parse → paint "Błąd pobierania HH:MM" → short retry sleep (no stale prices kept).
```

`setup()` runs the whole cycle and ends in `esp_deep_sleep_start()`; `loop()` is
effectively empty (each refresh is a fresh boot). No page rotation, no 1 s redraw.

## 7. Timekeeping across deep sleep

ESP32-S3 has no external 32 kHz crystal by default → the internal RC drifts, so the
**PCF8563 is the authoritative wall clock**. Flow:
- Each wake we have Wi-Fi anyway → run **NTP**; on success set system time **and**
  write PCF8563 (keeps it accurate, corrects drift).
- If Wi-Fi/NTP fails this wake → **read PCF8563** into the system clock so labels
  and the next-wake computation still work.
- The deep-sleep **timer** itself uses the internal RC (wake may be off by a minute
  or two); this is cosmetic — the displayed time is taken from PCF8563/NTP on wake,
  and re-NTP each wake corrects it. `TimeService` stays pure (operates on system
  time); all PCF8563 I2C lives in `SleepCycle` (board-specific), not in the core.

## 8. Error handling & edge cases

- **No cross-wake persistence (by design, v1):** RAM is lost on deep sleep, so there
  is no "keep last good data." A failed fetch paints an explicit error/stale screen
  with a timestamp, then schedules a short retry sleep. (Persisting last `PriceData`
  to NVS to survive a failed wake is **deferred** — §12.)
- **ADC2 / Wi-Fi conflict:** battery is read **before** `WiFi.begin()`; never during
  an active connection.
- **Ghosting:** every hourly paint does a full `epd_clear()` first; no partial updates.
- **Shared EPD/I2C pins (18/17):** access the PCF8563 (I2C) and the EPD bus in
  separate phases; don't interleave.
- **Bad key:** API 401/403 → "Błędny klucz API" screen; hold button → reconfigure.
- **No tomorrow yet:** Jutro half shows "brak danych jeszcze"; midday tighter wake
  (§5) catches it soon after publication.
- **DST days (23/25 h):** handled by the TZ rule in `TimeService`; charts draw
  whatever frame count exists.
- **Low battery:** prominent on-screen warning; refresh continues until the board
  can no longer boot.
- **Watchdog/crash:** reboot re-enters the cycle from NVS (no re-provisioning).

## 9. Security

- HTTPS via `WiFiClientSecure`; v1 `setInsecure()` (personal device, home network) —
  same posture as the Long board. CA pinning is a documented future option.
- API key stored in NVS, never logged in full.

## 10. Project structure (PlatformIO)

```
platformio.ini              + [env:t5_epaper_s3] (board=T5-ePaper-S3, build_src_filter,
                              -DPSTRYK_BOARD_EPAPER); existing [env:tdisplay_long] & [env:native] kept
board/T5-ePaper-S3.json     vendored LilyGo board JSON (16MB QIO flash, OPI PSRAM, native USB)
lib/EPD47/                  vendored LilyGo `LilyGo-EPD47` esp32s3-branch EPD driver
                              (epd_driver.*, i2s_data_bus.*, rmt_pulse.*, ED047TC1, fonts) + LICENSE/attribution
src/
  main.cpp                  picks App (loop) vs SleepCycle (deep sleep) by board macro
  core/                     UNCHANGED + RefreshPolicy gains secondsUntilNextWake()
  net/                      UNCHANGED (PstrykClient, WiFiProvisioner)
  view/PriceView.h          UNCHANGED
  render/
    IRenderer.h             UNCHANGED
    LongRenderer.*, Pages.* UNCHANGED (Long board only)
    EpdRenderer.{h,cpp}     NEW — IRenderer over epdiy grayscale framebuffer
    EpdDashboard.{h,cpp}    NEW — single-screen Layout-C paint
  app/
    App.{h,cpp}             UNCHANGED (Long board only)
    SleepCycle.{h,cpp}      NEW — deep-sleep orchestrator + PCF8563 + battery + button
test/
  test_refresh/             + cases for secondsUntilNextWake (top-of-hour, midday window, DST)
  (existing native tests unchanged)
```

## 11. Dependencies, build & testing

### Build (`[env:t5_epaper_s3]`)
- `platform = espressif32`, `framework = arduino`, `board = T5-ePaper-S3`
  (`boards_dir = board`), `board_build.partitions = default_16MB.csv`,
  `board_build.arduino.memory_type = qio_opi`, `board_build.memory_type = qio_opi`.
- `build_flags`: `-std=gnu++17 -I src -DBOARD_HAS_PSRAM -DARDUINO_USB_CDC_ON_BOOT=1
  -DARDUINO_USB_MODE=1 -DLILYGO_T5_EPD47_S3 -DPSTRYK_BOARD_EPAPER`.
- `build_src_filter`: core + view + net + `render/IRenderer` + `render/Epd*` +
  `app/SleepCycle` + `main.cpp` (exclude `render/Long*`, `render/Pages*`, `app/App*`).
- `lib_deps`: `bblanchon/ArduinoJson@^7` (shared), `tzapu/WiFiManager@^2`
  (provisioning), `lewisxhe/SensorLib` (PCF8563). The EPD driver is **vendored in
  `lib/EPD47/`** (LilyGo's esp32s3 fork is not a clean PlatformIO library).
  **No Arduino_GFX** for this board (epdiy has its own drawing + fonts).

### Testing (TDD — pure core on host)
- **`[env:native]`** Unity tests, no hardware:
  - **New:** `RefreshPolicy::secondsUntilNextWake` — next top-of-hour math, the
    12:00–16:00 tighter window (with/without tomorrow), and a DST-boundary day.
  - Existing `PriceLogic` / `TimeService` / parser tests reused as-is (core unchanged).
- **On-device smoke (manual):** provisioning; one live fetch with the real key; full
  screen paints correctly in grayscale; battery % reads sane before Wi-Fi; PCF8563
  read/write across a real deep-sleep cycle; timer wake at ~top-of-hour; button hold
  → reconfigure; (validate) short-press ext0 wake → refresh.

## 12. Future / deferred
- Persist last `PriceData` to NVS so a failed wake can repaint last-known prices.
- Partial e-paper refresh for sub-hour updates (not needed at 1/hour).
- Button **short-press ext0 wake** hardening (fallback already works).
- Support the **T5 S3 E-Paper Pro** variant (touch + PMIC, epdiy `epd_board_v7`).
- TLS root-CA pinning; deeper Li-ion SoC estimation; optional `full_price` toggle.

## 13. References
- Board repo (S3): https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/tree/esp32s3
  (`src/utilities.h`, `examples/demo/demo.ino`, `examples/sleep/sleep.ino`,
  `boards/T5-ePaper-S3.json`, `platformio.ini`)
- e-paper driver: https://github.com/vroland/epdiy (`src/displays.h` ED047TC1,
  `src/board/lilygo_board_s3.c`, `epd_board_v7`)
- Pro variant: https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO
- Pstryk API contract + parse notes: memory `[[pstryk-api-contract]]`,
  `[[pstryk-parse-nesting-bug]]`; Long-board spec `2026-06-02-pstryk-tdisplay-long-design.md`
- Ghosting/refresh: epdiy #146, LilyGo-EPD47 #93
```
