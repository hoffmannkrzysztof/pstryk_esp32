# E-paper Setup Screen Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dedykowany ekran konfiguracji portalu na T5 e-paper: duże 7-segmentowe hasło, kod QR do sieci Wi-Fi, kroki 1-2-3 i prośba o podłączenie ładowarki.

**Architecture:** Nowa funkcja `drawSetupScreen()` w `EpdDashboard.cpp` (reużywa istniejący 7-seg renderer i paletę), payload QR budowany w testowalnym natywnie `core/SetupQr`, SSID wyekstrahowany do `WiFiProvisioner::portalSsid()`. Dwa miejsca wywołania w `SleepCycle.cpp` przez wrapper `setupScreen()` powtarzający rytuał zatrzasków `msgScreen()`.

**Tech Stack:** PlatformIO (pioarduino ESP32-S3), Unity (testy natywne), `ricmoo/QRCode@0.0.1`, vendored LilyGo EPD47.

**Spec:** `docs/superpowers/specs/2026-08-24-epaper-setup-screen-design.md`

**Zakres:** wyłącznie env `t5_epaper_s3` (+ `_bootstrap`). `tdisplay_long` i `drawMessage()` bez zmian funkcjonalnych; `WiFiProvisioner` jest współdzielony, więc oba envy muszą się budować.

---

## Mapa plików

| Plik | Akcja | Odpowiedzialność |
|---|---|---|
| `src/core/SetupQr.h` / `.cpp` | Create | payload `WIFI:T:WPA;S:…;P:…;;` (czysta funkcja, native) |
| `test/test_setup_qr/test_setup_qr.cpp` | Create | testy formatu payloadu |
| `src/net/WiFiProvisioner.h` / `.cpp` | Modify | `portalSsid()` — jedno źródło prawdy SSID |
| `docs/mockups/epaper-setup.html` | Create | makieta 960×540, wiążące współrzędne layoutu |
| `src/render/EpdDashboard.h` / `.cpp` | Modify | `SetupInfo` + `drawSetupScreen()` |
| `platformio.ini` | Modify | `ricmoo/QRCode@0.0.1` w `[env:t5_epaper_s3]` |
| `src/app/SleepCycle.cpp` | Modify | wrapper `setupScreen()` + 2 miejsca wywołania + odczyt baterii w bootstrapie |

Współrzędne layoutu (wiążące, jedna prawda dla makiety i firmware):

```
Header:   tytul body y=10 x=24; badge baterii: ikona 36x18 y=16 + nub, tekst po prawej; kreska y=52
Krok 1:   body y=64  "1. Polacz telefon z siecia:"
SSID:     head y=100 x=48  (UiSansHead, 45 px)
Krok 2:   body y=160 "2. Wpisz haslo:"
Haslo:    ramka drawRect(48,196,500,116); cyfry 7-seg x=72 y=214 h=80 (8*46+7*12=452 px)
Krok 3:   body y=336 "3. Otworz w przegladarce: 192.168.4.1"
QR:       v3 = 29 modulow, modul 8 px -> 232x232 @ (660,90); BEZ ramki (biale tlo = quiet zone >= 32 px)
Podpis:   body "Zeskanuj telefonem" wysrodkowany pod QR, y=360
Kreska 2: y=400
Ladowarka: ikona baterii 44x24 @ (24,426); head "Podlacz mnie do ladowarki" x=84 y=416;
           body "Rozdaje tu Wi-Fi i szybko sie rozladowuje." x=84 y=468
Wersja:   body szara (20, 516) — jak drawMessage/drawDashboard
```

---

### Task 1: `buildWifiQrPayload` (core, TDD)

**Files:**
- Create: `src/core/SetupQr.h`
- Create: `src/core/SetupQr.cpp`
- Test: `test/test_setup_qr/test_setup_qr.cpp`

Payload wg formatu Wi-Fi Network config czytanego natywnie przez aparat iOS/Androida. Escapowanie świadomie pominięte (SSID to stała bez znaków specjalnych, PSK to cyfry — patrz spec); pilnuje tego test dokładnego formatu.

