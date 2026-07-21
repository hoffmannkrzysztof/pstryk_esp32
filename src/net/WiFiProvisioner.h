#pragma once
#include <Arduino.h>
#include "storage/Settings.h"

namespace pstryk {

class WiFiProvisioner {
 public:
  // Connects using saved settings. If incomplete or `forcePortal`, opens the
  // captive portal "Pstryk-Setup" (blocking, bounded to 10 min) to capture
  // WiFi + API key, then writes them into `s` and persists. With complete
  // settings and no `forcePortal` it is strictly non-interactive: a plain STA
  // join with a 15 s deadline, never a portal -- callers handle false with
  // their own retry/backoff.
  bool ensureConnected(Settings& s, bool forcePortal);
};

}  // namespace pstryk
