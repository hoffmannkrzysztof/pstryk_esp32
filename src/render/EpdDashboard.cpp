#include "render/EpdDashboard.h"
#include "core/Format.h"       // formatPln(float, char[8])
#include "core/Version.h"
#include "core/SetupQr.h"      // buildWifiQrPayload
#include "qrcode.h"            // ricmoo/QRCode (naglowek sam robi extern "C")
#include <cstdio>
#include <cstring>
#include <cmath>
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
    } else if (*q == '-') {
      // Negative price: the g (middle) segment. Dropping the sign here showed
      // |price| as the headline whenever the market went negative.
      r.fillRect(cx, y + (h - t) / 2, w, t, p.ink);
      cx += w + gap;
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
  // Scale from a zero baseline that admits negative prices (real on the PL
  // day-ahead market): positive bars grow up from the baseline, negative bars
  // hang below it, everything stays inside [y, y+h]. With no negatives the
  // baseline is y+h and the rendering is identical to the positive-only layout.
  float maxP = 0.0001f, minP = 0.0f;
  int n = (int)bars.size();
  for (int i = 0; i < n; ++i) {
    if (bars[i].price > maxP) maxP = bars[i].price;
    if (bars[i].price < minP) minP = bars[i].price;
  }
  float top = maxP * 1.12f;
  float bottom = (minP < 0.0f) ? minP * 1.12f : 0.0f;
  float span = top - bottom;
  // The EPD rasteriser bounds its pixel WRITES but not its loop counts, so every
  // float->int conversion has to be bounded on our side: (int)NaN saturates to
  // INT_MAX on Xtensa and epd_fill_rect would then iterate that many times.
  if (!std::isfinite(span) || span <= 0.0f) return;
  int zeroY = y + (int)((top / span) * h);
  if (zeroY < y) zeroY = y;
  if (zeroY > y + h) zeroY = y + h;
  int gap = 2, bw = (w - (n - 1) * gap) / n; if (bw < 1) bw = 1;
  for (int i = 0; i < n; ++i) {
    float ap = bars[i].price >= 0.0f ? bars[i].price : -bars[i].price;
    int bh = (int)((ap / span) * h); if (bh < 2) bh = 2;
    if (bh > h) bh = h;
    int bx = x + i * (bw + gap);
    int by = bars[i].price >= 0.0f ? zeroY - bh : zeroY;
    if (by < y) { bh -= (y - by); by = y; }
    if (bh < 1) continue;
    if (by + bh > y + h) bh = y + h - by;   // clamp a rounded-up negative stub
    uint16_t c = (bars[i].price >= avg) ? p.dark : p.light;
    r.fillRect(bx, by, bw, bh, c);
    if (i == liveIdx) r.drawRect(bx - 1, by - 1, bw + 2, bh + 2, p.ink);
  }
  int ay = std::isfinite(avg) ? zeroY - (int)((avg / span) * h) : y + h - 1;
  if (ay < y) ay = y;
  if (ay > y + h - 1) ay = y + h - 1;       // keep the dashes inside the chart
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

// Obrys baterii + nub + wypelnienie proporcjonalne do pct; pct < 0 (nieznany)
// zostawia pusta ikone -- spojnie z EpdStatus::batteryPct == -1.
void drawBatteryIcon(IRenderer& r, const Pal& p, int x, int y, int w, int h, int pct) {
  r.drawRect(x, y, w, h, p.ink);
  r.fillRect(x + w, y + h / 2 - h / 6, 4, h / 3, p.ink);   // styk
  if (pct >= 0) {
    int inner = (w - 6) * pct / 100;
    if (inner > 0) r.fillRect(x + 3, y + 3, inner, h - 6, p.ink);
  }
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
  // hasData does not imply a frame covering this hour: a gap in frames[] (partial
  // API publish, a re-priced frame) used to render the default 0.00 as an
  // authoritative "TERAZ 00:00" beside a real daily average -- internally
  // contradictory rather than obviously broken.
  char hl[6]; hourLabel(v.currentHour, hl);
  char head[24];
  std::snprintf(head, sizeof(head), "TERAZ %s", v.hasCurrent ? hl : "--:--");
  int heroY = sy + 12;
  r.text(M, heroY, head, p.ink, 2);

  int priceY = heroY + LH + 8, priceH = 150;
  const int rx = 580;                       // right column; hero must stay left of it
  if (v.hasCurrent) formatPln(v.currentBuy, buf);
  else              std::strcpy(buf, "--,--");
  int endx = drawBigPrice(r, p, M, priceY, priceH, buf);
  // The unit label sits after the price, and the sign added by the negative-price
  // fix costs a full digit cell -- enough for "-0,45 zl/kWh" to cross the column
  // separator. Drop the label under the price when it no longer fits.
  int unitW = r.textWidth("zl/kWh", 2);
  if (endx + 16 + unitW <= rx - 26) r.text(endx + 16, priceY + priceH - LH, "zl/kWh", p.ink, 2);
  else                              r.text(M, priceY + priceH + 2, "zl/kWh", p.ink, 2);

  int pillY = priceY + priceH + 16 + (endx + 16 + unitW <= rx - 26 ? 0 : LH);
  if (v.hasCurrent) {                       // no average comparison without a price
    const char* tag = v.currentBelowAvg ? "ponizej sredniej" : "powyzej sredniej";
    r.drawRect(M, pillY, r.textWidth(tag, 1) + 28, LB + 12, p.ink);
    r.text(M + 14, pillY + 6, tag, p.ink, 1);
  }

  // --- hero right column: next / sell / average (label + value per line) ---
  int ry = heroY, rstep = LB + 12;
  r.drawLine(rx - 26, heroY, rx - 26, pillY + LB, p.mid);
  char nh[6]; hourLabel(v.nextHour, nh);
  if (v.hasNext) {
    formatPln(v.nextBuy, buf);
    std::snprintf(line, sizeof(line), "Nastepna %s:  %s %s", nh, buf,
                  v.nextTrend == Trend::Up ? "^" : (v.nextTrend == Trend::Down ? "v" : "-"));
  } else {
    std::snprintf(line, sizeof(line), "Nastepna --:--:  --,--");
  }
  r.text(rx, ry, line, p.ink, 1);
  formatPln(v.currentSell, buf);
  if (!v.hasCurrent) std::strcpy(buf, "--,--");   // same source frame as the hero
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

// Ekran konfiguracji portalu. Wspolrzedne stref: spec
// docs/superpowers/specs/2026-08-24-epaper-setup-screen-design.md, makieta
// docs/mockups/epaper-setup.html (ta sama mapa liczb).
void drawSetupScreen(IRenderer& r, const SetupInfo& info) {
  Pal p(r);
  r.clear(p.bg);
  const int W = r.width();
  const int M = 24;

  // --- naglowek: tytul + badge baterii ---
  r.text(M, 10, "Pstryk - Konfiguracja", p.ink, 1);
  char bat[16];
  if (info.batteryPct >= 0)
    std::snprintf(bat, sizeof(bat), "BAT %d %%", info.batteryPct);
  else
    std::strcpy(bat, "BAT ?");
  int batX = W - M - r.textWidth(bat, 1);
  r.text(batX, 10, bat, p.ink, 1);
  drawBatteryIcon(r, p, batX - 40 - 12, 16, 36, 18, info.batteryPct);
  r.drawLine(M, 52, W - M, 52, p.mid);

  // --- lewa kolumna: kroki ---
  r.text(M, 64, "1. Polacz telefon z siecia:", p.ink, 1);
  r.text(48, 100, info.ssid, p.ink, 2);
  r.text(M, 160, "2. Wpisz haslo:", p.ink, 1);
  // 8 cyfr przy h=80 -> w=46, t=10, gap=12 -> 452 px; ramka z paddingiem 24/18.
  r.drawRect(48, 196, 500, 116, p.ink);
  drawBigPrice(r, p, 72, 214, 80, info.password);
  char step3[48];
  std::snprintf(step3, sizeof(step3), "3. Otworz w przegladarce: %s", info.portalUrl);
  r.text(M, 336, step3, p.ink, 1);

  // --- prawa kolumna: QR do sieci (v3 = 29 modulow po 8 px = 232 px) ---
  // Bez ramki dookola: biale tlo panelu JEST quiet zone (>= 32 px z kazdej
  // strony), a jakikolwiek tusz w poblizu zjada margines skanowania.
  char payload[43];  // 42 = dokladna pojemnosc QR v3-M w trybie bajtowym + NUL;
                     // buildWifiQrPayload zwraca false gdy sie nie miesci, wiec
                     // ten rozmiar bufora JEST straznikiem pojemnosci (w QRCode
                     // 0.0.1 qrcode_initText nie umie odmowic za dlugich danych).
  if (buildWifiQrPayload(info.ssid, info.password, payload, sizeof(payload))) {
    QRCode qr;
    uint8_t qrData[106];   // qrcode_getBufferSize(3) = ((29*29)+7)/8
    if (qrcode_initText(&qr, qrData, 3, ECC_MEDIUM, payload) >= 0) {
      const int mod = 8, qx = 660, qy = 90, qpx = 29 * mod;
      for (uint8_t my = 0; my < qr.size; ++my)
        for (uint8_t mx = 0; mx < qr.size; ++mx)
          if (qrcode_getModule(&qr, mx, my))
            r.fillRect(qx + mx * mod, qy + my * mod, mod, mod, p.ink);
      const char* cap = "Zeskanuj telefonem";
      r.text(qx + (qpx - r.textWidth(cap, 1)) / 2, qy + qpx + 38, cap, p.ink, 1);
    }
  }
  // Niepowodzenie QR zostawia strefe pusta -- cyfry wyzej sa sciezka nadrzedna.
  // Straznikiem jest rozmiar bufora payload (42 B pojemnosci v3-M), nie
  // qrcode_initText: w wersji 0.0.1 przepelnienie nie zwraca bledu, tylko
  // koduje niedekodowalny QR. Warunek >= 0 zostaje jako pas i szelki.

  // --- prosba o ladowarke ---
  r.drawLine(M, 400, W - M, 400, p.mid);
  drawBatteryIcon(r, p, M, 426, 44, 24, info.batteryPct);
  r.text(84, 416, "Podlacz mnie do ladowarki", p.ink, 2);
  r.text(84, 468, "Rozdaje tu Wi-Fi i szybko sie rozladowuje.", p.ink, 1);

  r.text(20, r.height() - 24, "v" FIRMWARE_VERSION, r.rgb(120, 120, 120), 1);
  r.present();
}

// Ekran powitalny / niedokonczonej konfiguracji. E-papier trzyma obraz bez
// zasilania, wiec to, co ten ekran namaluje po fabrycznym pierwszym rozruchu,
// jedzie w pudelku do klienta -- wizytowka produktu, nie komunikat bledu.
// Wspolrzedne stref: spec docs/superpowers/specs/2026-08-25-epaper-welcome-
// screen-design.md; blok akcji (rzad przyciskow, "Nacisnij i pusc") wg spec
// docs/superpowers/specs/2026-09-02-setup-ux-polish-portal-and-button-cue-
// design.md, B.1; makieta docs/mockups/epaper-welcome.html (ta sama mapa
// liczb). Pozycje prawostronne i rzad checkboxow licza sie z textWidth() w
// runtime -- staly x tylko tam, gdzie tekst zaczyna sie od lewej.
void drawWelcomeScreen(IRenderer& r, int batteryPct) {
  Pal p(r);
  r.clear(p.bg);
  const int W = r.width();
  const int M = 24;

  // --- naglowek: etykieta stanu + badge baterii (uklad 1:1 z drawSetupScreen) ---
  r.text(M, 10, "Pstryk - Pierwsze uruchomienie", p.ink, 1);
  char bat[16];
  if (batteryPct >= 0)
    std::snprintf(bat, sizeof(bat), "BAT %d %%", batteryPct);
  else
    std::strcpy(bat, "BAT ?");
  int batX = W - M - r.textWidth(bat, 1);
  r.text(batX, 10, bat, p.ink, 1);
  drawBatteryIcon(r, p, batX - 40 - 12, 16, 36, 18, batteryPct);
  r.drawLine(M, 52, W - M, 52, p.mid);

  // --- plakietka marki: cien, czarna plyta, bialy piorun ---
  // Sylwetka jak w symbolu wysokiego napiecia: dwie rownolegle przekatne
  // kreski (plastry 8 px schodzace o 6 px w lewo, ~37 stopni), zlamane w
  // polowie polka wystajaca w prawo. Plastry musza byc cienkie: grubsze rzedy
  // zabkuja OBIE krawedzie i polka ginie w tym szumie (sprawdzone na makiecie).
  r.fillRoundRect(82, 124, 280, 280, 56, p.light);
  r.fillRoundRect(70, 112, 280, 280, 56, p.ink);
  for (int i = 0; i < 12; ++i) r.fillRect(234 - 6 * i, 156 + 8 * i, 52, 9, p.bg);
  r.fillRect(142, 252, 114, 32, p.bg);   // polka: jedyny element lamiacy rytm
  for (int i = 0; i < 8; ++i)  r.fillRect(196 - 6 * i, 284 + 8 * i, 44, 9, p.bg);

  // --- prawa kolumna: powitanie i obietnica z mini-wykresem ---
  const int cx = 420;
  r.text(cx, 96, "Czesc, jestem Pstryk.", p.ink, 2);
  r.text(cx, 157, "Pokaze Ci, kiedy prad jest tani,", p.ink, 1);
  r.text(cx, 187, "a kiedy lepiej poczekac.", p.ink, 1);
  // 5 slupkow bez liczb i legendy; ciemniejsze = drozsze, jak w drawChart.
  r.fillRect(798, 202, 14, 10, p.mid);
  r.fillRect(818, 196, 14, 16, p.mid);
  r.fillRect(838, 188, 14, 24, p.ink);
  r.fillRect(858, 200, 14, 12, p.mid);
  r.fillRect(878, 192, 14, 20, p.ink);
  r.drawLine(796, 212, 894, 212, p.mid);

  // --- blok akcji: piktogram "3 klawisze, nacisnij i pusc pierwszy od lewej",
  //     2 linie head, 1 linia body ---
  // Ramka piktogramu x 420..548, y 252..352 (128x100); tusz x 420..547,
  // y 254..345. Rzad TRZECH klawiszy, bo na obudowie jest rzad przyciskow
  // i wlasciwy jest skrajny lewy: pojedynczy przycisk nie mowilby "ktory",
  // a strzalka poza ekran wymagalaby znajomosci krawedzi obudowy (gora/dol
  // nadal nieznana). Stad tez zadnej sylwetki obudowy -- tylko neutralna
  // linia bazowa, wcieta o 6 px z obu stron, zeby nie czytala sie jako
  // listwa. Rzad wypelnia ramke co do piksela (36 px, odstep 10), zaczyna sie
  // w kolumnie linii body (x 420), a jego gorna krawedz lezy na wierszu head
  // "1. przycisk od lewej" (y 300): kazda linia tekstu ma swoj kawalek rysunku.
  r.fillRoundRect(420, 300, 36, 36, 7, p.ink);   // klawisz 1 (skrajny lewy): wypelniony = ten wlasciwy
  r.fillRoundRect(466, 300, 36, 36, 7, p.ink);   // klawisz 2: kontur 2 px (ink + bg, jak pierscien)
  r.fillRoundRect(468, 302, 32, 32, 5, p.bg);
  r.fillRoundRect(512, 300, 36, 36, 7, p.ink);   // klawisz 3: kontur 2 px
  r.fillRoundRect(514, 302, 32, 32, 5, p.bg);
  // Linia bazowa 2 px w p.dark, 8 px pod klawiszami: p.mid o grubosci 1 px
  // ginal po kwantyzacji do 16 szarosci (sprawdzone na podgladzie posterize).
  r.fillRect(426, 344, 116, 2, p.dark);
  // Palec 14x36 r=7 (ksztalt jak dotad) naciska klawisz 1 od gory, na jego
  // osi, dol 6 px w klawiszu; halo p.bg 20x42 r=10 daje 3 px biala szczeline,
  // zeby czubek nie zlewal sie z czarnym klawiszem. Oba maja 2r == w, wiec
  // dziala kwirk pelnego kola (patrz ikona klucza nizej), ale BEZ przesuniecia
  // bazy o +1: dwa epd_fill_circle o srednicy 2r+1 i srodkach x+r oraz
  // x+w-r-1 maluja razem w+2 px SYMETRYCZNIE, po 1 px z obu stron (halo
  // 427..448, palec 430..445 -- srodek 437.5 = srodek klawisza 420..455),
  // a +1 zepchneloby palec 1 px z osi klawisza. W pionie kwirku nie ma
  // (h > 2r), wiec y 1:1 z makieta.
  r.fillRoundRect(428, 267, 20, 42, 10, p.bg);   // halo palca
  r.fillRoundRect(431, 270, 14, 36, 7, p.ink);   // palec
  // Gest "w gore" = puszczenie: daszek ^ nad palcem (jak dawne iskry, ale
  // zbiegajace sie w gore). Kazde ramie DWA razy (y i y+1), bo drawLine ma
  // 1 px, a makieta rysuje 2 px -- inaczej daszek zniknalby przy ogladaniu
  // z reki.
  r.drawLine(426, 264, 438, 254, p.ink);   // ramie lewe (w gore do wierzcholka)
  r.drawLine(426, 265, 438, 255, p.ink);   //   drugi piksel
  r.drawLine(450, 264, 438, 254, p.ink);   // ramie prawe
  r.drawLine(450, 265, 438, 255, p.ink);   //   drugi piksel
  r.text(564, 252, "Nacisnij i pusc", p.ink, 2);        // adv 248 -> koniec 812
  r.text(564, 300, "1. przycisk od lewej", p.ink, 2);   // adv 324 -> koniec 888, 48 px do marginesu
  // Krotkie nacisniecie zamiast trzymania 3 s (na niekompletnej konfiguracji
  // firmware honoruje kazde nacisniecie, SleepCycle::setup), ale e-papier
  // odpowiada dopiero po bocie, odczycie baterii i pelnym przemalowaniu
  // (~3-5 s) -- bez tej linii czesc osob nacisnie drugi raz.
  r.text(cx, 386, "Za chwile pokaze ustawienia.", p.dark, 1);

  // --- pasek "Przygotuj": 4 checkboxy + sciezka do klucza API ---
  // Uprzedza oba najczestsze scenariusze porazki, ZANIM portal sie otworzy:
  // brak klucza pod reka i padnieta w polowie konfiguracji bateria.
  r.drawLine(M, 428, W - M, 428, p.mid);
  r.text(M, 442, "Przygotuj:", p.ink, 1);
  int cbX = M + r.textWidth("Przygotuj:", 1) + 16;
  static const char* const kPrep[4] = {"telefon", "haslo Wi-Fi", "klucz API", "ladowarka"};
  for (int i = 0; i < 4; ++i) {
    r.drawRect(cbX, 448, 18, 18, p.ink);
    r.drawRect(cbX + 1, 449, 16, 16, p.ink);   // podwojny obrys = optyczne 2 px
    r.text(cbX + 28, 442, kPrep[i], p.ink, 1);
    cbX += 28 + r.textWidth(kPrep[i], 1) + 28;
  }
  // Ikona klucza: glowka z otworem + trzon + dwa zeby. Baza M+1, nie M:
  // fillRoundRect sklada kolo z epd_fill_circle o srednicy 2r+1 wokol srodkow
  // x+r i x+w-r-1, wiec pelne kolo (2r == w) maluje 1 px poza nominalny box
  // z kazdej strony, takze w lewo (EpdRenderer.cpp:47) -- glowka na x=M
  // wchodzilaby w margines. Tu +1 nie szkodzi (glowka nie musi trafiac w
  // zadna os); palec w bloku akcji wyzej musi, dlatego tam bez +1.
  const int kx = M + 1;
  r.fillRoundRect(kx, 478, 20, 20, 10, p.ink);
  r.fillRoundRect(kx + 5, 483, 10, 10, 5, p.bg);
  r.fillRect(kx + 18, 486, 34, 4, p.ink);
  r.fillRect(kx + 40, 490, 4, 8, p.ink);
  r.fillRect(kx + 48, 490, 4, 8, p.ink);
  r.text(88, 474, "Klucz API: Pstryk > Konto > Urzadzenia i integracje", p.dark, 1);

  // --- stopka: wersja; przy niskiej baterii cicha zapowiedz glosem portalu ---
  r.text(20, r.height() - 24, "v" FIRMWARE_VERSION, r.rgb(120, 120, 120), 1);
  if (batteryPct >= 0 && batteryPct < 20) {
    const char* chg = "Podlacz mnie do ladowarki";
    r.text(W - M - r.textWidth(chg, 1), r.height() - 24, chg, p.dark, 1);
  }
  r.present();
}

}  // namespace pstryk
