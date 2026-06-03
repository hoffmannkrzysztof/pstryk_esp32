#pragma once
#include <Arduino.h>
#include "storage/Settings.h"

namespace pstryk {

class WiFiProvisioner {
 public:
  // Connects using saved settings. If incomplete or `forcePortal`, opens the
  // captive portal "Pstryk-Setup" (blocking) to capture WiFi + API key, then
  // writes them into `s` and persists. Returns true once connected.
  bool ensureConnected(Settings& s, bool forcePortal);
};

}  // namespace pstryk
