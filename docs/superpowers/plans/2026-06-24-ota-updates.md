# OTA Firmware Updates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add silent, signed, pull-based OTA firmware updates from GitHub Releases to both the T5-ePaper-S3 and T-Display-S3-Long boards, with dual-bank auto-rollback and an on-screen version label.

**Architecture:** Pure, native-tested logic (version compare, manifest parse, update gate) lives in `core/`; the Arduino-coupled download/verify/flash orchestration lives in `net/`. Each board calls a single `OtaUpdater::runOnce()` at a safe point in its existing cycle. Firmware images are RSA-3072/SHA-256 signed by CI and verified on-device via arduino-esp32's `Update.installSignature()`; the stock S3 bootloader's rollback (`CONFIG_APP_ROLLBACK_ENABLE=y`) reverts a bad image that fails an on-boot health check.

**Tech Stack:** PlatformIO + arduino-esp32 3.3.8, ArduinoJson 7, `Update`/`HTTPClient`/`WiFiClientSecure`, `esp_ota_ops`, Unity (native tests), GitHub Actions + `bin_signing.py`.

---

## Deviations from the spec (intentional, grounded in the installed toolchain)

- **`OtaManifest` and the gate logic live in `core/`, not `net/`.** The `native` test env only compiles `core/` + `view/`, so pure logic must be in `core/` to be unit-tested. The spec called these "pure, unit-testable"; this realizes that.
- **Dev builds are gated off explicitly.** The spec's one-liner ("0.0.0-dev is never newer") was imprecise — a `0.0.0-dev` device *would* see `1.0.0` as newer. Instead, `shouldApplyUpdate()` returns false whenever the running version is the dev sentinel, so dev boards never self-update.
- **Firmware version is injected via a generated header** (`src/core/FirmwareVersionGen.h`, git-ignored) rather than a `-D` flag, avoiding brittle nested-quote escaping in CI. Local builds fall back to `0.0.0-dev`.
- **Rollback uses the core's `verifyRollbackLater()` weak hook** + an explicit `esp_ota_mark_app_valid_cancel_rollback()` after a health check — confirmed available on the stock arduino-esp32 3.3.8 S3 bootloader.

These are reflected back into the spec in Task 14.

---

## File structure

| File | Responsibility |
|---|---|
| `src/core/Version.h` / `.cpp` | `FIRMWARE_VERSION`, `PSTRYK_BOARD_ID`, `isNewer()`, `isDevVersion()` (pure) |
| `src/core/OtaManifest.h` / `.cpp` | Manifest struct + `parseManifest()` (pure, ArduinoJson) |
| `src/core/OtaPolicy.h` / `.cpp` | `shouldApplyUpdate()`, `dueForOtaCheck()` (pure) |
| `src/net/ota_public_key.h` | Compiled-in RSA-3072 public key (generated, committed) |
| `src/net/OtaRollback.h` / `.cpp` | `verifyRollbackLater()` override + `confirmRunningImageValid()` |
| `src/net/OtaUpdater.h` / `.cpp` | Fetch manifest → gate → download → verify signature → flash → reboot |
| `src/render/EpdDashboard.cpp` | e-paper version label (modify) |
| `src/render/Pages.cpp` | AMOLED version label (modify) |
| `src/app/SleepCycle.cpp` | e-paper: confirm-valid + opportunistic OTA (modify) |
| `src/app/App.h` / `App.cpp` | AMOLED: confirm-valid + periodic OTA (modify) |
| `platformio.ini` | `-DUPDATE_SIGN` both envs; `tdisplay_long` → `default_16MB.csv` (modify) |
| `.github/workflows/release.yml` | Build + sign + manifest + publish on tag |
| `test/test_version/`, `test/test_ota_manifest/`, `test/test_ota_policy/` | Native unit tests |

---

## Phase A — Pure logic (native TDD)

### Task 1: Version comparison

**Files:**
- Create: `src/core/Version.h`
- Create: `src/core/Version.cpp`
- Test: `test/test_version/test_version.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_version/test_version.cpp`:

```cpp
#include <unity.h>
#include "core/Version.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_newer_patch() { TEST_ASSERT_TRUE(isNewer("1.0.1", "1.0.0")); }
void test_newer_minor() { TEST_ASSERT_TRUE(isNewer("1.1.0", "1.0.9")); }
void test_newer_major() { TEST_ASSERT_TRUE(isNewer("2.0.0", "1.9.9")); }
void test_equal_not_newer() { TEST_ASSERT_FALSE(isNewer("1.2.3", "1.2.3")); }
void test_older_not_newer() { TEST_ASSERT_FALSE(isNewer("1.0.0", "1.0.1")); }
void test_leading_v_ignored() { TEST_ASSERT_TRUE(isNewer("v1.2.0", "v1.1.0")); }
void test_dev_is_dev() {
  TEST_ASSERT_TRUE(isDevVersion("0.0.0-dev"));
  TEST_ASSERT_TRUE(isDevVersion(""));
  TEST_ASSERT_TRUE(isDevVersion(nullptr));
}
void test_release_not_dev() { TEST_ASSERT_FALSE(isDevVersion("1.0.0")); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_newer_patch);
  RUN_TEST(test_newer_minor);
  RUN_TEST(test_newer_major);
  RUN_TEST(test_equal_not_newer);
  RUN_TEST(test_older_not_newer);
  RUN_TEST(test_leading_v_ignored);
  RUN_TEST(test_dev_is_dev);
  RUN_TEST(test_release_not_dev);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_version`
Expected: FAIL — compile error, `core/Version.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/core/Version.h`:

