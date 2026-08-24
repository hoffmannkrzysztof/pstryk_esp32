#pragma once
#include "render/IRenderer.h"
#include "view/PriceView.h"

namespace pstryk {

struct EpdStatus {
  const char* clockHHMM = "";   // last-update time
  int  batteryPct = -1;         // -1 = unknown
  bool batteryLow = false;
  bool wifiOk = true;
  bool stale = false;           // last fetch failed / data old
};

// Paint the whole 960x540 e-paper dashboard and flush. If !view.hasData, paints
// a "Brak danych" message instead.
void drawDashboard(IRenderer& r, const PriceView& view, const EpdStatus& st);

struct SetupInfo {
  const char* ssid;        // rozglaszany SSID portalu (WiFiProvisioner::portalSsid)
  const char* password;    // 8 cyfr z WiFiProvisioner::portalPassword
  const char* portalUrl;   // "192.168.4.1"
  int  batteryPct;         // -1 = nieznany (jak EpdStatus::batteryPct)
};

// Pelnoekranowy ekran konfiguracji portalu: SSID, duze 7-seg haslo, kod QR
// do sieci, kroki 1-2-3 i prosba o podlaczenie ladowarki. Flushuje panel.
void drawSetupScreen(IRenderer& r, const SetupInfo& info);

// White-background status/error message for the e-paper board (boot, errors).
void drawMessage(IRenderer& r, const char* line1, const char* line2);

}  // namespace pstryk
