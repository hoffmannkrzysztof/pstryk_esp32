# Pstryk price display — LilyGo T-Display-S3-Long

**Date:** 2026-06-02
**Status:** Approved design — ready for implementation planning
**Board (this spec):** LilyGo **T-Display-S3-Long** only

## 1. Overview

A small always-on, mains/USB-powered desk/wall module that shows current Pstryk
dynamic electricity prices on a LilyGo **T-Display-S3-Long** (180×640 wide-bar
LCD). It fetches hourly price data from the Pstryk API over WiFi and displays it
across auto-rotating pages, mirroring the key information from the Pstryk app.

The firmware is structured as a **board-independent core** plus a **thin
board-specific render layer**, so the same logic can later drive the user's
AMOLED and e-ink boards by writing only a new renderer. Those other boards are
**out of scope** for this spec — but the seams for them are part of the design.

### Goals
- Glanceable current price + supporting context, passively (no interaction needed).
- Reliable, rate-limit-safe data fetching, including reliably catching tomorrow's
  prices even when Pstryk publishes them late.
- Clean separation of data/logic from rendering, to enable later board reuse.

### Non-goals (out of scope for v1)
- Touch interaction (panel has touch; v1 is fully passive).
- AMOLED / e-ink renderers (only the abstraction seam is built now).
- Battery/PMU power management (assume USB/mains-powered, always on).
- Showing `full_price` (energy + distribution); v1 shows the energy price
  (`price_gross` buy / `price_prosumer_gross` sell), matching the app's headline price.
- Historical logging, cost/consumption metrics, web dashboard.

## 2. Hardware

LilyGo **T-Display-S3-Long** (NOTE: distinct from the regular `T-Display-S3`;
the correct repo is `Xinyuan-LilyGO/T-Display-S3-Long`).

- **MCU:** ESP32-S3R8, dual-core LX7 @240 MHz, WiFi b/g/n + BLE.
- **Flash:** 16 MB (QIO, 80 MHz). **PSRAM:** 8 MB Octal (OPI).
- **USB:** native USB-C (JTAG + USB-CDC serial). VID/PID `0x303A/0x1001`
  (matches the connected `usbmodem` device).
- **Display:** 3.4" TFT LCD, 180×640 native portrait (used as 640×180 landscape
  bar), controller **AXS15231B over QSPI** (`SPI2_HOST`).
- **Touch:** AXS15231B capacitive, I2C addr `0x3B` — **present but unused in v1**.

### Pin map (from LilyGo `pins_config.h`)
Display (QSPI): `CS=12, SCK=17, D0=13, D1=18, D2=21, D3=14, RST=16, BL=1`
(no dedicated D/C pin — QSPI carries command/address bits). QSPI clock **≤32 MHz**,
`SPI_MODE0` (corruption observed at 40 MHz). Backlight = GPIO 1 driven HIGH.
Touch (unused v1): `SDA=15, SCL=10, INT=11, RST=16` (RST shared with display).
Button **BOOT = GPIO 0** — used to re-open the captive portal.

### Graphics driver
**Arduino_GFX** (`moononournation/GFX Library for Arduino@1.3.7`):
`Arduino_ESP32QSPI(CS,SCK,D0,D1,D2,D3)` bus + `Arduino_AXS15231(bus, RST=16,
rotation, IPS=false, 180, 640)`. TFT_eSPI does **not** support this QSPI panel.
LVGL is intentionally **not** used (version-locked to 8.3.0 with forced software
rotation; overkill for passive pages).

## 3. Data source — Pstryk API

- **Endpoint:** `GET https://api.pstryk.pl/integrations/meter-data/unified-metrics/`
  `?metrics=pricing&resolution=hour&window_start=<UTC ISO8601>&window_end=<UTC ISO8601>`
- **Auth:** header `Authorization: sk-<key>` (raw key, **no** `Bearer` prefix).
  Fallback: if a request 401s, retry once with `Bearer ` prefix. `Accept: application/json`.
- **Windows are UTC only.** `for_tz` is not allowed with `resolution=hour`, so the
  device converts UTC→`Europe/Warsaw` locally for all labels.
- **Response:** `{ "frames": [...], "summary": {...} }`. Each hourly frame:
  - `start`, `end` — ISO 8601 UTC; `start` marks the start of the hour the frame covers.
  - `price_gross` — **buy** price, PLN/kWh, VAT-incl. → headline "current price".
  - `price_prosumer_gross` — **sell** (PV/prosument) price, PLN/kWh.
  - `is_live` — true only on the frame containing "now" → resilient current-hour marker.
  - `is_cheap` / `is_expensive` — Pstryk-flagged hours.
  - (also present, unused v1: `price_net`, `full_price`, `dist_price`, etc.)
- **Units:** PLN/kWh (e.g. `0.52` → displayed `0,52 zł/kWh`, Polish comma separator).
- **Rate limit:** **3 requests/hour** per endpoint → `429` with `Retry-After`. No daily cap.
  Personal/non-commercial use only (fine for this device).
