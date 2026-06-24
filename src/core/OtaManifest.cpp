#include "core/OtaManifest.h"
#include <ArduinoJson.h>

namespace pstryk {

bool parseManifest(const char* json, OtaManifest& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  out.board   = doc["board"]   | "";
  out.version = doc["version"] | "";
  out.url     = doc["url"]     | "";
  out.sha256  = doc["sha256"]  | "";
  out.size    = doc["size"]    | 0UL;
  return !out.board.empty() && !out.version.empty() && !out.url.empty();
}

}  // namespace pstryk
