# Over-the-air (OTA) firmware updates — both boards

**Date:** 2026-06-24
**Status:** Approved design — ready for implementation planning
**Boards:** LilyGo **T5-4.7-S3** ePaper *and* **T-Display-S3-Long** (AMOLED)
**Related specs:** `2026-06-02-pstryk-t5-epaper-s3-design.md`, `2026-06-02-pstryk-tdisplay-long-design.md`

## 1. Overview

Add **silent, signed, pull-based OTA updates** so devices built from the public
README stay current without a USB cable. On its normal cycle a device checks
GitHub Releases, downloads the correct **per-board** firmware binary, **verifies a
cryptographic signature**, flashes it to the inactive bank, reboots, runs a
self-test, and **auto-rolls-back** if the new image fails. No user interaction.

The existing network stack (`WiFiClientSecure` + `HTTPClient`) is reused — it is
exactly what Arduino's `Update`/`HTTPUpdate` rides on. A small, board-agnostic OTA
module is added alongside `PstrykClient`, matching the project's hand-rolled,
minimal-dependency style.

### Goals
- Builders auto-receive firmware updates from GitHub Releases, hands-off.
- A bad release can never brick a remote device (dual-bank auto-rollback).
- A tampered or forged image is rejected before flashing (image signature).
- The currently-loaded firmware version is visible on-screen on both boards.

### Non-goals (out of scope for v1)
- Local/LAN push (ElegantOTA, ArduinoOTA/espota) — can't reach strangers' LANs.
- ESP-IDF `esp_https_ota` + Secure Boot v2 / eFuse anti-rollback — burns
  irreversible eFuses on every builder's device; unacceptable for open distribution.
- Delta/compressed images, A/B staged rollouts, update telemetry/opt-in channels.
- Manifest signing (binary signature is the trust anchor in v1; noted as hardening).
- Cert pinning / root-CA bundle validation (signature is authoritative; noted as
  an easy hardening add).

## 2. Decisions (locked)

| Axis | Decision |
|---|---|
| Audience | Distribute to others (public, pull-based) |
| Mechanism | Pull from **GitHub Releases** + per-board JSON manifest |
| Apply policy | **Silent automatic** + dual-bank **auto-rollback** |
| Integrity | **Signed firmware images** (compiled-in public key) |
| Scope | **Both boards** |
| Implementation | **Arduino built-ins** (`Update`/`HTTPUpdate`) + a custom `OtaUpdater` module |
| Version display | Small on-screen version label on both boards |

## 3. Architecture & components

### New components
*(Pure logic lives in `core/` and is unit-tested in the `native` env; `net/` is device-only.)*
- **`src/core/Version.{h,cpp}`** — `FIRMWARE_VERSION` (injected at build via a
  CI-generated `core/FirmwareVersionGen.h`, defaults to `"0.0.0-dev"` locally),
  `PSTRYK_BOARD_ID` (`"epaper"` / `"amoled"`, from the `PSTRYK_BOARD_EPAPER` define),
  `isNewer(a, b)` semver compare, and `isDevVersion(v)`. Pure logic, native-tested.
- **`src/core/OtaManifest.{h,cpp}`** — `struct OtaManifest { std::string version,
  url, sha256, board; unsigned long size; }` + `bool parseManifest(const char* json,
  OtaManifest& out)` using ArduinoJson, mirroring `PstrykParse`. Pure logic, native-tested.
- **`src/core/OtaPolicy.{h,cpp}`** — `shouldApplyUpdate(manifest, currentVersion,
  boardId)` (board match + dev-guard + strict-newer) and `dueForOtaCheck(...)` rate
  limit. Pure logic, native-tested.
- **`src/net/OtaUpdater.{h,cpp}`** (device-only) — orchestration using the pure
  `core/` helpers: fetch manifest over HTTPS → `shouldApplyUpdate` gate → stream
  `.bin` through `Update` with appended-signature verification → reboot (the new
  image boots as `PENDING_VERIFY`).
- **`src/net/OtaRollback.{h,cpp}`** (device-only) — overrides arduino-esp32's weak
  `verifyRollbackLater()` (returns true, so the core does not auto-confirm a pending
  image) and provides `confirmRunningImageValid()` to mark the image valid after a
  health check. Uses `CONFIG_APP_ROLLBACK_ENABLE=y` (on in the stock S3 build;
  `ANTI_ROLLBACK` off → no eFuse burning).
- **`src/net/ota_public_key.h`** (device-only) — compiled-in RSA-3072 public key as
  PEM bytes + a trailing NUL (required by `mbedtls_pk_parse_public_key`). Generated
  once; the matching private key lives only in the `OTA_SIGNING_KEY` CI secret + an
  offline backup.

### Changed
- **`src/app/SleepCycle.cpp`** (e-paper) — after a successful fetch+paint, WiFi
  still up, call `OtaUpdater::runOnce()` gated by an `RTC_DATA_ATTR` "last-checked"
  timestamp (≤ once / 24 h). Never sacrifices a price refresh; only runs on an
  already-successful connected cycle.