- [ ] **Step 1: Napisz failujący test**

`test/test_setup_qr/test_setup_qr.cpp` (wzór struktury: `test/test_battery/test_battery.cpp`):

```cpp
#include <unity.h>
#include <cstring>
#include "core/SetupQr.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_payload_exact_format() {
  char out[64];
  TEST_ASSERT_TRUE(buildWifiQrPayload("Pstryk-Setup", "41850372", out, sizeof(out)));
  // Dokladny format, z terminatorem ";;" — od tego zalezy skanowalnosc.
  TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Pstryk-Setup;P:41850372;;", out);
}

void test_payload_fits_qr_v3_medium() {
  // Wersja 3 / ECC M miesci 42 bajty w trybie bajtowym; 12-znakowy SSID
  // + 8-cyfrowy PSK musza sie miescic, inaczej ekran traci QR.
  char out[64];
  TEST_ASSERT_TRUE(buildWifiQrPayload("Pstryk-Setup", "12345678", out, sizeof(out)));
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(42u, (uint32_t)std::strlen(out));
}

void test_small_buffer_fails_with_empty_string() {
  // Za maly bufor -> false i pusty string, nigdy ucieta (skanowalna!) resztka.
  char out[16];
  std::memset(out, 'X', sizeof(out));
  TEST_ASSERT_FALSE(buildWifiQrPayload("Pstryk-Setup", "41850372", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_zero_sized_buffer_fails() {
  TEST_ASSERT_FALSE(buildWifiQrPayload("a", "b", nullptr, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_payload_exact_format);
  RUN_TEST(test_payload_fits_qr_v3_medium);
  RUN_TEST(test_small_buffer_fails_with_empty_string);
  RUN_TEST(test_zero_sized_buffer_fails);
  return UNITY_END();
}
```

- [ ] **Step 2: Uruchom test — ma failować**

Run: `pio test -e native -f test_setup_qr`
Expected: **błąd kompilacji** `core/SetupQr.h: No such file or directory` (nowy moduł — to jest etap "red").

- [ ] **Step 3: Minimalna implementacja**

`src/core/SetupQr.h`:

```cpp
#pragma once
#include <cstddef>

namespace pstryk {

// Buduje payload "WIFI:T:WPA;S:<ssid>;P:<psk>;;" do out (rozmiar n).
// Zwraca false gdy sie nie miesci; out jest wtedy pustym stringiem.
// Escapowanie znakow specjalnych WIFI: swiadomie pominiete -- SSID portalu to
// stala bez nich, a PSK to wylacznie cyfry (WiFiProvisioner::portalPassword).
bool buildWifiQrPayload(const char* ssid, const char* psk, char* out, size_t n);

}  // namespace pstryk
```

`src/core/SetupQr.cpp`:

```cpp
#include "core/SetupQr.h"
#include <cstdio>

namespace pstryk {

bool buildWifiQrPayload(const char* ssid, const char* psk, char* out, size_t n) {
  int need = std::snprintf(out, n, "WIFI:T:WPA;S:%s;P:%s;;", ssid, psk);
  if (need < 0 || (size_t)need >= n) {
    if (n) out[0] = '\0';   // nigdy nie zostawiaj ucietego payloadu
    return false;
  }
  return true;
}

}  // namespace pstryk
```

(`env:native` ma `build_src_filter = +<core/> +<view/>` i `test_build_src = yes`, więc nowy plik w `core/` wchodzi do buildu testów automatycznie.)

- [ ] **Step 4: Uruchom testy — mają przechodzić**

Run: `pio test -e native -f test_setup_qr`
Expected: `4 Tests 0 Failures 0 Ignored` → PASSED

Run: `pio test -e native`
Expected: wszystkie dotychczasowe suity (test_battery, test_format, …) nadal PASSED

- [ ] **Step 5: Commit**

```bash
git add src/core/SetupQr.h src/core/SetupQr.cpp test/test_setup_qr/test_setup_qr.cpp
git commit -m "feat(core): Wi-Fi QR payload builder for the setup screen"
```

