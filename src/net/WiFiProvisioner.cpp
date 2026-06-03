#include "net/WiFiProvisioner.h"
#include <WiFi.h>
#include <WiFiManager.h>

namespace pstryk {

bool WiFiProvisioner::ensureConnected(Settings& s, bool forcePortal) {
  WiFiManager wm;
  wm.setConfigPortalTimeout(0);  // stay in portal until configured

  WiFiManagerParameter apiKeyParam("apikey", "Pstryk API key (sk-...)",
                                   s.apiKey.c_str(), 80);
  wm.addParameter(&apiKeyParam);

  bool ok;
  if (forcePortal || !s.isComplete()) {
    ok = wm.startConfigPortal("Pstryk-Setup");
  } else {
    WiFi.begin(s.ssid.c_str(), s.pass.c_str());
    // autoConnect falls back to the portal if saved creds fail.
    ok = wm.autoConnect("Pstryk-Setup");
  }

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