```cpp
#pragma once
#include <cstddef>

// CI writes core/FirmwareVersionGen.h (#define FIRMWARE_VERSION "x.y.z") from the
// git tag (see .github/workflows/release.yml). Absent in local/dev builds, where
// the dev sentinel below keeps self-update OFF.
#if defined(__has_include)
#  if __has_include("core/FirmwareVersionGen.h")
#    include "core/FirmwareVersionGen.h"
#  endif
#endif
#ifndef FIRMWARE_VERSION
#  define FIRMWARE_VERSION "0.0.0-dev"
#endif

// Stable per-board id used to select the right manifest/binary. Derived from the
// board define already set in platformio.ini (PSTRYK_BOARD_EPAPER for the e-paper
// env; the AMOLED env defines neither, so it falls through to "amoled").
#ifdef PSTRYK_BOARD_EPAPER
#  define PSTRYK_BOARD_ID "epaper"
#else
#  define PSTRYK_BOARD_ID "amoled"
#endif

namespace pstryk {

// True if release `candidate` is strictly newer than `current`, comparing
// MAJOR.MINOR.PATCH numerically. A leading 'v' and any trailing non-digits are
// ignored. The dev sentinel is handled by isDevVersion()/OtaPolicy, not here.
bool isNewer(const char* candidate, const char* current);

// True if `v` is the dev sentinel / empty / null. Self-update stays off for these.
bool isDevVersion(const char* v);

}  // namespace pstryk
```

- [ ] **Step 4: Write the implementation**

Create `src/core/Version.cpp`:

```cpp
#include "core/Version.h"
#include <cstring>
#include <cstdlib>

namespace pstryk {

// Parse leading "[v]A.B.C" into three ints; stops at the first non-version char.
static void parse3(const char* s, long out[3]) {
  out[0] = out[1] = out[2] = 0;
  if (!s) return;
  if (*s == 'v' || *s == 'V') ++s;
  for (int i = 0; i < 3 && *s; ++i) {
    char* end = nullptr;
    out[i] = std::strtol(s, &end, 10);
    s = end;
    if (*s == '.') ++s;
  }
}

bool isNewer(const char* candidate, const char* current) {
  if (!candidate || !current) return false;
  long c[3], r[3];
  parse3(candidate, c);
  parse3(current, r);
  for (int i = 0; i < 3; ++i) {
    if (c[i] != r[i]) return c[i] > r[i];
  }
  return false;
}

bool isDevVersion(const char* v) {
  if (!v || !*v) return true;
  return std::strstr(v, "-dev") != nullptr || std::strcmp(v, "0.0.0") == 0;
}

}  // namespace pstryk
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_version`
Expected: PASS — `8 Tests 0 Failures 0 Ignored`.

- [ ] **Step 6: Commit**

```bash
git add src/core/Version.h src/core/Version.cpp test/test_version/test_version.cpp
git commit -m "feat(ota): add firmware version compare and board id"
```

---

### Task 2: Manifest parsing

**Files:**
- Create: `src/core/OtaManifest.h`
- Create: `src/core/OtaManifest.cpp`
- Test: `test/test_ota_manifest/test_ota_manifest.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_ota_manifest/test_ota_manifest.cpp`:

```cpp
#include <unity.h>
#include "core/OtaManifest.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

static const char* kOk =
  R"JSON({"board":"epaper","version":"1.4.0",
  "url":"https://github.com/o/r/releases/download/v1.4.0/firmware-epaper.bin",
  "size":1331840,"sha256":"deadbeef"})JSON";

void test_parses_all_fields() {
  OtaManifest m;
  TEST_ASSERT_TRUE(parseManifest(kOk, m));
  TEST_ASSERT_EQUAL_STRING("epaper", m.board.c_str());
  TEST_ASSERT_EQUAL_STRING("1.4.0", m.version.c_str());
  TEST_ASSERT_EQUAL_STRING(
    "https://github.com/o/r/releases/download/v1.4.0/firmware-epaper.bin",
    m.url.c_str());
  TEST_ASSERT_EQUAL_UINT32(1331840UL, m.size);
  TEST_ASSERT_EQUAL_STRING("deadbeef", m.sha256.c_str());
}

void test_missing_url_fails() {
  OtaManifest m;
  TEST_ASSERT_FALSE(parseManifest(R"JSON({"board":"epaper","version":"1.4.0"})JSON", m));
}

void test_missing_board_fails() {
  OtaManifest m;
  TEST_ASSERT_FALSE(parseManifest(R"JSON({"version":"1.4.0","url":"x"})JSON", m));
}

void test_malformed_fails() {
  OtaManifest m;
  TEST_ASSERT_FALSE(parseManifest("not json", m));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_all_fields);
  RUN_TEST(test_missing_url_fails);
  RUN_TEST(test_missing_board_fails);
  RUN_TEST(test_malformed_fails);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_ota_manifest`
Expected: FAIL — `core/OtaManifest.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/core/OtaManifest.h`:

```cpp
#pragma once
#include <string>

namespace pstryk {

struct OtaManifest {
  std::string board;          // "epaper" | "amoled"
  std::string version;        // "1.4.0"
  std::string url;            // absolute https URL to the signed .bin
  std::string sha256;         // lowercase hex (informational; signature is the gate)
  unsigned long size = 0;     // bytes (informational)
};

// Parse a per-board OTA manifest JSON. Returns false on JSON error or if any
// required field (board, version, url) is missing/empty.
bool parseManifest(const char* json, OtaManifest& out);

}  // namespace pstryk
```

- [ ] **Step 4: Write the implementation**

Create `src/core/OtaManifest.cpp`:

