#pragma once
#include <Arduino.h>
#include "storage/Settings.h"

namespace pstryk {

class WiFiProvisioner {
 public:
  // Connects using saved settings. If incomplete or `forcePortal`, opens the
  // captive portal "Pstryk-Setup" (blocking, bounded to 10 min) to capture
  // WiFi + API key, then writes them into `s` and persists. With complete
  // settings and no `forcePortal` it is strictly non-interactive: a fast
  // directed join (RTC-cached BSSID/channel/lease) falling back to a plain
  // 15 s STA join, never a portal -- callers handle false with their own
  // retry/backoff.
  bool ensureConnected(Settings& s, bool forcePortal);

  // Drop the RTC fast-connect cache (AP association + DHCP lease). Called when
  // fetches keep failing right after "successful" joins -- the classic symptom
  // of a stale static lease -- so the next wake does a full DHCP join.
  static void forgetAp();

  // WPA2 password of the "Pstryk-Setup" portal: 8 random digits, generated once
  // per boot. Callers show it on the device screen right before opening the
  // portal -- an OPEN portal would hand the Wi-Fi credential form (and any
  // typed API key) to anyone in radio range.
  static const char* portalPassword();
};

}  // namespace pstryk
