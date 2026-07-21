#include "net/WiFiProvisioner.h"
#include <WiFi.h>
#include <WiFiManager.h>

namespace pstryk {

bool WiFiProvisioner::ensureConnected(Settings& s, bool forcePortal) {
  if (!forcePortal && s.isComplete()) {
    // Unattended path (hourly wake / boot reconnect): plain STA join, never a
    // portal. WiFiManager's autoConnect falls back to a BLOCKING captive portal
    // when the saved credentials fail, and with no timeout that parks the battery
    // board in SoftAP (~100 mA) until the cell is flat the first time a wake
    // overlaps a router outage. Fail fast instead -- every caller has a working
    // retry/backoff branch for a false return.
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.ssid.c_str(), s.pass.c_str());
    return WiFi.waitForConnectResult(15000) == WL_CONNECTED;
  }

  // Attended path: first-boot provisioning or an explicit button-held
  // reconfigure. A human is present, so a blocking portal is right -- but bound
  // it so an abandoned portal cannot burn the battery for days. On timeout the
  // callers retry (e-paper: next wake; Long: restart).
  WiFiManager wm;
  wm.setConfigPortalTimeout(600);
  wm.setConnectTimeout(20);      // bound the post-submit join attempt too

  WiFiManagerParameter apiKeyParam("apikey", "Pstryk API key (sk-...)",
                                   s.apiKey.c_str(), 80);
  wm.addParameter(&apiKeyParam);

  bool ok = wm.startConfigPortal("Pstryk-Setup");
  if (ok) {
    // Capture whatever WiFiManager negotiated/entered.
    s.ssid = WiFi.SSID();
    s.pass = WiFi.psk();
    String entered = apiKeyParam.getValue();
    if (entered.length() > 0) s.apiKey = entered;
    s.save();
  }
  return ok && WiFi.status() == WL_CONNECTED;
}

}  // namespace pstryk
