#include "render/Pages.h"
#include "core/Format.h"
#include "core/Version.h"
#include <cstdio>
#include <cstring>

namespace pstryk {

// Palette (RGB888 -> renderer.rgb()).
struct Palette {
  uint16_t bg, text, muted, green, red, amber, barbg, line;
  explicit Palette(IRenderer& r)
    : bg(r.rgb(0x0a,0x0e,0x14)), text(r.rgb(0xe8,0xed,0xf4)),
      muted(r.rgb(0x8b,0x97,0xa8)), green(r.rgb(0x34,0xd3,0x99)),
      red(r.rgb(0xf8,0x71,0x71)), amber(r.rgb(0xfb,0xbf,0x24)),
      barbg(r.rgb(0x3b,0x4a,0x66)), line(r.rgb(0x1e,0x27,0x38)) {}
};

static void hourLabel(int hour, char* out) { std::snprintf(out, 6, "%02d:00", hour); }

static void drawStrip(IRenderer& r, const Palette& p, const char* left,
                      bool stale, int dotIdx, int dotCount) {
  r.text(16, 8, left, p.muted, 1);
  if (stale) r.text(r.textWidth(left, 1) + 28, 8, "* nieaktualne", p.amber, 1);
  // page dots, right-aligned
  int dotR = 6, gap = 8, total = dotCount * (dotR + gap);
  int x = r.width() - 16 - total;
  for (int i = 0; i < dotCount; ++i) {
    uint16_t c = (i == dotIdx) ? p.text : p.barbg;
    r.fillRoundRect(x + i * (dotR + gap), 9, dotR, dotR, dotR / 2, c);
  }
}

// Generic bar chart in the rect [x,y,w,h]; rings the live bar.
static void drawChart(IRenderer& r, const Palette& p, const std::vector<Bar>& bars,
                      int x, int y, int w, int h) {
  if (bars.empty()) { r.text(x, y + h / 2, "Brak danych", p.muted, 2); return; }
  // Zero-baseline scaling so negative prices render as bars hanging below the
  // baseline instead of vanishing (Arduino_GFX normalizes a negative height
  // into garbage geometry). Positive-only data keeps the exact old layout.
  float maxP = 0.0001f, minP = 0.0f;
  for (const Bar& b : bars) {
    if (b.price > maxP) maxP = b.price;
    if (b.price < minP) minP = b.price;
  }
  maxP *= 1.1f;
  float bottom = (minP < 0.0f) ? minP * 1.1f : 0.0f;
  float span = maxP - bottom;
  int zeroY = y + (int)((maxP / span) * h);
  int n = (int)bars.size();
  int gap = 3;
  int bw = (w - (n - 1) * gap) / n; if (bw < 1) bw = 1;
  for (int i = 0; i < n; ++i) {
    const Bar& b = bars[i];
    float ap = b.price >= 0.0f ? b.price : -b.price;
    int bh = (int)((ap / span) * h);
    int bx = x + i * (bw + gap);
    int by = b.price >= 0.0f ? zeroY - bh : zeroY;
    if (by + bh > y + h) bh = y + h - by;
    uint16_t c = b.isCheap ? p.green : (b.isExpensive ? p.red : p.barbg);
    r.fillRect(bx, by, bw, bh, c);
    if (b.isLive) r.drawRect(bx - 1, by - 1, bw + 2, bh + 2, p.text);
  }
}

static void pageTeraz(IRenderer& r, const Palette& p, const PriceView& v) {
  char hl[6], buf[8];
  hourLabel(v.currentHour, hl);
  char lbl[24]; std::snprintf(lbl, sizeof(lbl), "TERAZ %s-%02d:00", hl, v.currentHour + 1);
  r.text(16, 34, lbl, p.muted, 1);

  uint16_t tag = v.currentBelowAvg ? p.green : p.red;
  r.text(16 + r.textWidth(lbl, 1) + 14, 34,
         v.currentBelowAvg ? "ponizej sredniej" : "powyzej sredniej", tag, 1);

  formatPln(v.currentBuy, buf);
  r.text(16, 56, buf, p.text, 8);                 // big price ~48x64 chars
  int after = 16 + r.textWidth(buf, 8) + 12;
  r.text(after, 64, "zl/kWh", p.muted, 2);
  uint16_t tc = (v.nextTrend == Trend::Up) ? p.red : (v.nextTrend == Trend::Down ? p.green : p.muted);
  const char* arrow = (v.nextTrend == Trend::Up) ? "^" : (v.nextTrend == Trend::Down ? "v" : "-");
  r.text(after, 96, arrow, tc, 4);

  // right column
  int rx = 430;
  r.drawLine(rx - 16, 44, rx - 16, 168, p.line);
  formatPln(v.currentSell, buf);
  r.text(rx, 44, "SPRZEDAZ (PV)", p.muted, 1);  r.text(rx, 56, buf, p.text, 3);
  formatPln(v.todayAvg, buf);
  r.text(rx, 92, "SREDNIA DZIS", p.muted, 1);   r.text(rx, 104, buf, p.text, 3);
  char nh[6]; hourLabel(v.nextHour, nh);
  char nl[18]; std::snprintf(nl, sizeof(nl), "NASTEPNA %s", nh);
  formatPln(v.nextBuy, buf);
  r.text(rx, 140, nl, p.muted, 1);              r.text(rx, 152, buf, tc, 3);
}

static void pageChart(IRenderer& r, const Palette& p, const PriceView& v) {
  drawChart(r, p, v.today, 16, 40, 608, 110);
  r.text(16, 156, "00", p.muted, 1);
  r.text(16 + 608 / 4, 156, "06", p.muted, 1);
  r.text(16 + 608 / 2, 156, "12", p.muted, 1);
  r.text(16 + 3 * 608 / 4, 156, "18", p.muted, 1);
  r.text(600, 156, "23", p.muted, 1);
}

static void pageExtremes(IRenderer& r, const Palette& p, const PriceView& v) {
  char hl[6], buf[8];
  // cheapest box
  r.fillRoundRect(16, 44, 296, 120, 12, r.rgb(0x10,0x2a,0x22));
  r.text(32, 56, "v NAJTANIEJ DZIS", p.green, 1);
  hourLabel(v.todayCheapest.hour, hl); r.text(32, 80, hl, p.text, 5);
  formatPln(v.todayCheapest.price, buf);
  char z1[16]; std::snprintf(z1, sizeof(z1), "%s zl/kWh", buf);
  r.text(32, 132, z1, p.muted, 2);
  // most expensive box
  r.fillRoundRect(328, 44, 296, 120, 12, r.rgb(0x2a,0x12,0x12));
  r.text(344, 56, "^ NAJDROZEJ DZIS", p.red, 1);
  hourLabel(v.todayExpensive.hour, hl); r.text(344, 80, hl, p.text, 5);
  formatPln(v.todayExpensive.price, buf);
  char z2[16]; std::snprintf(z2, sizeof(z2), "%s zl/kWh", buf);
  r.text(344, 132, z2, p.muted, 2);
}

static void pageJutro(IRenderer& r, const Palette& p, const PriceView& v) {
  drawChart(r, p, v.tomorrow, 16, 36, 608, 90);
  char hl[6], buf[8], line[40];
  hourLabel(v.tomorrowCheapest.hour, hl); formatPln(v.tomorrowCheapest.price, buf);
  std::snprintf(line, sizeof(line), "v %s  %s zl", hl, buf);
  r.text(16, 138, "Najtaniej", p.muted, 1); r.text(16, 152, line, p.green, 2);
  hourLabel(v.tomorrowExpensive.hour, hl); formatPln(v.tomorrowExpensive.price, buf);
  std::snprintf(line, sizeof(line), "^ %s  %s zl", hl, buf);
  r.text(330, 138, "Najdrozej", p.muted, 1); r.text(330, 152, line, p.red, 2);
}

void renderPage(IRenderer& r, Page page, const PriceView& v,
                bool stale, const char* clockHHMM, int pageDotIndex, int pageDotCount) {
  Palette p(r);
  r.clear(p.bg);

  char left[40];
  switch (page) {
    case Page::Teraz:    std::snprintf(left, sizeof(left), "%s  Pstryk", clockHHMM); break;
    case Page::Chart:    std::snprintf(left, sizeof(left), "Dzis"); break;
    case Page::Extremes: std::snprintf(left, sizeof(left), "Dzis"); break;
    case Page::Jutro:    std::snprintf(left, sizeof(left), "Jutro"); break;
  }
  drawStrip(r, p, left, stale, pageDotIndex, pageDotCount);

  if (!v.hasData) { r.text(16, 80, "Brak danych", p.muted, 3); r.present(); return; }
  switch (page) {
    case Page::Teraz:    pageTeraz(r, p, v); break;
    case Page::Chart:    pageChart(r, p, v); break;
    case Page::Extremes: pageExtremes(r, p, v); break;
    case Page::Jutro:    pageJutro(r, p, v); break;
  }
  const char* ver = "v" FIRMWARE_VERSION;
  r.text(r.width() - r.textWidth(ver, 1) - 8, r.height() - 12, ver, p.muted, 1);
  r.present();
}

void renderMessage(IRenderer& r, const char* line1, const char* line2) {
  Palette p(r);
  r.clear(p.bg);
  r.text(16, 60, line1, p.text, 3);
  if (line2 && line2[0]) r.text(16, 110, line2, p.muted, 2);
  const char* ver = "v" FIRMWARE_VERSION;
  r.text(r.width() - r.textWidth(ver, 1) - 8, r.height() - 12, ver, p.muted, 1);
  r.present();
}

}  // namespace pstryk
