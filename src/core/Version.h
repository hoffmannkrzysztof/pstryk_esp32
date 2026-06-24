#pragma once

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
