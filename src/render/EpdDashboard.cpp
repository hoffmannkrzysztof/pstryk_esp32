#include "render/EpdDashboard.h"
#include "render/Pages.h"      // renderMessage for the no-data case
#include "core/Format.h"       // formatPln(float, char[8])
#include <cstdio>
#include <cstring>
#include <vector>

namespace pstryk {

namespace {

struct Pal {
  uint16_t bg, ink, mid, light, dark;
  explicit Pal(IRenderer& r)
    : bg(r.rgb(255,255,255)), ink(r.rgb(0,0,0)), mid(r.rgb(140,140,140)),
      light(r.rgb(210,210,210)), dark(r.rgb(40,40,40)) {}
};

// 7-segment masks, bit0=a(top) b c d(bottom) e f g(middle).
const uint8_t SEG[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};

void drawDigit(IRenderer& r, int x, int y, int w, int h, int t, uint16_t c, uint8_t m) {
  int midY = y + (h - t) / 2;
  if (m & 0x01) r.fillRect(x, y, w, t, c);                       // a
  if (m & 0x02) r.fillRect(x + w - t, y, t, h / 2, c);           // b
  if (m & 0x04) r.fillRect(x + w - t, y + h / 2, t, h / 2, c);   // c
  if (m & 0x08) r.fillRect(x, y + h - t, w, t, c);               // d
  if (m & 0x10) r.fillRect(x, y + h / 2, t, h / 2, c);           // e
  if (m & 0x20) r.fillRect(x, y, t, h / 2, c);                   // f
  if (m & 0x40) r.fillRect(x, midY, w, t, c);                    // g
}

// Render a price string (digits, ',' and '.') as big 7-seg blocks. Returns end x.
int drawBigPrice(IRenderer& r, const Pal& p, int x, int y, int h, const char* s) {
  int w = (int)(h * 0.58f), t = (int)(h * 0.13f), gap = (int)(h * 0.16f);
  int cx = x;
  for (const char* q = s; *q; ++q) {
    if (*q == ',' || *q == '.') {
      r.fillRect(cx, y + h - t, t, t, p.ink);                    // comma block
      cx += t + gap;
    } else if (*q >= '0' && *q <= '9') {
      drawDigit(r, cx, y, w, h, t, p.ink, SEG[*q - '0']);
      cx += w + gap;
    } else {
      cx += w / 2;
    }
  }
  return cx;
}

void hourLabel(int hour, char* out) { std::snprintf(out, 6, "%02d:00", hour); }

// Chart in [x,y,w,h]: avg dashed line, above-avg dark / below-avg light bars,
// min v / max ^ tags, current hour ringed.
void drawChart(IRenderer& r, const Pal& p, const std::vector<Bar>& bars,
               float avg, int liveIdx, int x, int y, int w, int h) {
  if (bars.empty()) { r.text(x, y + h / 2 - 10, "brak danych jeszcze", p.mid, 1); return; }
  float maxP = 0.0001f, minP = 1e9f;
  int minI = 0, maxI = 0, n = (int)bars.size();
  for (int i = 0; i < n; ++i) {
    if (bars[i].price > maxP) { maxP = bars[i].price; maxI = i; }
    if (bars[i].price < minP) { minP = bars[i].price; minI = i; }
  }
  float top = maxP * 1.12f;
  int gap = 2, bw = (w - (n - 1) * gap) / n; if (bw < 1) bw = 1;
  for (int i = 0; i < n; ++i) {
    int bh = (int)((bars[i].price / top) * h);
    int bx = x + i * (bw + gap), by = y + h - bh;
    uint16_t c = (bars[i].price >= avg) ? p.dark : p.light;
    r.fillRect(bx, by, bw, bh, c);
    if (i == liveIdx) r.drawRect(bx - 1, by - 1, bw + 2, bh + 2, p.ink);
    if (i == minI) r.text(bx - 2, by - 14, "v", p.ink, 1);
    if (i == maxI) r.text(bx - 2, by - 14, "^", p.ink, 1);
  }
  // dashed average line
  int ay = y + h - (int)((avg / top) * h);
  for (int dx = x; dx < x + w; dx += 10) r.drawLine(dx, ay, dx + 5, ay, p.ink);
}

}  // namespace

void drawDashboard(IRenderer& r, const PriceView& v, const EpdStatus& st) {
  Pal p(r);
  r.clear(p.bg);
  const int W = r.width();

  if (!v.hasData) { renderMessage(r, "Brak danych", st.clockHHMM); return; }

  // --- status bar ---
  char status[64];
  std::snprintf(status, sizeof(status), "Pstryk  %s%s", st.clockHHMM,
                st.stale ? "  * nieaktualne" : "");
  r.text(20, 8, status, p.ink, 1);
  char bat[20];
  if (st.batteryPct >= 0) std::snprintf(bat, sizeof(bat), "%s %d%%",
                                        st.batteryLow ? "! BAT" : "BAT", st.batteryPct);
  else std::strcpy(bat, st.wifiOk ? "WiFi" : "WiFi?");
  r.text(W - r.textWidth(bat, 1) - 20, 8, bat, p.ink, 1);
  r.drawLine(20, 40, W - 20, 40, p.light);

  // --- hero ---
  char buf[8];
  char hl[6]; hourLabel(v.currentHour, hl);
  char head[24]; std::snprintf(head, sizeof(head), "TERAZ %s", hl);
  r.text(28, 56, head, p.ink, 1);
  formatPln(v.currentBuy, buf);
  int endx = drawBigPrice(r, p, 28, 92, 150, buf);
  r.text(endx + 16, 200, "zl/kWh", p.ink, 1);
  // below/above average pill
  const char* tag = v.currentBelowAvg ? "ponizej sredniej" : "powyzej sredniej";
  int pw = r.textWidth(tag, 1) + 24;
  r.drawRect(28, 250, pw, 30, p.ink);
  r.text(40, 257, tag, p.ink, 1);

  // hero right column
  int rx = 560;
  r.drawLine(rx - 24, 56, rx - 24, 280, p.light);
  char nh[6]; hourLabel(v.nextHour, nh);
  char line[28];
  formatPln(v.nextBuy, buf);
  std::snprintf(line, sizeof(line), "Nastepna %s", nh);
  r.text(rx, 64, line, p.mid, 1);
  std::snprintf(line, sizeof(line), "%s %s", buf,
                v.nextTrend == Trend::Up ? "^" : (v.nextTrend == Trend::Down ? "v" : "-"));
  r.text(rx, 84, line, p.ink, 1);
  formatPln(v.currentSell, buf);
  r.text(rx, 130, "Sprzedaz PV", p.mid, 1); r.text(rx, 150, buf, p.ink, 1);
  formatPln(v.todayAvg, buf);
  r.text(rx, 196, "Srednia dzis", p.mid, 1); r.text(rx, 216, buf, p.ink, 1);

  // --- charts ---
  int cy = 330, ch = 150, half = (W - 60) / 2;
  r.text(28, cy - 22, "Dzis", p.ink, 1);
  drawChart(r, p, v.today, v.todayAvg, v.liveIndex, 28, cy, half, ch);
  int rxc = 28 + half + 24;
  r.text(rxc, cy - 22, "Jutro", p.ink, 1);
  if (v.hasTomorrow)
    drawChart(r, p, v.tomorrow, v.tomorrowAvg, -1, rxc, cy, half, ch);
  else
    r.text(rxc, cy + ch / 2 - 10, "Jutro: brak danych jeszcze", p.mid, 1);

  r.present();
}

}  // namespace pstryk