- **`src/app/App.cpp`** (AMOLED) — a periodic check (~every 6 h) folded into the
  existing `loop()` cadence next to `doFetch()`.
- **`src/render/EpdDashboard.cpp`** + **`src/render/Pages.cpp`** — render the
  version label (see §8).
- **`platformio.ini`** — `tdisplay_long` partitions `huge_app.csv` → dual-OTA
  table; `-DFIRMWARE_VERSION=...` on both device envs; public-key include path.
- **`.github/workflows/release.yml`** (new) — build → sign → manifest → publish.

### Boundaries
`OtaUpdater` depends only on `WiFiClientSecure`/`HTTPClient` + `Update` + `Version`
+ `OtaManifest`. It has no knowledge of rendering or prices, and is invoked at a
single call site per board. The render layer reads `FIRMWARE_VERSION` directly
(compile-time constant) — no state plumbed through `EpdStatus` or `renderPage`.

## 4. Data flow

1. Maintainer tags `vX.Y.Z` and pushes the tag.
2. CI builds both envs, injecting `-DFIRMWARE_VERSION="X.Y.Z"`.
3. CI signs each `.bin` (private key from a GitHub Actions secret) and computes SHA-256.
4. CI publishes binaries + `manifest-epaper.json` + `manifest-amoled.json` as
   release assets.
5. A device, on its normal connected cycle and past its rate-limit window, GETs
   `…/releases/latest/download/manifest-<board>.json` over HTTPS.
6. It parses the manifest and applies the **strict-newer** gate
   (`isNewer(manifest.version, FIRMWARE_VERSION)` and `manifest.board == BOARD_ID`).
7. If eligible, it streams the `.bin` into the inactive OTA slot via `Update`.
8. `Update` verifies the appended signature against the compiled-in public key.
   Invalid → abort, keep running.
9. Valid → set the boot partition to the new slot and reboot.
10. The new firmware boots and, once core subsystems prove healthy, calls
    `esp_ota_mark_app_valid_cancel_rollback()` — e-paper: right after display/PSRAM
    init, before Wi-Fi (so a transient network blip on a later wake can't cause a
    false rollback on the sleeping board); AMOLED: right after the first Wi-Fi
    connect. If the new image crashes or resets before reaching that point, the
    bootloader reverts to the previous slot on the next reset. Confirmation is
    deliberately **not** gated on a successful price fetch: on the always-on AMOLED a
    fetch failure never triggers a reset, so gating on it would give no rollback
    benefit while risking a false rollback of a working-but-API-degraded image.

## 5. Versioning, CI & release pipeline

- **Version source:** semver git tags (`v1.4.0`). Local/dev builds default to
  `"0.0.0-dev"`; `shouldApplyUpdate()` excludes any version containing `-dev`, so a
  dev board never self-updates regardless of how new a release is.
- **Build:** `release.yml` triggers on `v*` tags; runs `pio run -e t5_epaper_s3`
  and `pio run -e tdisplay_long`.
- **Sign:** each `.bin` signed with the private key (GitHub Actions secret);
  SHA-256 computed for the manifest.
- **Publish:** binaries + both manifests attached to the GitHub Release. Devices
  read the stable redirect URL
  `https://github.com/hoffmannkrzysztof/pstryk_esp32/releases/latest/download/manifest-<board>.json`
  — no API call, no token.
- The existing `tests.yml` remains the PR/`main` gate (native unit tests).

### Manifest shape (per board)
```json
{
  "board": "epaper",
  "version": "1.4.0",
  "url": "https://github.com/hoffmannkrzysztof/pstryk_esp32/releases/download/v1.4.0/firmware-epaper.bin",
  "size": 1331840,
  "sha256": "…"
}
```

## 6. Security model

- **Trust anchor = the image signature**, not the transport. A compiled-in public
  key verifies the appended signature at `Update.end()`; a forged/tampered image is
  rejected even if the host or a mirror is compromised.
- **HTTPS** is used for manifest and binary (defense in depth). Because the
  signature is authoritative, keeping `setInsecure()` on the OTA path is acceptable
  for v1 (no cert-bundle maintenance). Validating GitHub's cert is an easy hardening
  add later.
- **Downgrade/replay:** the strict-newer gate blocks rolling a device back to an
  older signed build via a tampered manifest. Residual risk (replaying an old
  *validly-signed* binary) is bounded; optional manifest signing closes it later.
- **Key management caveat:** the public key is baked into firmware, so rotating it
  requires first shipping an update signed by the **old** key. Generate the keypair
  once, back up the private key offline, never commit it.

## 7. Per-board specifics

### T5-ePaper-S3
- Already on `default_16MB.csv` (dual `app0`/`app1` + `otadata`) → **no partition
  change**.
- OTA check rides an already-successful connected wake, rate-limited to once / 24 h
  via an `RTC_DATA_ATTR` timestamp. A download is a few seconds of radio,
  infrequent → negligible battery impact.