---

### Task 2: `WiFiProvisioner::portalSsid()`

**Files:**
- Modify: `src/net/WiFiProvisioner.h` (po deklaracji `portalPassword()`, ~linia 28)
- Modify: `src/net/WiFiProvisioner.cpp` (implementacja przy `portalPassword()` ~linia 31; użycie w `startConfigPortal` ~linia 95)

Dziś `"Pstryk-Setup"` jest literałem w `startConfigPortal(...)` i osobno w sześciu `snprintf` (4× App.cpp, 2× SleepCycle.cpp). QR pokazujący inny SSID niż rozgłaszany byłby zepsuty niewidocznie — stąd jedno źródło prawdy. Miejsca w `App.cpp` (T-Display-Long) zostają nietknięte — poza zakresem; te w `SleepCycle.cpp` znikną w Task 5.

Brak testu natywnego: plik jest Arduino-only (WiFi.h), a funkcja zwraca stałą — weryfikacją jest build obu envów, które go kompilują.

- [ ] **Step 1: Deklaracja w nagłówku**

W `src/net/WiFiProvisioner.h`, bezpośrednio po deklaracji `portalPassword()`:

```cpp
  // SSID rozglaszany przez portal. Jedno zrodlo prawdy dla startConfigPortal,
  // ekranu konfiguracji i kodu QR -- QR z innym SSID niz rozglaszany bylby
  // zepsuty w sposob niewidoczny na ekranie.
  static const char* portalSsid();
```

- [ ] **Step 2: Implementacja + użycie**

W `src/net/WiFiProvisioner.cpp`, przed `portalPassword()`:

```cpp
const char* WiFiProvisioner::portalSsid() { return "Pstryk-Setup"; }
```

Zamień (linia ~95):

```cpp
  bool ok = wm.startConfigPortal("Pstryk-Setup", portalPassword());
```

na:

```cpp
  bool ok = wm.startConfigPortal(portalSsid(), portalPassword());
```

- [ ] **Step 3: Zbuduj oba envy kompilujące ten plik**

Run: `pio run -e t5_epaper_s3 && pio run -e tdisplay_long`
Expected: oba `SUCCESS`

- [ ] **Step 4: Commit**

```bash
git add src/net/WiFiProvisioner.h src/net/WiFiProvisioner.cpp
git commit -m "refactor(net): extract portalSsid() as the single source of the AP name"
```

---

### Task 3: Makieta HTML

**Files:**
- Create: `docs/mockups/epaper-setup.html`

Konwencje jak w `docs/mockups/epaper.html`: natywne 960×540, `zoom: 2`, Fira Sans, stałe `LB=30/LH=45/FB=21/FH=32`, ink `#000`. QR jako deterministyczny placeholder z finder patterns (prawdziwy QR w przeglądarce to zbędna zależność — makieta domyka layout, nie skanowalność).

- [ ] **Step 1: Napisz makietę**

