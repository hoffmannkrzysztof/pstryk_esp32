# Konfiguracja wstępna po polsku + wskazanie przycisku — plan implementacji

> **Dla agentów:** plan jest wykonywany przez workflow (fan-out): tor „portal"
> i tor „ekran" biegną **równolegle na tym samym drzewie roboczym** i dotykają
> rozłącznych plików (mapa niżej). Reguła bezpieczeństwa buildów: dwa procesy
> `pio run` na **tym samym** envie jednocześnie psują `.pio/build` — tor portal
> kompiluje **wyłącznie** `tdisplay_long`, tor ekran **wyłącznie**
> `t5_epaper_s3`; pełny build wszystkich envów robi jeden agent w fazie
> weryfikacji, sekwencyjnie. Nie commitować — commit robi orkiestrator na końcu.

**Cel:** captive portal `Pstryk-Setup` w całości po polsku i przyjazny, z
wymaganym kluczem API przy pierwszej konfiguracji i bez pułapki „wiszącego"
portalu przy rekonfiguracji; ekran powitalny e-papieru mówi „Naciśnij i puść
pierwszy przycisk od lewej" z piktogramem rzędu przycisków, a firmware honoruje
krótkie naciśnięcie zawsze, gdy konfiguracja jest niekompletna.

**Spec:** `docs/superpowers/specs/2026-09-02-setup-ux-polish-portal-and-button-cue-design.md`
(treści, geometria i logika są tam wiążące; ten plan mówi *jak* i *w jakiej kolejności*).

**Tech:** PlatformIO (pioarduino ESP32-S3), WiFiManager 2.0.17 (przypięty),
Unity (native), Chrome headless (`/Applications/Google Chrome.app/Contents/MacOS/Google Chrome
--headless=new --disable-gpu --hide-scrollbars --window-size=W,H --screenshot=out.png file://…`),
`tools/epd_textwidth.py` (nowe, już w repo).

**Stan wyjściowy (już zrobione przed workflow):** `platformio.ini` ma
`-DWM_STRINGS_FILE=\"net/wm_strings_pl.h\"` w obu envach płytek;
`src/net/wm_strings_pl.h` to jeszcze **kopia angielskiego pliku** ze zmienionym
guardem, `lang='pl'`, `pl-PL` i `S_brand="Pstryk"` — build `t5_epaper_s3`
przechodzi, `firmware.bin` zawiera `html lang='pl'`.

---

## Mapa plików (rozłączność torów jest wiążąca)

| Plik | Tor | Akcja | Odpowiedzialność |
|---|---|---|---|
| `src/net/wm_strings_pl.h` | portal | Rewrite | wszystkie stringi/szablony WiFiManagera po polsku + CSS + stałe `PSTRYK_*` |
| `src/net/WiFiProvisioner.cpp` | portal | Modify | menu/tytuł/intro, parametry, callback „tylko klucz", trim, dołączenie po portalu |
| `tools/portal_preview.py` | portal | Create | offline’owe odtworzenie stron portalu z pliku stringów → HTML do zrzutów |
| `src/render/EpdDashboard.cpp` | ekran | Modify | blok akcji `drawWelcomeScreen` (piktogram + 2 linie head + 1 body) |
| `src/app/SleepCycle.cpp` | ekran | Modify | `buttonWake` liczone wcześniej; `human = held \|\| buttonWake` steruje pulą |
| `docs/mockups/epaper-welcome.html` | ekran | Modify | ta sama mapa liczb co C++ |
| `README.md`, spec 2026-08-25 (adnotacja), pamięć | orkiestrator | Modify | po workflow |

Pliki, których **nikt** nie rusza w workflow: `platformio.ini`, `WiFiProvisioner.h`,
`EpdDashboard.h`, `App.cpp`, `Settings.*`, testy natywne, `.github/`.

---

## Tor PORTAL

### Task P1: `src/net/wm_strings_pl.h` — tłumaczenie + styl + stałe własne

- [ ] Przepisać plik zgodnie ze spec A.1–A.2. Zachować **każdy** symbol
      z `.pio/libdeps/t5_epaper_s3/WiFiManager/wm_strings_en.h` (kontrola:
      `diff <(grep -o "^const char [A-Za-z_0-9]*" en) <(grep -o "^const char [A-Za-z_0-9]*" pl)`
      → dozwolone tylko **dodatkowe** symbole `PSTRYK_*` po stronie pl),
      tablicę `HTTP_PORTAL_MENU[]` (10 wpisów, ta sama kolejność), tokeny
      w tych samych miejscach, `#include "wm_consts_en.h"`, sekcje `#ifdef ESP32`
      / `#ifdef WM_JSTEST` / `#ifndef WM_NOHELP` (HTTP_HELP = "" w obu gałęziach).
