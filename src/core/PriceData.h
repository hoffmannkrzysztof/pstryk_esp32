#pragma once
#include <ctime>
#include <vector>

namespace pstryk {

struct PriceFrame {
  time_t start = 0;        // UTC epoch seconds, start of the hour
  float  buy   = 0.0f;     // price_gross, PLN/kWh (VAT incl.)
  float  sell  = 0.0f;     // price_prosumer_gross, PLN/kWh
  bool   isLive = false;
  bool   isCheap = false;
  bool   isExpensive = false;
};

struct PriceData {
  std::vector<PriceFrame> frames;  // ordered by start ascending
};

}  // namespace pstryk
