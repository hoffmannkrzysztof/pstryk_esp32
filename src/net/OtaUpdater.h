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
