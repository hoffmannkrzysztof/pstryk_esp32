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

}  // namespace pstryk
