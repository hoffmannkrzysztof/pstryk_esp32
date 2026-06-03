#pragma once
#include "render/IRenderer.h"
#include "view/PriceView.h"

namespace pstryk {

enum class Page { Teraz, Chart, Extremes, Jutro };

// Draws one page (incl. status strip) and flushes. `clockHHMM` may be "" if unknown.
void renderPage(IRenderer& r, Page page, const PriceView& v,
                bool stale, const char* clockHHMM, int pageDotIndex, int pageDotCount);

// Full-screen status/error message (boot, provisioning, errors).
void renderMessage(IRenderer& r, const char* line1, const char* line2);

}  // namespace pstryk
