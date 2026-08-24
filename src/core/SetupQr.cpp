#include "core/SetupQr.h"
#include <cstdio>

namespace pstryk {

bool buildWifiQrPayload(const char* ssid, const char* psk, char* out, size_t n) {
  int need = std::snprintf(out, n, "WIFI:T:WPA;S:%s;P:%s;;", ssid, psk);
  if (need < 0 || (size_t)need >= n) {
    if (n) out[0] = '\0';   // nigdy nie zostawiaj ucietego payloadu
    return false;
  }
  return true;
}

}  // namespace pstryk