```cpp
#include "core/OtaManifest.h"
#include <ArduinoJson.h>

namespace pstryk {

bool parseManifest(const char* json, OtaManifest& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  out.board   = doc["board"].as<const char*>()   ? doc["board"].as<const char*>()   : "";
  out.version = doc["version"].as<const char*>() ? doc["version"].as<const char*>() : "";
  out.url     = doc["url"].as<const char*>()     ? doc["url"].as<const char*>()     : "";
  out.sha256  = doc["sha256"].as<const char*>()  ? doc["sha256"].as<const char*>()  : "";
  out.size    = doc["size"] | 0UL;
  return !out.board.empty() && !out.version.empty() && !out.url.empty();
}

}  // namespace pstryk
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_ota_manifest`
Expected: PASS — `4 Tests 0 Failures 0 Ignored`.

- [ ] **Step 6: Commit**

```bash
git add src/core/OtaManifest.h src/core/OtaManifest.cpp test/test_ota_manifest/test_ota_manifest.cpp
git commit -m "feat(ota): parse per-board update manifest"
```

---

### Task 3: Update policy gates

**Files:**
- Create: `src/core/OtaPolicy.h`
- Create: `src/core/OtaPolicy.cpp`
- Test: `test/test_ota_policy/test_ota_policy.cpp`

- [ ] **Step 1: Write the failing test**

Create `test/test_ota_policy/test_ota_policy.cpp`:

```cpp
#include <unity.h>
#include "core/OtaPolicy.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

static OtaManifest mk(const char* board, const char* ver) {
  OtaManifest m; m.board = board; m.version = ver; m.url = "https://x/y.bin";
  return m;
}

void test_apply_when_newer_same_board() {
  TEST_ASSERT_TRUE(shouldApplyUpdate(mk("epaper", "1.4.0"), "1.3.0", "epaper"));
}
void test_skip_wrong_board() {
  TEST_ASSERT_FALSE(shouldApplyUpdate(mk("amoled", "1.4.0"), "1.3.0", "epaper"));
}
void test_skip_not_newer() {
  TEST_ASSERT_FALSE(shouldApplyUpdate(mk("epaper", "1.3.0"), "1.3.0", "epaper"));
}
void test_skip_dev_build() {
  TEST_ASSERT_FALSE(shouldApplyUpdate(mk("epaper", "9.9.9"), "0.0.0-dev", "epaper"));
}

void test_due_never_checked() { TEST_ASSERT_TRUE(dueForOtaCheck(0, 1000, 100)); }
void test_due_within_interval() { TEST_ASSERT_FALSE(dueForOtaCheck(1000, 1050, 100)); }
void test_due_past_interval() { TEST_ASSERT_TRUE(dueForOtaCheck(1000, 1200, 100)); }
void test_due_no_clock() { TEST_ASSERT_FALSE(dueForOtaCheck(1000, 0, 100)); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_when_newer_same_board);
  RUN_TEST(test_skip_wrong_board);
  RUN_TEST(test_skip_not_newer);
  RUN_TEST(test_skip_dev_build);
  RUN_TEST(test_due_never_checked);
  RUN_TEST(test_due_within_interval);
  RUN_TEST(test_due_past_interval);
  RUN_TEST(test_due_no_clock);
  return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_ota_policy`
Expected: FAIL — `core/OtaPolicy.h` does not exist.

- [ ] **Step 3: Write the header**

Create `src/core/OtaPolicy.h`:

```cpp
#pragma once
#include "core/OtaManifest.h"
#include <cstdint>

namespace pstryk {

// True only if the manifest targets this board, the running build is a real
// release (not the dev sentinel), and the manifest version is strictly newer.
bool shouldApplyUpdate(const OtaManifest& m, const char* currentVersion, const char* boardId);

// Rate-limit gate. True if >= minIntervalSec elapsed since lastCheckEpoch (or if
// never checked). nowEpoch == 0 (no trustworthy clock) returns false.
bool dueForOtaCheck(uint32_t lastCheckEpoch, uint32_t nowEpoch, uint32_t minIntervalSec);

}  // namespace pstryk
```

- [ ] **Step 4: Write the implementation**

Create `src/core/OtaPolicy.cpp`:

```cpp
#include "core/OtaPolicy.h"
#include "core/Version.h"
#include <cstring>

namespace pstryk {

bool shouldApplyUpdate(const OtaManifest& m, const char* currentVersion, const char* boardId) {
  if (isDevVersion(currentVersion)) return false;                  // dev never self-updates
  if (m.board.empty() || !boardId) return false;
  if (std::strcmp(m.board.c_str(), boardId) != 0) return false;    // wrong board
  return isNewer(m.version.c_str(), currentVersion);
}

bool dueForOtaCheck(uint32_t lastCheckEpoch, uint32_t nowEpoch, uint32_t minIntervalSec) {
  if (nowEpoch == 0) return false;              // no trustworthy clock
  if (lastCheckEpoch == 0) return true;         // never checked
  if (nowEpoch < lastCheckEpoch) return true;   // clock moved backwards -> allow
  return (nowEpoch - lastCheckEpoch) >= minIntervalSec;
}

}  // namespace pstryk
```

- [ ] **Step 5: Run test to verify it passes**

Run: `pio test -e native -f test_ota_policy`
Expected: PASS — `8 Tests 0 Failures 0 Ignored`.

- [ ] **Step 6: Run the full native suite (no regressions)**

Run: `pio test -e native`
Expected: all test folders PASS, including the pre-existing `test_parse`, `test_logic`, etc.

- [ ] **Step 7: Commit**

```bash
git add src/core/OtaPolicy.h src/core/OtaPolicy.cpp test/test_ota_policy/test_ota_policy.cpp
git commit -m "feat(ota): add update + rate-limit decision gates"
```

---

## Phase B — Build foundation

### Task 4: Enable signed-update support and dual-OTA partitions

