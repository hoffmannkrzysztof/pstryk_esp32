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
