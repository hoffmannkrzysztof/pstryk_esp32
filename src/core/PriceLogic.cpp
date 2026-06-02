#include "core/PriceLogic.h"
#include "core/TimeService.h"

namespace pstryk {

static Bar toBar(const PriceFrame& f) {
  Bar b;
  b.hour = localHour(f.start);
  b.price = f.buy;
  b.isLive = f.isLive;
  b.isCheap = f.isCheap;
  b.isExpensive = f.isExpensive;
  return b;
}

static void extremes(const std::vector<Bar>& bars, Bar& cheap, Bar& exp) {
  if (bars.empty()) { cheap = Bar{}; exp = Bar{}; return; }
  cheap = exp = bars[0];
  for (const Bar& b : bars) {
    if (b.price < cheap.price) cheap = b;
    if (b.price > exp.price)   exp = b;
  }
}

PriceView buildView(const PriceData& data, time_t now) {
  PriceView v;
  if (data.frames.empty()) return v;

  const int todayOrd = localDayOrdinal(now);

  // Split into today / tomorrow by local date.
  for (const PriceFrame& f : data.frames) {
    int ord = localDayOrdinal(f.start);
    if (ord == todayOrd)        v.today.push_back(toBar(f));
    else if (ord == todayOrd + 1) v.tomorrow.push_back(toBar(f));
  }
  if (v.today.empty() && v.tomorrow.empty()) return v;
  v.hasData = true;

  // Today aggregates.
  if (!v.today.empty()) {
    float sum = 0;
    for (size_t i = 0; i < v.today.size(); ++i) {
      sum += v.today[i].price;
      if (v.today[i].isLive) v.liveIndex = (int)i;
    }
    v.todayAvg = sum / v.today.size();
    extremes(v.today, v.todayCheapest, v.todayExpensive);
  }

  // Current frame: prefer is_live; else the frame whose hour contains `now`.
  int cur = v.liveIndex;
  if (cur < 0) {
    for (size_t i = 0; i < data.frames.size(); ++i) {
      if (now >= data.frames[i].start && now < data.frames[i].start + 3600) {
        // map into today vector if present
        for (size_t j = 0; j < v.today.size(); ++j)
          if (v.today[j].hour == localHour(data.frames[i].start)) { cur = (int)j; break; }
        break;
      }
    }
  }
  if (cur >= 0 && cur < (int)v.today.size()) {
    v.currentBuy = v.today[cur].price;
    v.currentHour = v.today[cur].hour;
    v.currentBelowAvg = v.currentBuy <= v.todayAvg;
    // sell price: find matching source frame by hour
    for (const PriceFrame& f : data.frames)
      if (localDayOrdinal(f.start) == todayOrd && localHour(f.start) == v.currentHour) {
        v.currentSell = f.sell; break;
      }
    // next hour trend
    if (cur + 1 < (int)v.today.size()) {
      v.nextBuy = v.today[cur + 1].price;
      v.nextHour = v.today[cur + 1].hour;
      float d = v.nextBuy - v.currentBuy;
      v.nextTrend = (d > 0.001f) ? Trend::Up : (d < -0.001f ? Trend::Down : Trend::Flat);
    }
  }

  // Tomorrow aggregates.
  if (!v.tomorrow.empty()) {
    v.hasTomorrow = true;
    float sum = 0;
    for (const Bar& b : v.tomorrow) sum += b.price;
    v.tomorrowAvg = sum / v.tomorrow.size();
    extremes(v.tomorrow, v.tomorrowCheapest, v.tomorrowExpensive);
  }
  return v;
}

}  // namespace pstryk
