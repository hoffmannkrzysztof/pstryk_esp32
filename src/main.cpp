#include <Arduino.h>
#include "render/LongRenderer.h"
#include "render/Pages.h"

pstryk::LongRenderer gfx;
pstryk::PriceView mockView() {
  pstryk::PriceView v; v.hasData = true;
  v.currentBuy = 0.52; v.currentSell = 0.31; v.currentHour = 14;
  v.currentBelowAvg = true; v.nextTrend = pstryk::Trend::Down; v.nextBuy = 0.48; v.nextHour = 15;
  v.todayAvg = 0.61; v.liveIndex = 2;
  for (int h = 0; h < 24; ++h) {
    pstryk::Bar b; b.hour = h; b.price = 0.2f + 0.03f * h; b.isLive = (h == 14);
    b.isCheap = (h < 5); b.isExpensive = (h >= 18 && h <= 20); v.today.push_back(b);
  }
  v.todayCheapest = {3, 0.21f, false, true, false};
  v.todayExpensive = {19, 1.12f, false, false, true};
  v.hasTomorrow = true; v.tomorrow = v.today; v.tomorrowCheapest = {12, 0.19f};
  v.tomorrowExpensive = {19, 1.04f};
  return v;
}

void setup() {
  Serial.begin(115200); delay(300);
  gfx.begin();
}

int page = 0;
void loop() {
  auto v = mockView();
  pstryk::Page pages[] = {pstryk::Page::Teraz, pstryk::Page::Chart,
                          pstryk::Page::Extremes, pstryk::Page::Jutro};
  pstryk::renderPage(gfx, pages[page % 4], v, false, "14:32", page % 4, 4);
  page++;
  delay(3000);
}
