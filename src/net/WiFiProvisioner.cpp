#include "net/WiFiProvisioner.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_attr.h>
#include <esp_random.h>
#include <cstring>

namespace pstryk {

// Last successful association, retained across deep sleep (zeroed on power-on
// reset, RTC RAM semantics). Lets the battery board's wakes skip the AP scan
// (directed channel+BSSID join) and the DHCP handshake (cached lease): about
// 1.5-3 s less radio time per connected wake. Inert on the always-on board.
RTC_DATA_ATTR static uint8_t  g_apBssid[6];
RTC_DATA_ATTR static int32_t  g_apChannel = 0;
RTC_DATA_ATTR static uint32_t g_apIp = 0, g_apGw = 0, g_apMask = 0, g_apDns = 0;

static void rememberAp() {
  const uint8_t* b = WiFi.BSSID();
  if (!b || WiFi.channel() <= 0) return;
  std::memcpy(g_apBssid, b, 6);
  g_apChannel = WiFi.channel();
  g_apIp   = (uint32_t)WiFi.localIP();
  g_apGw   = (uint32_t)WiFi.gatewayIP();
  g_apMask = (uint32_t)WiFi.subnetMask();
  g_apDns  = (uint32_t)WiFi.dnsIP();
}

void WiFiProvisioner::forgetAp() { g_apChannel = 0; }

const char* WiFiProvisioner::portalPassword() {
  // Random per boot (NOT MAC-derived: the SoftAP BSSID visible in beacons is
  // built from the same base MAC, so a MAC-derived PSK would be computable by
  // anyone sniffing the air). Digits only -- easy to retype from the screen.
  static char psk[9] = "";
  if (!psk[0]) {
    for (int i = 0; i < 8; ++i) psk[i] = (char)('0' + (esp_random() % 10));
    psk[8] = '\0';
  }
  return psk;
}

bool WiFiProvisioner::ensureConnected(Settings& s, bool forcePortal) {
  if (!forcePortal && s.isComplete()) {
    // Unattended path (hourly wake / boot reconnect): plain STA join, never a
    // portal. WiFiManager's autoConnect falls back to a BLOCKING captive portal
    // when the saved credentials fail, and with no timeout that parks the battery
    // board in SoftAP (~100 mA) until the cell is flat the first time a wake
    // overlaps a router outage. Fail fast instead -- every caller has a working
    // retry/backoff branch for a false return.
    WiFi.mode(WIFI_STA);
    if (g_apChannel > 0) {
      // Fast path: static lease + directed join skips the channel scan and the
      // DHCP DISCOVER/OFFER/REQUEST/ACK round-trips.
      WiFi.config(IPAddress(g_apIp), IPAddress(g_apGw), IPAddress(g_apMask),
                  IPAddress(g_apDns ? g_apDns : g_apGw));
      WiFi.begin(s.ssid.c_str(), s.pass.c_str(), g_apChannel, g_apBssid);
      if (WiFi.waitForConnectResult(5000) == WL_CONNECTED) return true;
      // AP moved channel / lease is stale: forget the cache, restore DHCP
      // (0.0.0.0 re-enables the DHCP client) and re-learn via the full join.
      forgetAp();
      WiFi.disconnect(true, false);
      WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0),
                  IPAddress((uint32_t)0));
      WiFi.mode(WIFI_STA);
    }
    WiFi.begin(s.ssid.c_str(), s.pass.c_str());
    if (WiFi.waitForConnectResult(15000) != WL_CONNECTED) return false;
    rememberAp();
    return true;
  }

  // Attended path: first-boot provisioning or an explicit button-held
  // reconfigure. A human is present, so a blocking portal is right -- but bound
  // it so an abandoned portal cannot burn the battery for days. On timeout the
  // callers retry (e-paper: next wake; Long: restart).
  WiFiManager wm;
  wm.setConfigPortalTimeout(600);
  wm.setConnectTimeout(20);      // bound the post-submit join attempt too

  // The stored key is deliberately NOT prefilled: the form would serve it to
  // anyone who joins the portal. A blank submit keeps the current key (the
  // length check below), so reconfiguring Wi-Fi never forces retyping it.
  WiFiManagerParameter apiKeyParam("apikey", "Pstryk API key (puste = bez zmian)",
                                   "", 80);
  wm.addParameter(&apiKeyParam);

  bool ok = wm.startConfigPortal("Pstryk-Setup", portalPassword());
  if (ok) {
    // Capture whatever WiFiManager negotiated/entered.
    s.ssid = WiFi.SSID();
    s.pass = WiFi.psk();
    String entered = apiKeyParam.getValue();
    if (entered.length() > 0) s.apiKey = entered;
    s.save();
    rememberAp();                // the portal may have joined a different AP
  }
  return ok && WiFi.status() == WL_CONNECTED;
}

}  // namespace pstryk