```html
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="utf-8">
<title>Pstryk — ekran konfiguracji e-paper (makieta)</title>
<style>
  /* Rendered at 2x for crisp screenshots. Coordinates are in the panel's native
     960x540 pixel space, matching drawSetupScreen() in src/render/EpdDashboard.cpp. */
  html, body { margin: 0; padding: 0; background: #d8dade; }
  body { zoom: 2; }
  .frame { padding: 24px; }
  .screen { position: relative; width: 960px; height: 540px; background: #ffffff;
            font-family: "Fira Sans", -apple-system, "Segoe UI", Helvetica, sans-serif;
            box-shadow: 0 4px 18px #0006; overflow: hidden; }
  .t { position: absolute; white-space: nowrap; color: #000; line-height: 1; }
  .seg { position: absolute; background: #000; }
  .box { position: absolute; box-sizing: border-box; border: 2px solid #000; }
  .ln { position: absolute; background: #969696; height: 1px; }
  .qrm { position: absolute; background: #000; }
</style>
</head>
<body>
<div class="frame">
  <div class="screen" id="epd"></div>
</div>
<script>
const C = { ink:'#000', mid:'#969696' };
const W = 960, M = 24;
const FB = 21, FH = 32;                 // body/head font px (same as epaper.html)

const SSID = 'Pstryk-Setup';
const PSK  = '41850372';
const URL  = '192.168.4.1';
const BAT  = 62;                        // ustaw -1 zeby zobaczyc wariant "BAT ?"

const scr = document.getElementById('epd');
function el(cls){ const d=document.createElement('div'); d.className=cls; scr.appendChild(d); return d; }
function text(x,y,s,size,color){
  const t=el('t'); t.textContent=s; t.style.left=x+'px'; t.style.top=y+'px';
  t.style.fontSize=(size>=2?FH:FB)+'px'; t.style.color=color||C.ink;
  if(size>=2) t.style.fontWeight='600';
  return t;
}
function textW(s,size){ const t=text(-9999,-9999,s,size); const w=t.getBoundingClientRect().width/2; t.remove(); return w; }
function rect(x,y,w,h,fill){ const d=el(fill?'seg':'box'); d.style.left=x+'px'; d.style.top=y+'px';
  d.style.width=w+'px'; d.style.height=h+'px'; return d; }
function hline(x,y,w){ const d=el('ln'); d.style.left=x+'px'; d.style.top=y+'px'; d.style.width=w+'px'; }

// 7-seg jak drawDigit() w EpdDashboard.cpp: h=80 -> w=46, t=10, gap=12.
const SEG=[0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F];
function segDigit(x,y,w,h,t,m){
  if(m&0x01) rect(x,y,w,t,1);
  if(m&0x02) rect(x+w-t,y,t,h/2,1);
  if(m&0x04) rect(x+w-t,y+h/2,t,h/2,1);
  if(m&0x08) rect(x,y+h-t,w,t,1);
  if(m&0x10) rect(x,y+h/2,t,h/2,1);
  if(m&0x20) rect(x,y,t,h/2,1);
  if(m&0x40) rect(x,y+(h-t)/2,w,t,1);
}
function bigDigits(x,y,h,s){
  const w=Math.floor(h*0.58), t=Math.floor(h*0.13), gap=Math.floor(h*0.16);
  let cx=x;
  for(const ch of s){ segDigit(cx,y,w,h,t,SEG[+ch]); cx+=w+gap; }
}
// Ikona baterii: obrys + nub + wypelnienie proporcjonalne (pct<0 = pusta).
function batteryIcon(x,y,w,h,pct){
  rect(x,y,w,h,0);
  rect(x+w, y+h/2-h/6, 4, h/3, 1);
  if(pct>=0){ const inner=Math.floor((w-6)*pct/100); if(inner>0) rect(x+3,y+3,inner,h-6,1); }
}
// Placeholder QR: finder patterns + deterministyczny szum (LCG), 29x29 po 8 px.
function qrPlaceholder(qx,qy,mod){
  const N=29; let seed=12345;
  const rnd=()=>((seed=(seed*1103515245+12345)&0x7fffffff)/0x7fffffff);
  const finder=(fx,fy)=>{ for(let j=0;j<7;j++)for(let i=0;i<7;i++){
    const ring=(i==0||i==6||j==0||j==6), core=(i>=2&&i<=4&&j>=2&&j<=4);
    if(ring||core) rect(qx+(fx+i)*mod, qy+(fy+j)*mod, mod, mod, 1).className='qrm';
  }};
  for(let j=0;j<N;j++)for(let i=0;i<N;i++){
    const inF=(i<9&&j<9)||(i>=N-9&&j<9)||(i<9&&j>=N-9);
    if(!inF && rnd()<0.45) rect(qx+i*mod, qy+j*mod, mod, mod, 1).className='qrm';
  }
  finder(0,0); finder(N-7,0); finder(0,N-7);
}

// --- header ---
text(M, 10, 'Pstryk - Konfiguracja', 1);
const bat = BAT>=0 ? `BAT ${BAT} %` : 'BAT ?';
const batW = textW(bat,1), batX = W-M-batW;
text(batX, 10, bat, 1);
batteryIcon(batX-40-12, 16, 36, 18, BAT);
hline(M, 52, W-2*M);

// --- lewa kolumna ---
text(M, 64, '1. Polacz telefon z siecia:', 1);
text(48, 100, SSID, 2);
text(M, 160, '2. Wpisz haslo:', 1);
rect(48, 196, 500, 116, 0);
bigDigits(72, 214, 80, PSK);
text(M, 336, `3. Otworz w przegladarce: ${URL}`, 1);

// --- QR ---
qrPlaceholder(660, 90, 8);
const cap='Zeskanuj telefonem', cw=textW(cap,1);
text(660+(232-cw)/2, 360, cap, 1);

// --- ladowarka ---
hline(M, 400, W-2*M);
batteryIcon(M, 426, 44, 24, BAT);
text(84, 416, 'Podlacz mnie do ladowarki', 2);
text(84, 468, 'Rozdaje tu Wi-Fi i szybko sie rozladowuje.', 1);

text(20, 516, 'v1.4.0', 1, '#787878');
</script>
</body>
</html>
```