**Files:**
- Modify: `platformio.ini`
- Modify: `.gitignore`

- [ ] **Step 1: Add `-DUPDATE_SIGN` to the e-paper env**

In `platformio.ini`, under `[env:t5_epaper_s3]` `build_flags`, add `-DUPDATE_SIGN` after the existing `-DPSTRYK_BOARD_EPAPER` line:

```ini
    -DLILYGO_T5_EPD47_S3
    -DPSTRYK_BOARD_EPAPER
    -DUPDATE_SIGN
```

- [ ] **Step 2: Add `-DUPDATE_SIGN` and switch partitions on the AMOLED env**

In `[env:tdisplay_long]`, change the partitions line and add the flag:

```ini
board_build.partitions = default_16MB.csv
```

and add to its `build_flags` (after `-DBOARD_HAS_PSRAM`):

```ini
    -DUPDATE_SIGN
```

- [ ] **Step 3: Ignore the generated version header**

In `.gitignore`, add under the PlatformIO section:

```gitignore
# CI-generated firmware version (see release.yml)
src/core/FirmwareVersionGen.h
```

- [ ] **Step 4: Build both boards to verify they still compile**

Run: `pio run -e t5_epaper_s3 && pio run -e tdisplay_long`
Expected: both `SUCCESS`. (The AMOLED app now targets a dual-app 16 MB layout; the binary still fits the 6.5 MB app slot.)

- [ ] **Step 5: Commit**

```bash
git add platformio.ini .gitignore
git commit -m "build(ota): enable signed updates + dual-OTA partitions"
```

---

## Phase C — On-screen version label

### Task 5: e-paper version label

**Files:**
- Modify: `src/render/EpdDashboard.cpp`

- [ ] **Step 1: Include the version header**

At the top of `src/render/EpdDashboard.cpp`, add after the existing includes:

```cpp
#include "core/Version.h"
```

- [ ] **Step 2: Draw the label on the dashboard**

In `drawDashboard(...)`, immediately before the final `r.present();` (or at the end of the function body before it flushes), add a discreet bottom-left label:

```cpp
  r.text(20, r.height() - 24, "v" FIRMWARE_VERSION, p.ink, 1);
```

- [ ] **Step 3: Draw the label on the message/boot/error screen**

In `drawMessage(IRenderer& r, const char* line1, const char* line2)`, before its `r.present();`, add:

```cpp
  r.text(20, r.height() - 24, "v" FIRMWARE_VERSION, r.rgb(120, 120, 120), 1);
```

- [ ] **Step 4: Build the e-paper board**

Run: `pio run -e t5_epaper_s3`
Expected: `SUCCESS`. (Local build shows `v0.0.0-dev`.)

- [ ] **Step 5: Commit**

```bash
git add src/render/EpdDashboard.cpp
git commit -m "feat(ota): show firmware version on e-paper screen"
```

---

### Task 6: AMOLED version label

**Files:**
- Modify: `src/render/Pages.cpp`

- [ ] **Step 1: Include the version header**

At the top of `src/render/Pages.cpp`, add after the existing includes:

```cpp
#include "core/Version.h"
```

- [ ] **Step 2: Draw the label on every page**

In `renderPage(...)`, just before its `r.present();`, add a discreet bottom-right label:

```cpp
  const char* ver = "v" FIRMWARE_VERSION;
  r.text(r.width() - r.textWidth(ver, 1) - 8, r.height() - 12, ver, p.muted, 1);
```

- [ ] **Step 3: Draw the label on the message/boot/config screen**

In `renderMessage(IRenderer& r, const char* line1, const char* line2)`, before its `r.present();`, add:

```cpp
  const char* ver = "v" FIRMWARE_VERSION;
  r.text(r.width() - r.textWidth(ver, 1) - 8, r.height() - 12, ver, p.muted, 1);
```

- [ ] **Step 4: Build the AMOLED board**

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS`.

- [ ] **Step 5: Commit**

```bash
git add src/render/Pages.cpp
git commit -m "feat(ota): show firmware version on AMOLED screen"
```

---

## Phase D — Signing material

### Task 7: Generate the signing keypair and compiled-in public key

> One-time procedure on a trusted machine. The **private key never enters the repo** — it goes to the `OTA_SIGNING_KEY` GitHub Actions secret and an offline backup. The **public key** is safe to commit.

**Files:**
- Create: `src/net/ota_public_key.h` (committed)

- [ ] **Step 1: Install the signing dependency and fetch the tool**

```bash
python3 -m pip install --user cryptography
curl -fsSL https://raw.githubusercontent.com/espressif/arduino-esp32/master/tools/bin_signing.py -o /tmp/bin_signing.py
```

- [ ] **Step 2: Generate the RSA-3072 keypair**

```bash
python3 /tmp/bin_signing.py --generate-key rsa-3072 --out ota_private_key.pem
python3 /tmp/bin_signing.py --extract-pubkey ota_private_key.pem --out ota_public_key.pem
```

- [ ] **Step 3: Convert the public key PEM into a committable C header**

```bash
{
  printf '#pragma once\n#include <cstddef>\n#include <cstdint>\n'
  printf '// RSA-3072 OTA public key (PEM bytes). Generated once; safe to commit.\n'
  printf '// Private key lives ONLY in the OTA_SIGNING_KEY GitHub secret + offline backup.\n'
  xxd -i -n PUBLIC_KEY ota_public_key.pem | sed 's/^unsigned char/const uint8_t/; /unsigned int PUBLIC_KEY_len/d'
  printf 'const size_t PUBLIC_KEY_LEN = sizeof(PUBLIC_KEY);\n'
} > src/net/ota_public_key.h
```

Verify it defines `const uint8_t PUBLIC_KEY[]` and `const size_t PUBLIC_KEY_LEN`:

Run: `grep -E 'PUBLIC_KEY\[\]|PUBLIC_KEY_LEN' src/net/ota_public_key.h`
Expected: both symbols present.

- [ ] **Step 4: Store the private key as a GitHub secret and back it up**

```bash
gh secret set OTA_SIGNING_KEY < ota_private_key.pem
# Back up ota_private_key.pem to an offline/secure location, then remove the local copy:
mv ota_private_key.pem ~/secure-backups/pstryk_ota_private_key.pem
rm -f ota_public_key.pem
```

> If this key is ever lost, no future signed updates can be produced; if leaked, an attacker can sign images your devices will accept. Treat it as a long-lived, high-value secret.

- [ ] **Step 5: Commit the public key only**

```bash
git add src/net/ota_public_key.h
git status   # confirm NO *_private_key.pem is staged
git commit -m "feat(ota): add compiled-in signature public key"
```

---

## Phase E — Device OTA modules

### Task 8: Rollback confirmation

**Files:**
- Create: `src/net/OtaRollback.h`
- Create: `src/net/OtaRollback.cpp`

- [ ] **Step 1: Write the header**

Create `src/net/OtaRollback.h`:

```cpp
#pragma once

