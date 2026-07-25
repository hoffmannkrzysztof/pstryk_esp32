#pragma once
#include <string>

namespace pstryk {

struct OtaManifest {
  std::string board;          // "epaper" | "amoled"
  std::string version;        // "1.4.0"
  std::string url;            // absolute https URL to the signed .bin
  std::string sha256;         // lowercase hex (not verified yet; signature is the gate)
  unsigned long size = 0;     // bytes; enforced against Content-Length by OtaUpdater
};

// Parse a per-board OTA manifest JSON. Returns false on JSON error or if any
// required field (board, version, url) is missing/empty.
bool parseManifest(const char* json, OtaManifest& out);

}  // namespace pstryk