- [ ] **Step 2: Obejrzyj w przeglądarce**

Run: `open docs/mockups/epaper-setup.html`
Expected: układ zgodny z mapą współrzędnych z nagłówka planu; nic nie nachodzi, podpis QR mieści się w ekranie. Sprawdź też wariant `BAT = -1` (edytując stałą): ikony puste, tekst `BAT ?`.

- [ ] **Step 3: Commit**

```bash
git add docs/mockups/epaper-setup.html
git commit -m "docs(mockups): setup-screen mockup at native e-paper resolution"
```

---

### Task 4: `drawSetupScreen()` + zależność QRCode

**Files:**
- Modify: `platformio.ini` (`[env:t5_epaper_s3]` → `lib_deps`)
- Modify: `src/render/EpdDashboard.h`
- Modify: `src/render/EpdDashboard.cpp`

Bez testu natywnego — plik renderuje na sprzęt i nie wchodzi do `env:native` (`build_src_filter = +<core/> +<view/>`). Weryfikacja: build. `qrcode.h` włączany tylko w `EpdDashboard.cpp`, którego `tdisplay_long` nie kompiluje (`-<render/EpdDashboard.cpp>` w jego filtrze), więc LDF nie wciągnie biblioteki do tamtego envu.

- [ ] **Step 1: Przypnij bibliotekę**

W `platformio.ini`, sekcja `[env:t5_epaper_s3]`, dopisz do `lib_deps`:

```ini
lib_deps =
    bblanchon/ArduinoJson@7.4.3
    tzapu/WiFiManager@2.0.17
    ricmoo/QRCode@0.0.1
```

(Pin dokładny — zasada opisana w komentarzach `platformio.ini`. `0.0.1` to jedyna wersja w rejestrze. `env:t5_epaper_s3_bootstrap` dziedziczy przez `extends`, niczego nie dopisujemy.)

- [ ] **Step 2: Rozszerz nagłówek**

W `src/render/EpdDashboard.h`, po deklaracji `drawDashboard(...)`:

```cpp
struct SetupInfo {
  const char* ssid;        // rozglaszany SSID portalu (WiFiProvisioner::portalSsid)
  const char* password;    // 8 cyfr z WiFiProvisioner::portalPassword
  const char* portalUrl;   // "192.168.4.1"
  int  batteryPct;         // -1 = nieznany (jak EpdStatus::batteryPct)
};

// Pelnoekranowy ekran konfiguracji portalu: SSID, duze 7-seg haslo, kod QR
// do sieci, kroki 1-2-3 i prosba o podlaczenie ladowarki. Flushuje panel.
void drawSetupScreen(IRenderer& r, const SetupInfo& info);
```

- [ ] **Step 3: Implementacja**

W `src/render/EpdDashboard.cpp`:

Do bloku include'ów na górze dopisz:

```cpp
#include "core/SetupQr.h"      // buildWifiQrPayload
#include "qrcode.h"            // ricmoo/QRCode (naglowek sam robi extern "C")
```