- [ ] `HTTP_HEAD_START`: `lang='pl'`, `<meta charset='UTF-8'>` zostaje pierwsze
      po `<head>`; `HTTP_HEAD_END`: rama z paskiem `Pstryk` + `<div class='wrap'>`
      (uwaga: token `{c}` klasy body zostaje); `HTTP_ROOT_MAIN = ""`.
- [ ] `HTTP_FORM_WIFI`: nagłówek sekcji + podpowiedź + `Nazwa sieci` / `Hasło Wi-Fi`
      / `Pokaż hasło` (id/name `s`, `p`, `showpass`, `onclick='f()'` bez zmian;
      `placeholder='{v}'` i `placeholder='{p}'` zostają).
      `HTTP_FORM_PARAM_HEAD`: `<h2 class='sec'>Klucz API Pstryk</h2>`.
      `HTTP_FORM_PARAM`: bez `<br/>`, **z** końcowym `\n`; `HTTP_FORM_LABEL`
      bez zmian. `HTTP_FORM_END`: `Zapisz i połącz`. `HTTP_SCAN_LINK`:
      `Odśwież listę sieci`. `HTTP_ITEM`: cały wiersz klikalny (link `c(this)`,
      `data-ssid='{V}'`, `{qi}{qp}` po prawej).
- [ ] Komunikaty: `HTTP_SAVED`, `HTTP_PARAMSAVED`, `HTTP_STATUS_*`, `S_nonetworks`,
      tytuły `S_title*`, `S_closing/S_exiting/S_resetting/S_error/S_notfound`,
      `HTTP_ERASEBTN/HTTP_UPDATEBTN/HTTP_BACKBTN`, `HTTP_UPDATE*`, `HTTP_INFO_*`,
      `S_y/S_n/S_enable/S_disable/S_NA`, statyczne IP — po polsku (spec A.2).
      **Nie tłumaczyć:** `S_parampre`, `S_GET`, `S_POST`, `S_debugPrefix`, `D_HR`,
      `S_ssidpre`, `S_passph`, klucze debug `S_uri/S_method/S_args`.
- [ ] `HTTP_STYLE`: nowy CSS wg spec A.2 „Styl"; sprite’y ikon `.q` (dwa `data:image/png;base64`)
      przenieść **dosłownie**, wraz z klasami `q-0…q-4`, `.l`, `.h`, `.ql`.
- [ ] Stałe własne (`PSTRYK_HTML_INTRO_FIRST`, `_KEYMISSING`, `_RECONF` z `{v}`,
      `PSTRYK_HTML_KEY_HINT_FIRST`, `_RECONF`, `PSTRYK_ATTR_KEY_FIRST`, `_RECONF`,
      `PSTRYK_HTML_HEAD_FIRST`) — treści ze spec A.2; atrybuty **bez** apostrofów
      wewnątrz wartości.
- [ ] Sanity: plik jest poprawnym C (parzyste cudzysłowy, każdy literał kończy
      `;`), brak `{`/`}` w tekstach poza tokenami (nie ma parsera, ale `{v}` w
      niewłaściwym miejscu zostanie zastąpione).

### Task P2: `src/net/WiFiProvisioner.cpp` — przebieg portalu

- [ ] Implementacja dokładnie wg pseudokodu spec A.3. Szczegóły biblioteki
      (zweryfikowane): `wm.server` publiczny (`std::unique_ptr<WM_WebServer>`),
      `wm.stopConfigPortal()` przy portalu blokującym ustawia `abort` →
      `startConfigPortal` zwraca `false` po `shutdownConfigPortal()`;
      `setSaveParamsCallback(std::function<void()>)` woła się w `doParamSave`
      z `handleWifiSave` (po ustawieniu `_ssid/_pass` z argumentów); menu:
      `const char* menu[] = {"custom","wifi"}; wm.setMenu(menu, 2);`;
      `setCustomMenuHTML(const char*)` i `setCustomHeadElement(const char*)`
      przechowują **wskaźnik** — bufory (`String intro`) muszą żyć do końca
      `startConfigPortal`. Parametry dodawać **raz** (obiekty na stosie funkcji,
      poza pętlami).