namespace pstryk {

// If the running image was just OTA-flashed and is awaiting verification, mark it
// valid so the bootloader keeps it. Safe no-op otherwise. Call once per boot AFTER
// core subsystems (display/PSRAM) have initialized, so a build that crashes during
// boot is never confirmed and the bootloader rolls back to the previous image.
void confirmRunningImageValid();

}  // namespace pstryk
```

- [ ] **Step 2: Write the implementation**

Create `src/net/OtaRollback.cpp`:

```cpp
#include "net/OtaRollback.h"
#include <esp_ota_ops.h>

// Override arduino-esp32's weak hook so the core does NOT auto-confirm a pending
// OTA image during init(). We confirm explicitly from the app after a health
// check (see confirmRunningImageValid). Requires CONFIG_APP_ROLLBACK_ENABLE, which
// the stock arduino-esp32 ESP32-S3 build enables.
extern "C" bool verifyRollbackLater() {
  return true;
}

namespace pstryk {

void confirmRunningImageValid() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

}  // namespace pstryk
```

- [ ] **Step 3: Build both boards**

Run: `pio run -e t5_epaper_s3 && pio run -e tdisplay_long`
Expected: both `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/net/OtaRollback.h src/net/OtaRollback.cpp
git commit -m "feat(ota): self-test gated rollback confirmation"
```

---

### Task 9: OTA updater (download → verify → flash)

**Files:**
- Create: `src/net/OtaUpdater.h`
- Create: `src/net/OtaUpdater.cpp`

- [ ] **Step 1: Write the header**

Create `src/net/OtaUpdater.h`:

```cpp
#pragma once

namespace pstryk {

enum class OtaResult { NoUpdate, FetchError, ParseError, VerifyError, FlashError };

class OtaUpdater {
 public:
  // Checks this board's manifest and, if a newer correctly-signed build exists,
  // downloads + verifies + flashes it and REBOOTS (does not return on success).
  // Returns a non-fatal result if nothing was applied. Caller must have Wi-Fi up.
  OtaResult runOnce();
};

}  // namespace pstryk
```

- [ ] **Step 2: Write the implementation**

Create `src/net/OtaUpdater.cpp`:

```cpp
#include "net/OtaUpdater.h"
#include "net/ota_public_key.h"
#include "core/Version.h"
#include "core/OtaManifest.h"
#include "core/OtaPolicy.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <SHA2Builder.h>