- **API key source:** Pstryk app/web → Konto → "Urządzenia i integracje" → generate
  key (shown once). User already has a key.

There is no dedicated "current price" / "cheapest" endpoint — these are derived
client-side from the day's frames.

## 4. Functional requirements

### Pages (auto-rotating, ~7 s each; passive)
Rotation order: **Teraz → Wykres 24h → Najtaniej/Najdrożej → Jutro**.
The current page **redraws every ~1 s** so the live hour/clock stays correct — no
network call for redraws. All labels Polish; unit `zł/kWh`.

1. **Teraz** — big current buy price (`price_gross` of the `is_live` frame) with a
   next-hour trend arrow (▲ red / ▼ green) and a "poniżej/powyżej średniej" tag.
   Right column: **Sprzedaż (PV)** `price_prosumer_gross`, **Średnia dziś**, **Następna <HH:00>**.
2. **Wykres 24h** — today's hourly bar chart; current hour ringed, cheap hours
   green, expensive hours red; axis ticks at 00/06/12/18/23; header shows today's average.
3. **Najtaniej / Najdrożej** — today's cheapest and most-expensive hours as two
   colour-coded blocks (hour + price).
4. **Jutro** — tomorrow's 24h chart with tomorrow's own cheapest/most-expensive
   marked underneath. **Skipped from rotation entirely until tomorrow's frames
   exist** (no placeholder); joins automatically once published.

Mockups of all four pages: `.superpowers/brainstorm/.../layouts.html` and
`layouts-v2.html` (reference; not shipped).

### Config / provisioning
- **Captive-portal setup** via WiFiManager: first boot (or no saved config) starts
  an AP `Pstryk-Setup`; the user enters WiFi SSID/password **and the Pstryk API
  key** (custom WiFiManager field) in a web form. Saved to NVS (Preferences).
- Holding **BOOT (GPIO 0)** re-opens the captive portal to change WiFi/key.

### Refresh & rate-limit strategy
- Single request per refresh; window = **today 00:00 local → +48 h** (one call
  brings tomorrow along once published). Convert local day-boundary to UTC for the
  window params.
- **Base cadence: every 30 min** (2 req/hr) — keeps the live hour current.
- **"Awaiting tomorrow" mode:** from **12:00 local** until `PriceData` contains any
  frame dated tomorrow, poll at the tightest cap-safe cadence — **every 20 min**
  (= 3 req/hr exactly). Pstryk usually publishes ~12:00 but **can be late**, so we
  keep checking until it appears, then revert to 30 min.
- Always honor `429 Retry-After`; never exceed 3 req/hr.

## 5. Architecture

**Core (board-independent — reused verbatim on future boards):**
- `PstrykClient` — split into *transport* (HTTPS GET, headers, 429/backoff) and a
  **pure `parse(String)→PriceData`** function (host-testable).
- `PriceData` — the parsed hourly frames (start UTC, `price_gross`, `price_prosumer_gross`,
  `is_live`/`is_cheap`/`is_expensive`).
- `PriceLogic` — pure functions: current price, cheapest/most-expensive hour,
  daily average, next-hour trend, "is tomorrow present".
- `TimeService` — NTP sync + `Europe/Warsaw` DST via POSIX TZ
  `CET-1CEST,M3.5.0,M10.5.0/3`; UTC→local hour; current local date / tomorrow boundary.
- `Settings` — load/save WiFi creds + API key + options in NVS.

**Connectivity:**
- `WiFiProvisioner` — WiFiManager wrapper with the extra API-key field; connect or
  open captive portal.

**Render layer (board-specific — the only part rewritten per board):**
- `IRenderer` — abstract: `begin()`, drawing primitives, `showMessage(text)`.
- `LongRenderer` — Arduino_GFX implementation (QSPI panel, landscape, backlight).
- `Pages` — `drawTeraz / drawChart / drawExtremes / drawJutro`, each taking a
  `PriceView` + `IRenderer`.

**Handoff seam:**
- `PriceView` — a plain display-ready struct produced by `PriceLogic` from
  `PriceData`. Pages consume only `PriceView`, so logic never touches pixels and
  rendering never touches the API.

**Orchestration:**
- `App` — state machine + a page-rotation timer + a refresh scheduler.

## 6. Data flow & state machine

```
BOOT → load Settings (NVS)
  ├─ no WiFi/key ─→ PROVISION (captive portal) ─→ save ─→ reboot
  └─ have config ─→ CONNECT_WIFI
        ↓ (retry+backoff; long fail or BOOT held → PROVISION)
     SYNC_TIME (NTP; is_live covers "current" until synced)
        ↓
     FETCH (one GET, window=today→+48h) ──→ parse → PriceData → PriceView
        ↓                                   ↑
     READY ───────────────────────────────┘
       • rotate pages every ~7 s (skip Jutro if no tomorrow frames)
       • redraw current page every ~1 s (no network)
       • refresh scheduler: 30 min base / 20 min while awaiting tomorrow
       • on fetch error → keep last PriceData, show stale marker, backoff retry
```

