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
  // incl.; see PriceData.h canonical contract). Renderers display this verbatim --
  // but ONLY when hasCurrent. hasData means "some frames were parsed"; it does not
  // mean one of them covers the current hour, and a gap there used to render the
  // default 0.00 as an authoritative "TERAZ 00:00" next to a real daily average.
  bool  hasCurrent = false;
  float currentBuy = 0, currentSell = 0;
  int   currentHour = 0;
  bool  currentBelowAvg = true;
  // Likewise for the next hour: at 23:00 there is no today[cur+1], and before the
  // day-ahead auction publishes there may be no tomorrow[0] either.
  bool  hasNext = false;
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