Do anonimowej przestrzeni nazw (obok `drawBigPrice`, przed `}  // namespace`):

```cpp
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
```

Na końcu pliku, po `drawMessage(...)` (musi być w tym samym TU co `drawBigPrice` — anonimowa przestrzeń nazw):

```cpp
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
  char payload[64];
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
  // Kazde niepowodzenie QR (payload sie nie miesci / init < 0) zostawia strefe
  // QR pusta -- cyfry wyzej sa sciezka nadrzedna, a lewa kolumna i tak miesci
  // sie w pelnej szerokosci, wiec zaden reflow nie jest potrzebny.

  // --- prosba o ladowarke ---
  r.drawLine(M, 400, W - M, 400, p.mid);
  drawBatteryIcon(r, p, M, 426, 44, 24, info.batteryPct);
  r.text(84, 416, "Podlacz mnie do ladowarki", p.ink, 2);
  r.text(84, 468, "Rozdaje tu Wi-Fi i szybko sie rozladowuje.", p.ink, 1);

  r.text(20, r.height() - 24, "v" FIRMWARE_VERSION, r.rgb(120, 120, 120), 1);
  r.present();
}
```

- [ ] **Step 4: Build**

Run: `pio run -e t5_epaper_s3`
Expected: `SUCCESS`; w logu LDF widoczne `QRCode @ 0.0.1`

Run: `pio run -e tdisplay_long`
Expected: `SUCCESS` bez QRCode w grafie zależności (env nie kompiluje EpdDashboard.cpp)

- [ ] **Step 5: Commit**

```bash
git add platformio.ini src/render/EpdDashboard.h src/render/EpdDashboard.cpp
git commit -m "feat(epaper): dedicated setup screen with big PSK, Wi-Fi QR and charger prompt"
```

---

### Task 5: Podpięcie w `SleepCycle.cpp`

**Files:**
- Modify: `src/app/SleepCycle.cpp` (wrapper przy `msgScreen` ~linia 261; rekonfiguracja ~linia 345; bootstrap ~linia 273)

Ścieżka parkowania rozładowanej baterii (`"Bateria rozladowana"`, ~linia 334) **zostaje nietknięta** — celowo nie podnosi radia, więc nie może dostać ekranu z QR do sieci, której nie ma.

- [ ] **Step 1: Wrapper `setupScreen()`**

Bezpośrednio po funkcji `msgScreen(...)` dodaj:

```cpp
// Ten sam rytual zatrzaskow co msgScreen (to tez pelnoekranowe przemalowanie).
// requestDeepClean ma tu drugi powod poza spojnoscia: pozostalosci po
// poprzednim obrazie obnizaja kontrast modulow QR, a QR ma najmniejszy
// margines bledu ze wszystkiego, co rysujemy.
static void setupScreen(EpdRenderer& g, int batteryPct) {
  g_lastDrawSig = 0;
  g_authShown = 0;
  g_lastDeepCleanDay = 0;
  g.requestDeepClean();
  SetupInfo si;
  si.ssid = WiFiProvisioner::portalSsid();
  si.password = WiFiProvisioner::portalPassword();
  si.portalUrl = "192.168.4.1";
  si.batteryPct = batteryPct;
  drawSetupScreen(g, si);
}
```

(`render/EpdDashboard.h` i `net/WiFiProvisioner.h` są już włączone na górze pliku.)

- [ ] **Step 2: Miejsce 1 — rekonfiguracja / pierwszy boot**

Zamień (w `setup()`, ~linia 345):

```cpp
  // 3) reconfigure: button held at boot, or no usable saved config
  if (wantsPortal) {
    char pskLine[40];
    std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                  WiFiProvisioner::portalPassword());
    msgScreen(gfx_, "Konfiguracja", pskLine);
    WiFiProvisioner prov;
```

na:

```cpp
  // 3) reconfigure: button held at boot, or no usable saved config
  if (wantsPortal) {
    setupScreen(gfx_, batPct);   // batPct z odczytu w kroku 1 (przed Wi-Fi)
    WiFiProvisioner prov;
```