namespace pstryk {

// Stable "latest release" manifest URL; GitHub 302-redirects to the asset CDN.
static const char* kManifestBase =
    "https://github.com/hoffmannkrzysztof/pstryk_esp32/releases/latest/download/manifest-";

OtaResult OtaUpdater::runOnce() {
  // 1) Fetch this board's manifest.
  String manifestUrl = String(kManifestBase) + PSTRYK_BOARD_ID + ".json";
  WiFiClientSecure mClient;
  mClient.setInsecure();                                  // signature is the trust anchor (v1)
  HTTPClient mHttp;
  mHttp.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // GitHub -> CDN redirect
  mHttp.setConnectTimeout(8000);
  mHttp.setTimeout(12000);
  if (!mHttp.begin(mClient, manifestUrl)) return OtaResult::FetchError;
  if (mHttp.GET() != HTTP_CODE_OK) { mHttp.end(); return OtaResult::FetchError; }
  String body = mHttp.getString();
  mHttp.end();
  if (body.isEmpty()) return OtaResult::FetchError;

  OtaManifest m;
  if (!parseManifest(body.c_str(), m)) return OtaResult::ParseError;

  // 2) Decision gate (pure, unit-tested).
  if (!shouldApplyUpdate(m, FIRMWARE_VERSION, PSTRYK_BOARD_ID)) return OtaResult::NoUpdate;
  log_i("OTA: updating %s -> %s", FIRMWARE_VERSION, m.version.c_str());

  // 3) Download the signed image.
  WiFiClientSecure fClient;
  fClient.setInsecure();
  HTTPClient fHttp;
  fHttp.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  fHttp.setConnectTimeout(8000);
  fHttp.setTimeout(20000);
  if (!fHttp.begin(fClient, String(m.url.c_str()))) return OtaResult::FetchError;
  if (fHttp.GET() != HTTP_CODE_OK) { fHttp.end(); return OtaResult::FetchError; }
  int total = fHttp.getSize();
  if (total <= 0) { fHttp.end(); return OtaResult::FetchError; }

  // 4) Install signature verification BEFORE begin(); begin() takes the TOTAL size
  //    (firmware + appended signature). end() performs the verification.
  UpdaterRSAVerifier verifier(PUBLIC_KEY, PUBLIC_KEY_LEN, HASH_SHA256);
  if (!Update.installSignature(&verifier)) { fHttp.end(); return OtaResult::VerifyError; }
  if (!Update.begin((size_t)total)) { fHttp.end(); return OtaResult::FlashError; }

  size_t written = Update.writeStream(*fHttp.getStreamPtr());
  fHttp.end();
  if (written != (size_t)total) { Update.abort(); return OtaResult::FlashError; }

  if (!Update.end()) {
    OtaResult r = (Update.getError() == UPDATE_ERROR_SIGN)
                      ? OtaResult::VerifyError
                      : OtaResult::FlashError;
    log_e("OTA failed: %s", Update.errorString());
    return r;
  }
  if (!Update.isFinished()) return OtaResult::FlashError;

  log_i("OTA flashed; rebooting into new image");
  delay(200);
  ESP.restart();                                          // boots new image as PENDING_VERIFY
  return OtaResult::NoUpdate;                             // unreachable
}

}  // namespace pstryk
```

> **Before building:** open `src/net/OtaUpdater.cpp` and confirm the `kManifestBase` URL host (`hoffmannkrzysztof/pstryk_esp32`) matches this repo's GitHub owner/name. Adjust if the repo was forked/renamed.

- [ ] **Step 3: Build both boards**

Run: `pio run -e t5_epaper_s3 && pio run -e tdisplay_long`
Expected: both `SUCCESS`. (Confirms `UPDATE_SIGN`, `UpdaterRSAVerifier`, and the public-key header all link.)

- [ ] **Step 4: Commit**

```bash
git add src/net/OtaUpdater.h src/net/OtaUpdater.cpp
git commit -m "feat(ota): download, verify signature, and flash update"
```

---

## Phase F — Board integration

### Task 10: Integrate OTA into the e-paper deep-sleep cycle

**Files:**
- Modify: `src/app/SleepCycle.cpp`

- [ ] **Step 1: Add includes**

At the top of `src/app/SleepCycle.cpp`, after the existing `#include "net/WiFiProvisioner.h"`, add:

```cpp
#include "net/OtaUpdater.h"
#include "net/OtaRollback.h"
#include "core/OtaPolicy.h"
#include "core/Version.h"
```

- [ ] **Step 2: Add an RTC-retained last-check timestamp**

Next to the existing `RTC_DATA_ATTR static uint32_t g_consecFail = 0;` (around line 42), add:

```cpp
// Last OTA-manifest check (epoch s), retained across deep sleep so we poll GitHub
// at most once/day per device rather than every wake.
RTC_DATA_ATTR static uint32_t g_lastOtaCheck = 0;
```

- [ ] **Step 3: Confirm the running image once the display is proven healthy**

In `SleepCycle::setup()`, immediately after the successful display init line `if (!gfx_.begin()) { sleepFor(3600); return; }`, add:

```cpp
  // Reaching here means PSRAM + display init succeeded -> a just-OTA'd image is
  // healthy enough to keep. Confirm now (before Wi-Fi) so a transient network blip
  // on a later wake can't trigger a false rollback on this sleeping board.
  confirmRunningImageValid();
```

- [ ] **Step 4: Run an opportunistic OTA check after a successful fetch**

In `SleepCycle::setup()`, immediately before the final `WiFi.disconnect(true, false);` line (around line 221), add:

```cpp
  // Opportunistic OTA: only on a healthy connected cycle, rate-limited to once/day.
  // runOnce() reboots into the new image on success and never returns.
  if (res.status == FetchStatus::Ok &&
      dueForOtaCheck(g_lastOtaCheck, (uint32_t)now, 24u * 3600u)) {
    g_lastOtaCheck = (uint32_t)now;
    OtaUpdater().runOnce();
  }
```

- [ ] **Step 5: Build the e-paper board**

Run: `pio run -e t5_epaper_s3`
Expected: `SUCCESS`.

- [ ] **Step 6: Commit**

```bash
git add src/app/SleepCycle.cpp
git commit -m "feat(ota): opportunistic update + rollback confirm on e-paper"
```

---

### Task 11: Integrate OTA into the AMOLED continuous loop

**Files:**
- Modify: `src/app/App.h`
- Modify: `src/app/App.cpp`

- [ ] **Step 1: Add a next-check member**

In `src/app/App.h`, after `uint32_t lastRedrawMs_ = 0;` (line 33), add:

```cpp
  uint32_t nextOtaCheckAtMs_ = 0;
```

- [ ] **Step 2: Add includes**

At the top of `src/app/App.cpp`, after `#include <WiFi.h>`, add:

```cpp
#include "net/OtaUpdater.h"
#include "net/OtaRollback.h"
```

- [ ] **Step 3: Confirm the running image after the first successful Wi-Fi connect**

In `App::setup()`, immediately after the `if (!provisioner_.ensureConnected(...)) { ... ESP.restart(); }` block succeeds (i.e., right after that `if` statement closes, before the `renderMessage(gfx_, "Czas", ...)` line), add:

```cpp
  // Display is up and Wi-Fi connected -> a just-OTA'd image is healthy; keep it.
  confirmRunningImageValid();
```

- [ ] **Step 4: Schedule the first OTA check**

At the end of `App::setup()`, after `lastRedrawMs_ = 0;` (line 43), add:

```cpp
  nextOtaCheckAtMs_ = now + 6u * 60u * 60u * 1000u;   // first OTA check ~6 h after boot
```

- [ ] **Step 5: Run the periodic OTA check in the loop**

