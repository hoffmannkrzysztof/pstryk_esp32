#pragma once

namespace pstryk {

enum class OtaResult { NoUpdate, FetchError, ParseError, VerifyError, FlashError };

class OtaUpdater {
 public:
  // Checks this board's manifest and, if a newer correctly-signed build exists,
  // downloads + verifies + flashes it and REBOOTS (does not return on success).
  // Returns a non-fatal result if nothing was applied. Caller must have Wi-Fi up.
  //
  // force=true (bootstrap/installer): install the latest release regardless of the
  // version/dev gate. The board-match check and the signature verification still
  // apply, so it can never flash an unsigned image or another board's binary.
  OtaResult runOnce(bool force = false);
};

}  // namespace pstryk
