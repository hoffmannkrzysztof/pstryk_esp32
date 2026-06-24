#include "render/EpdDashboard.h"
#include "core/Format.h"       // formatPln(float, char[8])
#include "core/Version.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace pstryk {

namespace {

struct Pal {
  uint16_t bg, ink, mid, light, dark;
  explicit Pal(IRenderer& r)
    : bg(r.rgb(255,255,255)), ink(r.rgb(0,0,0)), mid(r.rgb(150,150,150)),
      light(r.rgb(205,205,205)), dark(r.rgb(40,40,40)) {}
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

// Render a price string (digits, ',' '.') as big 7-seg blocks. Returns end x.
int drawBigPrice(IRenderer& r, const Pal& p, int x, int y, int h, const char* s) {
  int w = (int)(h * 0.58f), t = (int)(h * 0.13f), gap = (int)(h * 0.16f);
  int cx = x;
  for (const char* q = s; *q; ++q) {
    if (*q == ',' || *q == '.') {
      r.fillRect(cx, y + h - t, t, t, p.ink);                    // separator block
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

// Bars + dashed daily-average line + current-hour ring. (Min/max labels are drawn
// by the caller beneath the chart so they never overlap the bars.)
void drawChart(IRenderer& r, const Pal& p, const std::vector<Bar>& bars,
               float avg, int liveIdx, int x, int y, int w, int h) {
  if (bars.empty()) return;
  float maxP = 0.0001f;
  int n = (int)bars.size();
  for (int i = 0; i < n; ++i) if (bars[i].price > maxP) maxP = bars[i].price;
  float top = maxP * 1.12f;
  int gap = 2, bw = (w - (n - 1) * gap) / n; if (bw < 1) bw = 1;
  for (int i = 0; i < n; ++i) {
    int bh = (int)((bars[i].price / top) * h); if (bh < 2) bh = 2;
    int bx = x + i * (bw + gap), by = y + h - bh;
    uint16_t c = (bars[i].price >= avg) ? p.dark : p.light;
    r.fillRect(bx, by, bw, bh, c);
    if (i == liveIdx) r.drawRect(bx - 1, by - 1, bw + 2, bh + 2, p.ink);
  }
  int ay = y + h - (int)((avg / top) * h);
  for (int dx = x; dx < x + w; dx += 10) r.drawLine(dx, ay, dx + 5, ay, p.ink);
}

// "v HH:00 P   ^ HH:00 P" — cheapest / priciest hour + price under a chart.
void extremesLine(IRenderer& r, const Pal& p, const Bar& lo, const Bar& hi, int x, int y) {
  char pl[8], ph[8]; formatPln(lo.price, pl); formatPln(hi.price, ph);
  char s[48];
  std::snprintf(s, sizeof(s), "v %02d:00 %s    ^ %02d:00 %s",
                lo.hour, pl, hi.hour, ph);
  r.text(x, y, s, p.ink, 1);
}

}  // namespace

void drawDashboard(IRenderer& r, const PriceView& v, const EpdStatus& st) {
  Pal p(r);
  r.clear(p.bg);
  const int W = r.width();
  const int M = 24;
  const int LB = r.textHeight(1);   // body line height
  const int LH = r.textHeight(2);   // heading line height

  if (!v.hasData) { drawMessage(r, "Brak danych", st.clockHHMM); return; }

  char buf[8], line[48];

  // --- status bar ---
  char status[64];
  std::snprintf(status, sizeof(status), "Pstryk  %s%s", st.clockHHMM,
                st.stale ? "  * nieakt." : "");
  r.text(M, 10, status, p.ink, 1);
  char bat[20];
  if (st.batteryPct >= 0)
    std::snprintf(bat, sizeof(bat), "%s %d%%", st.batteryLow ? "! BAT" : "BAT", st.batteryPct);
  else
    std::strcpy(bat, st.wifiOk ? "WiFi" : "WiFi?");
  r.text(W - r.textWidth(bat, 1) - M, 10, bat, p.ink, 1);
  int sy = 10 + LB + 6;
  r.drawLine(M, sy, W - M, sy, p.mid);

  // --- hero (left): label, big 7-seg price, below/above-average pill ---
  char hl[6]; hourLabel(v.currentHour, hl);
  char head[24]; std::snprintf(head, sizeof(head), "TERAZ %s", hl);
  int heroY = sy + 12;
  r.text(M, heroY, head, p.ink, 2);

  int priceY = heroY + LH + 8, priceH = 150;
  formatPln(v.currentBuy, buf);
  int endx = drawBigPrice(r, p, M, priceY, priceH, buf);
  r.text(endx + 16, priceY + priceH - LH, "zl/kWh", p.ink, 2);

  int pillY = priceY + priceH + 16;
  const char* tag = v.currentBelowAvg ? "ponizej sredniej" : "powyzej sredniej";
  r.drawRect(M, pillY, r.textWidth(tag, 1) + 28, LB + 12, p.ink);
  r.text(M + 14, pillY + 6, tag, p.ink, 1);

  // --- hero right column: next / sell / average (label + value per line) ---
  int rx = 580, ry = heroY, rstep = LB + 12;
  r.drawLine(rx - 26, heroY, rx - 26, pillY + LB, p.mid);
  char nh[6]; hourLabel(v.nextHour, nh);
  formatPln(v.nextBuy, buf);
  std::snprintf(line, sizeof(line), "Nastepna %s:  %s %s", nh, buf,
                v.nextTrend == Trend::Up ? "^" : (v.nextTrend == Trend::Down ? "v" : "-"));
  r.text(rx, ry, line, p.ink, 1);
  formatPln(v.currentSell, buf);
  std::snprintf(line, sizeof(line), "Sprzedaz PV:  %s", buf);
  r.text(rx, ry + rstep, line, p.ink, 1);
  formatPln(v.todayAvg, buf);
  std::snprintf(line, sizeof(line), "Srednia dzis:  %s", buf);
  r.text(rx, ry + 2 * rstep, line, p.ink, 1);

  // --- charts (Dzis | Jutro) ---
  int cTitleY = 320;
  int half = (W - 3 * M) / 2;
  int barsY = cTitleY + LH + 6, barsH = 110;
  int mmY = barsY + barsH + 8;
  int rxc = M + half + M;

  r.text(M, cTitleY, "Dzis", p.ink, 2);
  drawChart(r, p, v.today, v.todayAvg, v.liveIndex, M, barsY, half, barsH);
  extremesLine(r, p, v.todayCheapest, v.todayExpensive, M, mmY);

  r.text(rxc, cTitleY, "Jutro", p.ink, 2);
  if (v.hasTomorrow) {
    drawChart(r, p, v.tomorrow, v.tomorrowAvg, -1, rxc, barsY, half, barsH);
    extremesLine(r, p, v.tomorrowCheapest, v.tomorrowExpensive, rxc, mmY);
  } else {
    r.text(rxc, barsY + barsH / 2 - LB / 2, "brak danych jeszcze", p.ink, 1);
  }

  r.text(20, r.height() - 24, "v" FIRMWARE_VERSION, p.ink, 1);
  r.present();
}

void drawMessage(IRenderer& r, const char* line1, const char* line2) {
  Pal p(r);
  r.clear(p.bg);
  int lh = r.textHeight(2);
  int y = r.height() / 2 - lh;
  r.text((r.width() - r.textWidth(line1, 2)) / 2, y, line1, p.ink, 2);
  if (line2 && line2[0])
    r.text((r.width() - r.textWidth(line2, 1)) / 2, y + lh + 12, line2, p.ink, 1);
  r.text(20, r.height() - 24, "v" FIRMWARE_VERSION, r.rgb(120, 120, 120), 1);
  r.present();
}

}  // namespace pstryk
