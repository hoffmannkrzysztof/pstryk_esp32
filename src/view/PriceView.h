#pragma once
#include <vector>

namespace pstryk {

enum class Trend { Up, Down, Flat };

struct Bar {
  int   hour = 0;          // local hour 0..23
  float price = 0.0f;      // buy price PLN/kWh
  bool  isLive = false;
  bool  isCheap = false;
  bool  isExpensive = false;
};

struct PriceView {
  bool  hasData = false;

  // Page: Teraz
  // currentBuy is THE headline value every board renders (= price_gross, VAT
  // incl.; see PriceData.h canonical contract). Renderers display this verbatim.
  float currentBuy = 0, currentSell = 0;
  int   currentHour = 0;
  bool  currentBelowAvg = true;
  Trend nextTrend = Trend::Flat;
  float nextBuy = 0;
  int   nextHour = 0;
  float todayAvg = 0;

  // Page: Wykres 24h + Najtaniej/Najdrozej (today)
  std::vector<Bar> today;
  int   liveIndex = -1;          // index into `today` of the live hour, or -1
  Bar   todayCheapest, todayExpensive;

  // Page: Jutro
  bool  hasTomorrow = false;
  std::vector<Bar> tomorrow;
  float tomorrowAvg = 0;
  Bar   tomorrowCheapest, tomorrowExpensive;
};

}  // namespace pstryk