- [ ] Komentarze: po polsku, w stylu pliku (dlaczego, nie co). Zaktualizować
      komentarz o „puste = bez zmian" (nadal prawdziwy dla klucza; nowy tryb
      „tylko klucz" i „nic nie zmieniam" opisać).
- [ ] Nie zmieniać: `portalSsid()`, `portalPassword()`, ścieżki bezobsługowej,
      `forgetAp/rememberAp`, timeoutów (600 s / 20 s).

### Task P3: `tools/portal_preview.py` — podgląd offline

- [ ] Parsuje `src/net/wm_strings_pl.h` (konkatenacja sąsiadujących literałów
      C, `\"`, `\n`, `\\`), składa strony **w kolejności WiFiManagera**
      (`getHTTPHead` = HEAD_START(`{v}`=tytuł) + SCRIPT + STYLE + custom head +
      HEAD_END; root = ROOT_MAIN + custom menu + `HTTP_PORTAL_MENU[0]` + status
      + END; wifi = 3 udawane wiersze `HTTP_ITEM` (z `HTTP_ITEM_QI`) +
      FORM_START(`{v}`=wifisave) + FORM_WIFI(`{v}`=SSID lub "", `{p}`="********"
      lub "") + FORM_PARAM_HEAD + [hint HTML] + LABEL+PARAM dla `apikey`
      (`{i}`=apikey, `{n}`=apikey, `{l}`=80, `{v}`="", `{c}`=atrybuty) + FORM_END
      + SCAN_LINK + status + END; saved = HEAD + HTTP_SAVED + END).
      Stany: `root-first`, `root-keymissing`, `root-reconf`, `root-failed`
      (status `HTTP_STATUS_OFF` + `HTTP_STATUS_OFFPW`), `wifi-first`,
      `wifi-reconf`, `saved`, `paramsaved`. Wyjście: `<outdir>/<stan>.html`.
- [ ] Zrzuty: Chrome headless `--window-size=390,844` dla każdego stanu do
      `<outdir>/<stan>.png`; obejrzeć (Read PNG) i poprawić styl/teksty, aż
      strony wyglądają jak produkt: czytelne, jednoznaczne, po polsku.

### Task P4: kompilacja toru portal

- [ ] `pio run -e tdisplay_long` (**tylko ten env**; pierwszy build ~5 min) →
      SUCCESS; `strings .pio/build/tdisplay_long/firmware.bin | grep -c "html lang='pl'"` ≥ 1.

---

## Tor EKRAN

### Task S1: piktogram — 3 warianty → sędzia (faza workflow, przed S2)

- [ ] Każdy wariant: kopia `docs/mockups/epaper-welcome.html` do scratchpadu,
      podmiana bloku akcji na geometrię ze spec B.1 (ramka piktogramu
      420..548 × 252..352, head 564/252 i 564/300, body 420/386), własny
      piktogram wg kryteriów B.1; zrzut 2× (`--window-size=2016,1176`) + crop
      bloku (np. `python3 -c` z PIL? — jeśli brak PIL, zrzut całości wystarczy);
      zwrot: lista wywołań makiety (`rrect/rect/dline`) z komentarzem
      „co to jest", ścieżka PNG, 2 zdania uzasadnienia.
- [ ] Sędzia ogląda 3 PNG (Read), ocenia wg kryteriów B.1, wybiera zwycięzcę,
      zapisuje drobne poprawki (np. grubość linii) — zwrot: `winner`, ops, uwagi.

### Task S2: `drawWelcomeScreen` + makieta

- [ ] Port zwycięskiego piktogramu do C++ w `EpdDashboard.cpp`: `rrect→fillRoundRect`
      (koło: 2r+1 — bazę x/y przesunąć o +1 jak przy ikonie klucza), `rect(...,1)→fillRect`,
      `rect(...,0)→drawRect`, `dline→drawLine` (na panelu 1 px; w makiecie 2 px).
- [ ] Teksty: `r.text(564, 252, "Nacisnij i pusc", p.ink, 2)`,
      `r.text(564, 300, "1. przycisk od lewej", p.ink, 2)`,
      `r.text(cx, 386, "Za chwile pokaze ustawienia.", p.dark, 1)`; usunąć
      `drawDigit(... SEG[3])`, `sekundy`, stary piktogram i stare komentarze
      o 3 sekundach; nowe komentarze: dlaczego rząd trzech, dlaczego bez
      krawędzi obudowy, dlaczego linia o opóźnieniu.