## 7. Error handling & edge cases

- **No config / bad key:** missing WiFi → captive portal. API `401/403` → "Błędny
  klucz API" screen; BOOT re-opens captive portal.
- **Current hour without clock:** the `is_live` flag identifies the current frame
  even before NTP syncs; NTP retries in background (needed for hour labels and the
  today/tomorrow date boundary). Show "Synchronizacja czasu…" only if NTP never syncs.
- **Fetch failures:** `429` → honor `Retry-After`, keep last data. Network/TLS/5xx/
  parse errors → keep last good data, retry with backoff. If newest successful
  fetch is older than ~90 min → small **"nieaktualne"** marker on the status strip.
- **Data gaps:** missing/empty frames → affected page shows "Brak danych"; tomorrow
  absent → Jutro page skipped.
- **DST days (23/25 h):** handled by the TZ rule; chart draws whatever frame count exists.
- **Robustness:** always-on, no sleep, backlight on. Watchdog fed; crash → reboot →
  re-enter from NVS (no re-provisioning).

## 8. Security

- HTTPS via `WiFiClientSecure`. v1 default: **`setInsecure()`** (skip TLS cert
  validation) — acceptable for a personal device on a home network. Pinning
  Pstryk's root CA is a documented future option (§11).
- API key stored in NVS, never logged in full.

## 9. Project structure (PlatformIO)

```
platformio.ini              env: Arduino; board=T-Display-Long (boards_dir=./board)
board/T-Display-Long.json   from LilyGo repo (PSRAM/16MB/native-USB flags)
src/
  main.cpp                  setup/loop → App
  App.{h,cpp}               state machine, rotation timer, refresh scheduler
  core/
    PstrykClient.{h,cpp}    transport + pure parse()
    PriceData.h
    PriceLogic.{h,cpp}
    TimeService.{h,cpp}
    Settings.{h,cpp}
  net/
    WiFiProvisioner.{h,cpp}
  render/
    IRenderer.h
    LongRenderer.{h,cpp}    Arduino_GFX
    Pages.{h,cpp}           drawTeraz/drawChart/drawExtremes/drawJutro
    pins_config.h           from LilyGo
  view/
    PriceView.h             core→render handoff struct
test/
  test_pricelogic/          native unit tests
  test_timeservice/
  test_parse/
  fixtures/sample_pricing.json
```

## 10. Dependencies, build & testing

### Build config (`platformio.ini`)
- `platform = espressif32`, `framework = arduino`, `board = T-Display-Long`
  (`boards_dir = ./board`), `board_build.partitions = huge_app.csv`.
- Flags (from board JSON): `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1
  -DBOARD_HAS_PSRAM -DARDUINO_RUNNING_CORE=1 -DARDUINO_EVENT_RUNNING_CORE=1`.
- `monitor_speed = 115200`.
- `lib_deps`: `moononournation/GFX Library for Arduino@1.3.7`,
  `tzapu/WiFiManager`, `bblanchon/ArduinoJson@^7`.
- Toolchain already present locally: PlatformIO with `espressif32` platform;
  device on `/dev/cu.usbmodem1101` (native USB; manual download mode = hold BOOT,
  tap RST, release).

### Testing (TDD — pure core tested first, on host)
- **PlatformIO `native` env**, Unity tests, no hardware:
  - `PriceLogic`: fixture frames → correct current / cheapest / most-expensive /
    daily-avg / next-hour trend.
  - `TimeService`: UTC→Warsaw incl. DST; today/tomorrow boundary logic.
  - **Parser:** `parse(String)→PriceData` against `fixtures/sample_pricing.json`,
    incl. tomorrow-absent and malformed inputs.
- **On-device smoke test** (manual): provisioning flow, one live fetch with the
  real key, page rotation, chart rendering, `429`/stale behavior.

The seams (`IRenderer`, pure `parse()`, `PriceView`) are what make the future
AMOLED/e-ink port "write a new renderer" rather than a rewrite.

## 11. Future / deferred
- AMOLED and e-ink renderers (reuse the entire core via `IRenderer`).
- Touch interaction (pause/advance pages) — hardware ready, code deferred.
- TLS root-CA pinning instead of `setInsecure()`.
- Optional backlight PWM brightness / night dimming; battery+PMU support.
- Optional `full_price` (energy + distribution) display toggle.

## 12. References
- Board repo: https://github.com/Xinyuan-LilyGO/T-Display-S3-Long
- Pstryk OpenAPI: https://api.pstryk.pl/integrations/schema/ · Swagger:
  https://api.pstryk.pl/integrations/swagger/ · API terms: https://pstryk.pl/regulamin-api
- API-key how-to: https://pstryk.pl/blog/co-nowego-w-aplikacji-pstryk-2-klucze-api
- Reference integrations (response shapes): https://github.com/balgerion/ha_Pstryk ·
  https://github.com/aLAN-LDZ/Pstryk_HA
- TFT_eSPI QSPI verdict: https://github.com/Bodmer/TFT_eSPI/discussions/2962
```