- [ ] **Step 3: Miejsce 2 — bootstrap (+ odczyt baterii)**

Zamień w `runBootstrap()` (~linia 275):

```cpp
  bool needPortal = !settings_.isComplete();
  if (needPortal) {
    char pskLine[40];
    std::snprintf(pskLine, sizeof(pskLine), "Pstryk-Setup  haslo: %s",
                  WiFiProvisioner::portalPassword());
    msgScreen(gfx_, "Konfiguracja", pskLine);
  } else {
```

na:

```cpp
  bool needPortal = !settings_.isComplete();
  if (needPortal) {
    // Obraz instalatora nigdy nie czytal ogniwa -- ekran konfiguracji pokazuje
    // badge baterii, wiec odczyt musi byc TUTAJ: przed portalem (ADC2 vs Wi-Fi),
    // inaczej ekran zawsze pokazywalby "BAT ?".
    bool batLow = false;
    float batVolts = 0.0f;
    int batPct = readBatteryPercent(batLow, batVolts);
    setupScreen(gfx_, batPct);
  } else {
```

- [ ] **Step 4: Build obu wariantów e-paper**

Run: `pio run -e t5_epaper_s3 && pio run -e t5_epaper_s3_bootstrap`
Expected: oba `SUCCESS` (bootstrap kompiluje gałąź `#ifdef PSTRYK_OTA_BOOTSTRAP` z nowym odczytem baterii — bez tego builda ta linia nigdy nie przejdzie przez kompilator)

- [ ] **Step 5: Commit**

```bash
git add src/app/SleepCycle.cpp
git commit -m "feat(epaper): show the setup screen on both portal paths, read battery in bootstrap"
```

---

### Task 6: Weryfikacja końcowa

**Files:** brak zmian — wyłącznie weryfikacja.

- [ ] **Step 1: Pełny zestaw buildów i testów (to samo co CI)**

Run: `pio test -e native && pio run -e t5_epaper_s3 -e tdisplay_long && pio run -e t5_epaper_s3_bootstrap`
Expected: testy PASSED, wszystkie buildy SUCCESS

- [ ] **Step 2: Smoke test na sprzęcie (ręczny — wymaga płytki i człowieka)**

1. `pio run -e t5_epaper_s3 -t upload`, przytrzymaj przycisk 3 s → ekran konfiguracji
2. QR skanuje się z ~40 cm aparatem iOS i Androida; telefon dołącza do `Pstryk-Setup`
3. Osiem cyfr czytelne z ~3 m; zgadzają się z hasłem, które przyjmuje portal
4. Badge pokazuje sensowny procent; z odłączonym ogniwem — `BAT ?` i puste ikony
5. Po zapisaniu konfiguracji pierwszy pulpit rysuje się z pełnym deghostem (bez duchów po QR)

Jeśli sprzętu nie ma pod ręką w trakcie wykonywania planu: krok jest jawnie raportowany jako niewykonany, nie pomijany po cichu.

---

## Self-review (wykonany przy pisaniu planu)

- **Pokrycie specu:** payload+testy → T1; portalSsid → T2; makieta → T3; SetupInfo/drawSetupScreen/QR/fallback/badge/ładowarka/pin biblioteki → T4; wrapper+zatrzaski+2 wywołania+bateria w bootstrapie → T5; buildy+testy+smoke → T6. Ścieżka parkowania i `drawMessage()` nietknięte (T5, T4).
- **Odstępstwo od specu (świadome):** spec mówił "lewa kolumna rozciąga się na pełną szerokość" przy braku QR — lewa kolumna i tak mieści się w pełnej szerokości, więc żaden reflow nie istnieje; strefa QR po prostu zostaje pusta. Zachowanie użytkowe identyczne, kod prostszy.
- **Spójność typów:** `SetupInfo` (T4 header) = użycie w T5; `drawBatteryIcon(r,p,x,y,w,h,pct)` spójne między nagłówkiem (36×18) a stopką (44×24); `buildWifiQrPayload` (T1) = wywołanie w T4.
