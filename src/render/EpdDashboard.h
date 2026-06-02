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

// White-background status/error message for the e-paper board (boot, errors).
void drawMessage(IRenderer& r, const char* line1, const char* line2);

}  // namespace pstryk