- Rollback self-test confirms the image right after display/PSRAM init succeeds
  (before Wi-Fi), so a transient network blip on a later wake can't trigger a false
  rollback on this sleeping board; a build that crashes during boot never reaches the
  confirm call and is rolled back.

### T-Display-S3-Long (AMOLED)
- Currently `huge_app.csv` — a **single** app slot, so OTA is impossible as-is.
- Switch to a dual-app 16 MB table (`default_16MB.csv`). The project uses **NVS,
  not SPIFFS** (settings in NVS; fonts/images compiled in), so the move is low-risk.
- This is a **one-time, breaking USB reflash** for existing AMOLED owners (you
  cannot add a second app slot over-the-air while the firmware occupies the only
  one). **Must be documented** in the README and the first release notes.
- Always-on + always-connected → the periodic check is trivial in `loop()`.
- **Rollback caveat:** if a bad new image cannot connect Wi-Fi, `WiFiManager`
  (`setConfigPortalTimeout(0)`) parks it in the captive portal indefinitely, so it
  never resets and the bootloader's next-reset rollback does not fire on its own — a
  manual power-cycle rolls it back. This is the pre-existing Wi-Fi-failure behaviour,
  not an OTA regression, but it is the weakest point of AMOLED auto-rollback. Verify
  both a forced-bad-boot AND a bad-Wi-Fi image recover during on-device testing
  (§10 / plan Task 15); a bounded portal timeout on a `PENDING_VERIFY` boot is a
  recommended follow-up hardening.

### Wrong-board guard
The per-board manifest URL is the primary guard; the manifest also carries `board`,
and a device ignores any manifest whose `board` ≠ its `BOARD_ID`.

## 8. Firmware version display

Show the loaded version unobtrusively so it's visible at a glance and in bug reports.

- **Placement:** a small, low-contrast `vX.Y.Z` in a **corner of the status strip**
  on the main screen of both boards, **and** on the config/boot/error screens
  (`drawMessage` / `renderMessage`), where it is most useful during setup.
- **Source:** render code includes `core/Version.h` and draws `FIRMWARE_VERSION`
  directly — no plumbing through `EpdStatus` or `renderPage` signatures.
- **e-paper:** drawn in `EpdDashboard.cpp` (`drawDashboard` + `drawMessage`).
- **AMOLED:** drawn in `Pages.cpp` (`renderPage` status strip + `renderMessage`).
- Dev builds show `v0.0.0-dev`, which doubles as an at-a-glance "this is not a
  released build" indicator.

## 9. Failure handling

All OTA failures are non-fatal — the device has already shown prices for the cycle.

| Failure | Behaviour |
|---|---|
| No WiFi / manifest fetch fails | Skip OTA this cycle |
| Manifest parse error | Skip, log, retry next window |
| Not newer / wrong board | No-op |
| Download interrupted | `Update` aborts, partition unchanged, keep running |
| Bad signature | Reject, log, keep running; retry next window |
| Bad boot after flash | Bootloader auto-rollback to previous slot |
| GitHub load | ≤ 1×/day per device over a CDN redirect — trivial |

## 10. Testing

### Native unit tests (new `test_ota_*`, mirroring `test_parse` / `test_logic`)
- Manifest parsing: valid, missing fields, malformed JSON, wrong types.
- Semver compare: newer / older / equal / pre-release / malformed inputs.
- Board selection: correct manifest accepted, mismatched `board` ignored.
- Update gate: strict-newer + rate-limit "should update?" decisions.

### On-device manual matrix (hardware-only paths)
- vN → vN+1 happy path: silent download, reboot, mark-valid.
- Deliberately bad signature build → image rejected, device keeps running.
- Forced bad boot (e.g. crash before mark-valid) → rollback to previous slot.

(Signature verification and flashing can't run in `native`; they live here.)

## 11. Confirmed defaults
- Check cadence: e-paper ≤ 1×/24 h; AMOLED ~6 h.
- Signature scheme: RSA-3072 + SHA-256 (Arduino `Update` signed-update convention).
- Manifest at `releases/latest/download/manifest-<board>.json`.
- `setInsecure()` retained on the OTA path for v1 (signature is the trust anchor).

## 12. Rollout / migration notes
- **e-paper:** ships OTA-capable on the first release with no user action.
- **AMOLED:** existing owners must perform **one final USB flash** onto the new
  partition table to get onto the OTA track. New builders flash the OTA table from
  the start. Call this out prominently in the README and release notes.
- The first signed release establishes the public key baked into all future
  firmware — treat the keypair as long-lived and back it up before tagging.

## 13. Risks & mitigations
- **Bad release reaches the fleet** → dual-bank auto-rollback + a self-test gate
  before `mark_app_valid`.
- **Compromised host/mirror** → image signature verified on-device.
- **Lost private key** → no future updates can be signed; mitigate with offline
  backup. **Leaked private key** → attacker can sign images; mitigate by treating it
  as a high-value secret and (future) manifest signing / key rotation via an
  old-key-signed update.
- **AMOLED partition migration friction** → unavoidable one-time reflash; reduce
  pain with clear docs and a ready-to-flash binary.
