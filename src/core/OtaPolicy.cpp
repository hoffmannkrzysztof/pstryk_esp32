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
