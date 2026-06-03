#pragma once
#include <ctime>
#include <vector>

namespace pstryk {

// CANONICAL DISPLAY CONTRACT (all boards): `buy` is the headline price every
// board shows. It is api field `price_gross` (PLN/kWh, VAT incl.) -- NEVER
// price_net/tge_price/full_price. This single mapping (set in PstrykParse.cpp,
// locked by test_buy_is_gross) is the one source of truth, so adding a board
// can't re-introduce the tax-stripped e-paper bug. New renderers must display
// PriceView.currentBuy and must not re-derive price from raw api fields.
struct PriceFrame {
  time_t start = 0;        // UTC epoch seconds, start of the hour
  float  buy   = 0.0f;     // price_gross, PLN/kWh (VAT incl.) -- canonical, see above
  float  sell  = 0.0f;     // price_prosumer_gross, PLN/kWh (PV sell, VAT incl.)
  bool   isLive = false;
  bool   isCheap = false;
  bool   isExpensive = false;
};

struct PriceData {
  std::vector<PriceFrame> frames;  // ordered by start ascending
};

}  // namespace pstryk