In `App::loop()`, immediately after the `if ((int32_t)(now - nextFetchAtMs_) >= 0) doFetch();` line (line 115), add:

```cpp
  if ((int32_t)(now - nextOtaCheckAtMs_) >= 0) {
    nextOtaCheckAtMs_ = now + 6u * 60u * 60u * 1000u;   // re-check every ~6 h
    if (WiFi.status() == WL_CONNECTED) OtaUpdater().runOnce();  // reboots on update
  }
```

- [ ] **Step 6: Build the AMOLED board**

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS`.

- [ ] **Step 7: Commit**

```bash
git add src/app/App.h src/app/App.cpp
git commit -m "feat(ota): periodic update + rollback confirm on AMOLED"
```

---

## Phase G — Release pipeline & docs

### Task 12: CI release workflow

**Files:**
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Write the workflow**

Create `.github/workflows/release.yml`:

```yaml
name: Release

on:
  push:
    tags: ['v*']

permissions:
  contents: write

jobs:
  build-sign-release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - uses: actions/setup-python@v5
        with:
          python-version: "3.13"

      - name: Install PlatformIO and signing deps
        run: pip install --upgrade platformio cryptography

      - name: Derive version
        id: ver
        run: |
          TAG="${GITHUB_REF_NAME}"
          echo "tag=$TAG" >> "$GITHUB_OUTPUT"
          echo "ver=${TAG#v}" >> "$GITHUB_OUTPUT"

      - name: Inject firmware version
        run: echo '#define FIRMWARE_VERSION "${{ steps.ver.outputs.ver }}"' > src/core/FirmwareVersionGen.h

      - name: Build both boards
        run: |
          pio run -e t5_epaper_s3
          pio run -e tdisplay_long

      - name: Fetch signing tool
        run: curl -fsSL https://raw.githubusercontent.com/espressif/arduino-esp32/master/tools/bin_signing.py -o bin_signing.py

      - name: Restore signing key
        run: printf '%s' "${{ secrets.OTA_SIGNING_KEY }}" > private_key.pem

      - name: Extract public key (for self-verify)
        run: python bin_signing.py --extract-pubkey private_key.pem --out public_key.pem

      - name: Sign, verify, and write manifests
        env:
          REPO: ${{ github.repository }}
          TAG: ${{ steps.ver.outputs.tag }}
          VER: ${{ steps.ver.outputs.ver }}
        run: |
          set -euo pipefail
          sign_board () {
            env="$1"; board="$2"
            python bin_signing.py --bin ".pio/build/$env/firmware.bin" --key private_key.pem --out "firmware-$board.bin"
            python bin_signing.py --verify "firmware-$board.bin" --pubkey public_key.pem
            size=$(stat -c%s "firmware-$board.bin")
            sha=$(sha256sum "firmware-$board.bin" | cut -d' ' -f1)
            printf '{"board":"%s","version":"%s","url":"https://github.com/%s/releases/download/%s/firmware-%s.bin","size":%s,"sha256":"%s"}\n' \
              "$board" "$VER" "$REPO" "$TAG" "$board" "$size" "$sha" > "manifest-$board.json"
          }
          sign_board t5_epaper_s3 epaper
          sign_board tdisplay_long amoled

      - name: Publish release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            firmware-epaper.bin
            firmware-amoled.bin
            manifest-epaper.json
            manifest-amoled.json

      - name: Scrub key material
        if: always()
        run: rm -f private_key.pem public_key.pem
```

- [ ] **Step 2: Validate the YAML locally**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml')); print('yaml ok')"`
Expected: `yaml ok`.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/release.yml
git commit -m "ci(ota): build, sign, and publish per-board firmware on tag"
```

> First real run happens when the first `v*` tag is pushed (Task 15). Requires the `OTA_SIGNING_KEY` secret from Task 7.

---

### Task 13: Documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add an OTA updates section**

Add a new section to `README.md` (place it after the build/flash instructions). Use this content:

```markdown
## Aktualizacje OTA (over-the-air)

Po wgraniu pierwszej wersji przez USB urządzenia same pobierają kolejne
aktualizacje z GitHub Releases — cicho, w tle, z weryfikacją podpisu i
automatycznym wycofaniem (rollback) wadliwego obrazu.

- **Płyta e-paper (T5):** sprawdza aktualizacje maksymalnie raz na dobę, przy
  okazji udanego cyklu pobierania cen.
- **Płyta AMOLED (T-Display-Long):** sprawdza co ok. 6 godzin.
- Wersja firmware jest widoczna w rogu ekranu (`vX.Y.Z`); `v0.0.0-dev` oznacza
  lokalny build, który **nie** aktualizuje się sam.

### Jednorazowa migracja płyty AMOLED

Starsze buildy AMOLED używały układu partycji `huge_app.csv` z **jedną** partycją
aplikacji — OTA jest tam niemożliwe. Aby włączyć OTA, wgraj **raz, przez USB**,
nową wersję (która używa `default_16MB.csv`). Od tego momentu kolejne
aktualizacje pójdą już przez OTA. Płyta e-paper nie wymaga tego kroku.

### Wydawanie nowej wersji (dla maintainera)

1. Otaguj commit sem-ver tagiem, np. `git tag v1.4.0 && git push origin v1.4.0`.
2. CI (`.github/workflows/release.yml`) zbuduje obie płyty, podpisze binaria
   kluczem prywatnym z sekretu `OTA_SIGNING_KEY` i opublikuje release z plikami
   `firmware-<board>.bin` oraz `manifest-<board>.json`.
3. Urządzenia pobiorą `…/releases/latest/download/manifest-<board>.json` i
   zaktualizują się, jeśli wersja w manifeście jest nowsza.

### Klucz podpisu