- [ ] `docs/mockups/epaper-welcome.html`: ta sama mapa liczb; komentarz nagłówka
      makiety bez zmian; zrzut po zmianie obejrzany.
- [ ] `python3 tools/epd_textwidth.py --font head --x 564 "Nacisnij i pusc" "1. przycisk od lewej"`
      i `--font body --x 420 "Za chwile pokaze ustawienia."` — brak `POZA ASCII`,
      dodatni margines.

### Task S3: `SleepCycle.cpp` — krótkie naciśnięcie

- [ ] Wg spec B.2: `buttonWake` liczone zaraz po `buttonHeld(3000)`, `human`,
      pula (`g_autoPortalOpens`) sterowana `human`; park baterii nadal `!held`;
      usunąć późniejszą deklarację `bool buttonWake = …` w kroku 3.5 (użyć
      wcześniejszej). Komentarze przy `g_autoPortalOpens` i w kroku 3 (linie
      ~356, ~370, ~389-400, ~417) uaktualnić — mówią dziś o „przytrzymaniu".

### Task S4: kompilacja toru ekran

- [ ] `pio run -e t5_epaper_s3` (**tylko ten env**) → SUCCESS.

---

## Faza WERYFIKACJA (po obu torach)

### Task V1: buildy i testy (jeden agent, sekwencyjnie)

- [ ] `pio test -e native` → wszystkie testy PASS (dziś 92).
- [ ] `pio run -e t5_epaper_s3 -e t5_epaper_s3_bootstrap -e tdisplay_long -e tdisplay_long_bootstrap`
      → 4× SUCCESS; `grep -c "html lang='pl'"` w obu `firmware.bin` ≥ 1,
      `grep -c "html lang='en'"` = 0.
- [ ] Zestaw symboli pl ⊇ en (komenda z P1); `HTTP_PORTAL_MENU` ma 10 wpisów.
- [ ] `tools/portal_preview.py` + zrzuty 390×844 wszystkich stanów;
      zrzut makiety ekranu powitalnego 2×. Raport: ścieżki PNG.

### Task V2: recenzje kontradyktoryjne (równolegle, tylko odczyt)

- [ ] R-portal-logika: `WiFiProvisioner.cpp` vs niezmienniki spec A.3
      (pierwszy raz bez klucza → `false`; rekonfiguracja puste Wi-Fi + klucz →
      zapis + dołączenie do starej sieci; puste wszystko → „anuluj"; literówka
      w haśle → portal zostaje; `String` żyje do końca portalu; parametry poza
      pętlami; `trim`).
- [ ] R-portal-html: `wm_strings_pl.h` — kompletność symboli, tokeny na
      miejscu, HTML domknięty, brak apostrofów w atrybutach `{c}`, polszczyzna
      (ortografia, spójność „Ty"), `required` faktycznie w `<input>` przy
      pierwszym uruchomieniu (na zrzucie i w HTML), CSS bez zasobów zewnętrznych.
- [ ] R-ekran: `EpdDashboard.cpp`/makieta — ASCII, marginesy (`epd_textwidth`),
      kwirk koła, zgodność liczb makieta↔C++, komentarze bez „3 sekund";
      `SleepCycle.cpp` — semantyka puli, park baterii, brak podwójnej deklaracji,
      komentarze zgodne z kodem.

### Task V3: poprawki

- [ ] Każde potwierdzone znalezisko → poprawka (jeden agent na wszystkie,
      sekwencyjnie; te same reguły envów) → ponowny V1 (buildy + testy).

---

## Po workflow (orkiestrator)

- [ ] README: „Pierwsze uruchomienie" (portal po polsku, klucz wymagany, tryb
      „tylko klucz", gest e-papieru: naciśnij i puść pierwszy przycisk od lewej;
      długie trzymanie nadal działa i jest gestem rekonfiguracji skonfigurowanego
      urządzenia), lista funkcji.
- [ ] Spec 2026-08-25: sekcja „Zmiany 2026-09-02" (blok akcji zastąpiony, pozycja
      przycisku znana).
- [ ] Pamięć projektu: pozycja przycisku, mechanizm `WM_STRINGS_FILE`, narzędzia.
- [ ] Commit(y) konwencją repo (`feat(portal): …`, `feat(epaper): …`, `docs: …`).