Obrazy są podpisywane RSA-3072/SHA-256. Klucz **publiczny** jest wkompilowany w
firmware (`src/net/ota_public_key.h`). Klucz **prywatny** istnieje wyłącznie w
sekrecie `OTA_SIGNING_KEY` i offline. Utrata klucza = brak możliwości wydawania
aktualizacji; wyciek = ktoś może podpisać firmware, które urządzenia zaakceptują.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs(ota): document OTA, AMOLED migration, and release process"
```

---

### Task 14: Reconcile the spec with implemented decisions

**Files:**
- Modify: `docs/superpowers/specs/2026-06-24-ota-updates-design.md`

- [ ] **Step 1: Note the realized component locations**

In §3 of the spec, update the bullets that place `OtaManifest`/policy in `src/net/` to `src/core/` (so they are native-testable), and note that `Version`, `OtaManifest`, and `OtaPolicy` live in `core/`.

- [ ] **Step 2: Correct the dev-version rationale**

In §5, replace the "0.0.0-dev is never newer" sentence with: "Builds whose running version is the dev sentinel (`0.0.0-dev`, or any `-dev`) are excluded from self-update by `shouldApplyUpdate()`, so dev boards never auto-flash a release."

- [ ] **Step 3: Record the confirmed rollback mechanism**

In §3/§7, add a note: rollback uses arduino-esp32's weak `verifyRollbackLater()` hook (overridden to defer confirmation) plus `esp_ota_mark_app_valid_cancel_rollback()` after a health check; confirmed enabled via `CONFIG_APP_ROLLBACK_ENABLE=y` on the stock S3 build, with `ANTI_ROLLBACK` off (no eFuse burning).

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-06-24-ota-updates-design.md
git commit -m "docs(ota): reconcile spec with implemented design"
```

---

## Phase H — On-device verification

### Task 15: End-to-end on-device verification matrix

> Hardware-only paths (signature verify, flash, rollback) that can't run in `native`. Do this on at least one physical board of each type. Requires the `OTA_SIGNING_KEY` secret (Task 7) and the CI workflow (Task 12).

- [ ] **Step 1: Flash the OTA-capable baseline over USB**

Build and flash the current branch to each board over USB:

Run (e-paper): `pio run -e t5_epaper_s3 -t upload`
Run (AMOLED): `pio run -e tdisplay_long -t upload`
Expected: boots, normal price screen, version label shows `v0.0.0-dev`.

- [ ] **Step 2: Cut a baseline release `v1.0.0`**

```bash
git tag v1.0.0 && git push origin v1.0.0
```
Expected: the `Release` workflow succeeds and the GitHub release has `firmware-epaper.bin`, `firmware-amoled.bin`, `manifest-epaper.json`, `manifest-amoled.json`.

- [ ] **Step 3: Flash `v1.0.0` over USB as the new baseline**

Re-build with the released version and flash both boards (so they run a real release, not `-dev`):

```bash
echo '#define FIRMWARE_VERSION "1.0.0"' > src/core/FirmwareVersionGen.h
pio run -e t5_epaper_s3 -t upload
pio run -e tdisplay_long -t upload
rm -f src/core/FirmwareVersionGen.h
```
Expected: each screen shows `v1.0.0`.

- [ ] **Step 4: Happy path — publish `v1.1.0` and confirm silent update**

```bash
git tag v1.1.0 && git push origin v1.1.0
```
Then wait for each board's check window (AMOLED ~6 h; to test the e-paper sooner, short-press to force a wake/cycle on consecutive days, or temporarily lower the `24u * 3600u` interval in a local build).
Expected: board downloads silently, reboots, screen now shows `v1.1.0`, and it **stays** on `v1.1.0` across the next reboot (proves `confirmRunningImageValid()` marked it valid).

- [ ] **Step 5: Signature rejection — confirm a bad signature is refused**

Build an image and sign it with a throwaway key, host it, and point a manifest at it (or temporarily corrupt a byte in a signed `.bin`). Trigger a check.
Expected: serial log shows `OTA failed` with `UPDATE_ERROR_SIGN`; the device keeps running the current image (no reboot loop).

- [ ] **Step 6: Rollback — confirm a bad image reverts**

Build a deliberately broken release (e.g., add an early `abort();` in `setup()` after display init but before `confirmRunningImageValid()`), sign it, publish it as `v1.2.0`, and let a board update to it.
Expected: the broken image boots once, fails to reach the confirm call, and on the next reset the bootloader rolls back; the board returns to `v1.1.0` and keeps running. Remove the broken build / publish a fixed `v1.2.1` afterward.

- [ ] **Step 7: Record results**

Note pass/fail for each step in the PR description. All must pass before relying on OTA for distribution.

---

## Self-review

- **Spec coverage:** Goals → version compare/board id (T1), pull manifest (T2), gate + rate-limit (T3), signed verify (T7, T9), dual-bank + rollback (T4, T8, T10, T11), version label (T5, T6), CI/release (T12), docs incl. AMOLED migration (T13), testing (T1–T3 native, T15 on-device). All spec sections map to a task.
- **Placeholders:** none — every code/command step is concrete. `<board>` in URLs is a runtime token, not a plan gap.
- **Type consistency:** `isNewer`/`isDevVersion` (T1) used by `shouldApplyUpdate` (T3); `OtaManifest` fields (T2) consumed by `OtaPolicy` (T3) and `OtaUpdater` (T9); `PUBLIC_KEY`/`PUBLIC_KEY_LEN` (T7) consumed by `OtaUpdater` (T9); `confirmRunningImageValid()` (T8) called in T10/T11; `nextOtaCheckAtMs_` declared (T11 Step 1) before use (T11 Step 5). Consistent throughout.
