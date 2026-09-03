# Konfiguracja wstępna po polsku + wskazanie przycisku na ekranie powitalnym — projekt

Data: 2026-09-02
Zakres: (A) captive portal `Pstryk-Setup` — **obie płytki** (WiFiProvisioner jest
wspólny); (B) blok akcji ekranu powitalnego + logika otwierania portalu —
**wyłącznie** `t5_epaper_s3` (+ `_bootstrap`).
Proces: scouting inline → spec → plan → workflow (3 warianty piktogramu → sędzia;
dwa równoległe tory implementacji; weryfikacja buildów + recenzje kontradyktoryjne;
poprawki).

## Prośba użytkownika (2026-09-02) i jej odczyt

> Przy konfiguracji wstępnej tam gdzie wpisujemy WIFI i Pstryk, czy możemy ten
> ekran jakoś poprawić? Koniecznie Pstryk API musi zostać wpisany. Zrobić po
> polsku ten setup. Bardziej user-friendly. Dodatkowo na ekranie gdzie jest
> "Przytrzymaj 3 sek" to zmienić ikonkę żeby wskazywało na pierwszy przycisk po
> lewej + info przyciśnij i puść. Zrób workflow pod to.

Odczyt (założenia jawne, bo sesja bez możliwości dopytania):

1. „Ekran, gdzie wpisujemy WIFI i Pstryk" = **formularz captive portalu**
   (WiFiManager) — dziś angielski szkielet biblioteki z dwoma polskimi etykietami
   bez diakrytyków. Ma być w całości po polsku i przyjazny.
2. „Koniecznie Pstryk API musi zostać wpisany" = przy pierwszej konfiguracji pole
   klucza jest **wymagane** (blokada po stronie przeglądarki + komunikat), a przy
   rekonfiguracji puste pole nadal znaczy „bez zmian" (formularz nie zdradza
   zapisanego klucza — decyzja z audytu, zostaje).
3. „Zmienić ikonkę, żeby wskazywało na pierwszy przycisk po lewej" = na obudowie
   jest **rząd przycisków**, a właściwy jest **skrajny lewy** (rozstrzyga to
   pytanie otwarte ze spec ekranu powitalnego z 2026-08-25). Krawędź obudowy
   (góra/dół) nadal nieznana → piktogram pokazuje „pierwszy z rzędu od lewej",
   bez strzałki poza ekran.
4. „Info przyciśnij i puść" = gest ma być **krótkim naciśnięciem**, nie
   3-sekundowym trzymaniem. To zmiana zachowania firmware, nie tylko napisu —
   patrz sekcja B.2. Trzymanie 3 s nadal działa (nadzbiór).

## Problem

**Portal.** Strona główna portalu to menu WiFiManagera („Configure WiFi / Info /
Exit / Update") z nazwą hosta; formularz ma etykiety „SSID / Password / Save",
komunikaty „Saving Credentials… If it fails reconnect to AP" i nasze
„Pstryk API key (wymagany)". Dla nietechnicznego klienta to strona obca
językowo i wizualnie, bez wyjaśnienia, skąd wziąć klucz. Pole klucza nie jest
wymuszone — formularz wysłany bez klucza łączy Wi-Fi, a urządzenie ląduje na
ekranie powitalnym bez słowa, czego brakuje (jedyna obrona to tekst etykiety).
Dodatkowe pułapki rekonfiguracji: pozostawienie pustych pól Wi-Fi (co sugeruje
„puste = bez zmian") powoduje, że WiFiManager pomija zapis i portal **wisi do
timeoutu (10 min)** — zmiana samego klucza jest dziś niewykonalna bez
przepisania hasła Wi-Fi.

**Ekran powitalny.** Blok akcji mówi „Przytrzymaj przycisk 3 sekundy", piktogram
jest celowo abstrakcyjny (pozycja przycisku była nieznana), a e-papier nie daje
żadnej reakcji przez całe trzymanie. Firmware liczy jednak każde wybudzenie
**krótkim** naciśnięciem jako „automatyczne" otwarcie portalu (pula 4) — po
wyczerpaniu puli krótkie naciśnięcie tylko przemalowuje ten sam ekran, co
wygląda jak zepsuty przycisk.

## Cel

- Portal w całości po polsku (z diakrytykami — to przeglądarka, nie e-papier),
  z jednym jasnym przebiegiem: intro → lista sieci → hasło → klucz API (z
  informacją, skąd go wziąć) → „Zapisz i połącz" → co dalej.
- Klucz API wymagany przy pierwszej konfiguracji; bez pułapki „wiszącego"
  portalu przy rekonfiguracji.
- Ekran powitalny: „Naciśnij i puść pierwszy przycisk od lewej" z piktogramem
  rzędu przycisków i palcem na pierwszym; firmware honoruje krótkie naciśnięcie
  zawsze, gdy konfiguracja jest niekompletna.

## Ograniczenia (zweryfikowane w kodzie)

| Ograniczenie | Źródło | Konsekwencja |
|---|---|---|
| Teksty/szablony WiFiManagera są stałymi `PROGMEM` w `wm_strings_en.h`, dołączanym przez `#include WM_STRINGS_FILE` | `WiFiManager.h:137-141` | Tłumaczenie = własny plik stringów podany build-flagą. **Zweryfikowane**: `-DWM_STRINGS_FILE=\"net/wm_strings_pl.h\"` w obu envach kompiluje się (`-I src` działa też dla biblioteki), a `firmware.bin` zawiera `html lang='pl'`, nie `lang='en'`. |
| Plik stringów musi definiować **wszystkie** symbole z `wm_strings_en.h` (biblioteka ich używa) | `wm_strings_es.h` ma identyczny zestaw | Kontrola zestawu symboli w weryfikacji (diff nazw `const char …[]`). `S_parampre` („param_") to prefiks nazw pól formularza — **nie tłumaczyć**. |
| Strona główna = `HTTP_ROOT_MAIN` + `getMenuOut()` (kolejność z `setMenu`; token `custom` wstawia `_customMenuHTML`) + status | `WiFiManager.cpp:1329-1351, 1438-1446` | Intro po polsku zależne od stanu (pierwszy raz / brak klucza / rekonfiguracja) idzie przez `setCustomMenuHTML`, menu = `{"custom","wifi"}`. |
| Strona `/wifi` = lista skanu → `<form>` (`HTTP_FORM_WIFI`) → pola statyczne → `HTTP_FORM_PARAM_HEAD` → parametry (`getParamOut`) → `HTTP_FORM_END` → `HTTP_SCAN_LINK` → status | `WiFiManager.cpp:1358-1400` | Nagłówki sekcji i podpowiedzi statyczne w szablonach; zależne od stanu — jako parametr z samym HTML (`WiFiManagerParameter(const char* custom)`, `getID()==NULL` → wstawiany dosłownie). |
| `{c}` parametru ląduje **wewnątrz** `<input … {c}>` | `HTTP_FORM_PARAM`, `getParamOut` | Atrybuty `required`, `placeholder`, `autocomplete` itd. idą tym kanałem. |
| Puste `s` i `p` w `/wifisave` → „No ssid, skipping wifi save" → portal działa dalej do timeoutu | `handleWifiSave`, `processConfigPortal` (gałąź blokująca czyści `_ssid/_pass`, wyłącza STA) | Zmiana samego klucza wymaga własnej obsługi: `setSaveParamsCallback` + `server->arg("s")`/`arg("p")` (`server` jest publiczny) + `stopConfigPortal()` (ustawia `abort`, pętla `startConfigPortal` kończy się `false`). |
| `handleWifiSave`: puste `s` + niepuste `p` = zmiana hasła do zapisanej sieci (`_ssid = WiFi_SSID(true)`), ustawiane **przed** `doParamSave` | `WiFiManager.cpp:1815-1822` | Callback przerywa portal tylko, gdy **oba** pola Wi-Fi są puste. |
| Po nieudanym połączeniu (złe hasło) portal blokujący zostaje otwarty, `_ssid/_pass` wyczyszczone, status widoczny na stronie głównej; aktywność WWW przedłuża timeout | `processConfigPortal`, `configPortalHasTimeout` (`_webClientCheck`) | Literówka w haśle = ponowna próba bez restartu urządzenia. `setBreakAfterConfig` **nie** jest używane (zamykałoby portal po literówce). |
| Wartości parametrów przetrwają nieudane połączenie (`_params[i]->_value`) i wracają w `value='{v}'` | `doParamSave`, `getParamOut` | Po literówce w haśle Wi-Fi klucz jest już wpisany — użytkownik poprawia tylko hasło. Przy rekonfiguracji pole startuje puste (nie prefill'ujemy zapisanego klucza). |
| Fonty e-papieru: ASCII 0x20–0x7E, znak spoza zakresu **znika** | `ui_font_body.h:687`, `ui_font_head.h:1311`, `EpdRenderer.cpp:78` | Napisy ekranu bez diakrytyków (jak dotąd). `tools/epd_textwidth.py` zgłasza znaki poza zakresem. |
| Dokładne metryki fontów: head `advance_y=45, ascender=36`, body `30/24`; średnio ~17 px/znak head, ~11,5 px/znak body | `tools/epd_textwidth.py` (nowe, liczy z tablic glifów) | Poprzednia „pesymistyczna metryka" (22/15 px) była o 30–40 % za ostrożna. Współrzędne poniżej są liczone z prawdziwych szerokości. |
| Wybudzenie ext0 możliwe **wyłącznie** przez przycisk (pull-up utrzymany w deep sleep, `rtc_gpio_pullup_en`) | `SleepCycle::sleepFor` | Każde wybudzenie `ESP_SLEEP_WAKEUP_EXT0` = obecność człowieka → może odnawiać pulę otwarć portalu. |
| Bateria czytana przed Wi-Fi (ADC2) | `SleepCycle::setup()` krok 1 | Bez zmian; `batPct` jest już dostępne dla ekranu. |

## Rozwiązanie A — portal po polsku

### A.1 Mechanizm

- `src/net/wm_strings_pl.h`: pełna kopia struktury `wm_strings_en.h`
  (te same symbole, te same tokeny `{v} {p} {i} {n} {l} {c} {t} {r} {q} {h} {1} {2}`),
  po polsku, UTF-8 z diakrytykami, `lang='pl'`, `WM_LANGUAGE="pl-PL"`,
  własny `HTTP_STYLE`, `HTTP_HELP` pusty. Dodatkowo stałe **własne portalu**
  (prefiks `PSTRYK_`), żeby cały HTML portalu był w jednym pliku i jeden
  generator podglądu (`tools/portal_preview.py`) mógł go odtworzyć offline:
  `PSTRYK_HTML_INTRO_FIRST`, `PSTRYK_HTML_INTRO_KEYMISSING`,
  `PSTRYK_HTML_INTRO_RECONF` (token `{v}` = zapisany SSID),
  `PSTRYK_HTML_KEY_HINT_FIRST`, `PSTRYK_HTML_KEY_HINT_RECONF`,
  `PSTRYK_ATTR_KEY_FIRST`, `PSTRYK_ATTR_KEY_RECONF`, `PSTRYK_HTML_HEAD_FIRST`.
- `platformio.ini`: `-DWM_STRINGS_FILE=\"net/wm_strings_pl.h\"` w
  `[env:tdisplay_long]` i `[env:t5_epaper_s3]` (bootstrapy dziedziczą) oraz
  `extra_scripts = post:tools/wm_strings_dep.py`: include przez makro jest
  niewidoczny dla skanera zależności SCons, więc bez tego skryptu edycja pliku
  stringów **nie** rekompilowała `WiFiManager.cpp` i binarka cicho zostawała
  ze starym (angielskim) portalem mimo `SUCCESS` (wykryte w weryfikacji
  workflow). Skrypt dokłada jawną zależność obiektu biblioteki od pliku.
- `WiFiProvisioner::ensureConnected` (ścieżka z portalem): `setTitle("Pstryk")`,
  `setMenu({"custom","wifi"})`, `setCustomMenuHTML(intro)`,
  `setCustomHeadElement(PSTRYK_HTML_HEAD_FIRST)` tylko przy pierwszym
  uruchomieniu, dwa parametry (podpowiedź jako czysty HTML + pole `apikey`
  z atrybutami zależnymi od stanu), `setSaveParamsCallback` (tryb „tylko klucz /
  nic nie zmieniam"), przycięcie klucza (`trim()`), po portalu dołączenie do
  zapisanej sieci, gdy portal zamknął się z callbacku.

### A.2 Strony i teksty (wiążące co do treści; HTML/CSS domyka implementacja)

**Wspólna rama** (`HTTP_HEAD_END`): pasek `Pstryk` (wordmark tekstowy, bez
grafik), `<div class='wrap'>`. `HTTP_ROOT_MAIN` pusty (wordmark już jest w
ramie, nazwa hosta nikomu nic nie mówi).

**Strona główna `/`** (to, co otwiera captive portal) = intro zależne od stanu
+ jeden przycisk `Dalej` (`HTTP_PORTAL_MENU[0]`, akcja `/wifi`) + status:

- pierwsze uruchomienie (`apiKey` pusty, `ssid` pusty):
  `Cześć! Skonfigurujmy Pstryk.` + lista 3 kroków: **Wi-Fi** — wybierz swoją
  sieć i wpisz hasło; **Klucz API Pstryk** — wklej klucz z aplikacji Pstryk
  (Konto → Urządzenia i integracje); **Zapisz** — urządzenie samo połączy się
  i pobierze ceny prądu. Podpowiedź: `Zajmie to około 2 minuty. Urządzenie
  najlepiej mieć w tym czasie na ładowarce.`
- brak klucza (`apiKey` pusty, `ssid` zapisany — np. archaiczna przeglądarka
  ominęła `required`): `msg D`: `Brakuje klucza API Pstryk.` — `Bez niego
  urządzenie nie pobierze cen. Sieć Wi-Fi jest już zapisana — wystarczy wpisać
  klucz i zapisać. Chcesz zmienić sieć? Wybierz ją z listy i podaj hasło.`
  (w tym stanie działa tryb „tylko klucz" z A.3, więc formularz z pustymi
  polami Wi-Fi i kluczem domyka konfigurację).
- rekonfiguracja (oba zapisane): `Ustawienia Pstryk`, `Zapisana sieć: <b>{v}</b>.`,
  `Możesz zmienić sieć Wi-Fi, jej hasło albo klucz API. Pola, których nie
  zmieniasz, zostaw puste.`

**Strona `/wifi`**: lista sieci (każdy wiersz = cały link, ikony siły sygnału
`.q` zachowane — sprite'y base64 z oryginału), potem formularz:

- `Sieć Wi-Fi` (nagłówek sekcji) + `Wybierz sieć z listy powyżej albo wpisz jej
  nazwę.`; pola `Nazwa sieci` (placeholder `{v}` = bieżący SSID) i `Hasło Wi-Fi`
  (placeholder `{p}`), checkbox `Pokaż hasło` (skrypt `f()` bez zmian).
- `Klucz API Pstryk` (nagłówek, `HTTP_FORM_PARAM_HEAD`) + podpowiedź zależna
  od stanu: pierwszy raz — `Znajdziesz go w aplikacji Pstryk: Konto →
  Urządzenia i integracje → Wygeneruj klucz. Zaczyna się od sk-. Pole jest
  wymagane.`; rekonfiguracja — `Masz już zapisany klucz. Zostaw puste, aby go
  zachować.` Pole `Klucz API` (`maxlength 80`; pierwszy raz: `required
  autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false'
  placeholder='sk-…'`; rekonfiguracja: to samo bez `required`, placeholder
  `zostaw puste = bez zmian`).
- Przycisk `Zapisz i połącz` (`HTTP_FORM_END`), niżej `Odśwież listę sieci`
  (`HTTP_SCAN_LINK`). `S_nonetworks`: `Nie znaleziono sieci Wi-Fi. Odśwież
  listę, aby przeskanować ponownie.`
- Bez zapisanej sieci: `PSTRYK_HTML_HEAD_FIRST` = mały `<script>` ustawiający
  `required` na `#s` po `DOMContentLoaded` (bez SSID nie ma czego łączyć,
  a szablon jest statyczny; z zapisaną siecią — rekonfiguracja lub brak
  klucza — puste pola Wi-Fi = bez zmian, patrz tryb „tylko klucz").

**Po wysłaniu** (`HTTP_SAVED`, `msg S`): `Zapisano.` — `Pstryk łączy się teraz
z Twoją siecią Wi-Fi…` — `Za chwilę na ekranie urządzenia pojawią się ceny
prądu. Jeśli po minucie urządzenie nadal pokazuje ekran konfiguracji, wróć do
tej strony (192.168.4.1) albo połącz się ponownie z siecią Pstryk-Setup —
zobaczysz, co poszło nie tak.` `HTTP_PARAMSAVED` (tryb „tylko klucz"):
`Zapisano. Urządzenie łączy się z zapisaną siecią Wi-Fi.`

**Statusy** (strona główna po nieudanej próbie): `Nie połączono z {v}` +
`Błędne hasło Wi-Fi` / `Nie znaleziono sieci` / `Nie udało się połączyć`;
`Połączono z {v} (adres IP {i})`; `Brak zapisanej sieci Wi-Fi.` (dyskretnie).

**Reszta symboli** (menu, tytuły stron, `/info`, `/update`, `/erase`, statyczne
IP): przetłumaczyć krótko i sensownie — nie są w menu, ale muszą istnieć.
Nie ruszać: `S_parampre`, `S_GET`, `S_POST`, `S_debugPrefix`, `D_HR`,
`S_ssidpre`, tokeny.

**Styl** (`HTTP_STYLE`): mobile-first, systemowy font, bazowo 16 px+, pola
i przyciski ≥ 48 px wysokości, `wrap` max ~520 px wycentrowany, przycisk główny
czarny (jak e-papier), wtórny konturowy, `.msg` z wariantami `S/D/P`
(zielony/czerwony/niebieski lewy bok), widoczny `:focus`, wiersze listy sieci
z podkreśleniem/obramowaniem i ikoną sygnału po prawej. Bez zewnętrznych
zasobów (portal nie ma internetu).

### A.3 Logika provisionera

```
firstRun   = s.apiKey.length() == 0
keyMissing = firstRun && s.ssid.length() > 0
intro      = firstRun ? (keyMissing ? INTRO_KEYMISSING : INTRO_FIRST)
                      : INTRO_RECONF z {v} := s.ssid      (String żyjący do końca portalu)
params     = [ WiFiManagerParameter(firstRun ? KEY_HINT_FIRST : KEY_HINT_RECONF),   // czysty HTML
               WiFiManagerParameter("apikey", "Klucz API", "", 80,
                                    firstRun ? PSTRYK_ATTR_KEY_FIRST : PSTRYK_ATTR_KEY_RECONF) ]
closedByForm = false
hasSavedNet = s.ssid.length() > 0            // head-skrypt `required` na SSID tylko gdy !hasSavedNet
setSaveParamsCallback: if (hasSavedNet && server->arg("s").isEmpty() && server->arg("p").isEmpty())
                          { closedByForm = true; stopConfigPortal(); }
ok = startConfigPortal(portalSsid(), portalPassword())
entered = trim(keyParam.getValue())
if (ok)                 { s.ssid = WiFi.SSID(); s.pass = WiFi.psk(); }
if (entered non-empty)  s.apiKey = entered
if (ok || closedByForm) s.save()
if (!ok && closedByForm && s.isComplete())
    ok = plain STA join(s.ssid, s.pass, 15 s)      // ta sama procedura co ścieżka bezobsługowa
if (ok) rememberAp()
return ok && WiFi.status()==WL_CONNECTED && s.isComplete()
```

Niezmienniki: zapisany klucz nigdy nie trafia do HTML; pusty klucz przy
rekonfiguracji = bez zmian; pierwsza konfiguracja kończy się sukcesem tylko
z kluczem (`isComplete()`); literówka w haśle Wi-Fi nie zamyka portalu.

## Rozwiązanie B — ekran powitalny i krótkie naciśnięcie

### B.1 Blok akcji (`drawWelcomeScreen`, kolumna prawa)

Reszta ekranu bez zmian (nagłówek, plakietka, powitanie, mini-wykres, pasek
„Przygotuj", stopka). Zmienia się wyłącznie strefa `y ∈ [240, 420]`, `x ≥ 420`:

```
piktogram:  ramka x 420..548, y 252..352 (128×100) — rząd 3 przycisków, palec na PIERWSZYM (lewym)
head  (564, 252): "Nacisnij i pusc"          adv 248 -> koniec 812
head  (564, 300): "1. przycisk od lewej"     adv 324 -> koniec 888   (48 px do marginesu 936)
body  (420, 386): "Za chwile pokaze ustawienia."  p.dark, adv 334 -> koniec 754
```

Usunięte: 7-segmentowa „3", `sekundy`, `Trzymaj, az pokaze ustawienia.`
(trzymanie nie jest już potrzebne). Linia `Za chwile pokaze ustawienia.` zostaje
jako informacja o opóźnieniu e-papieru (boot + odczyt baterii + pełne
przemalowanie ≈ 3–5 s po naciśnięciu) — bez niej część osób naciśnie drugi raz.

Wszystkie napisy ASCII (kontrola `tools/epd_textwidth.py`); pozycje lewe stałe,
prawa krawędź policzona z realnych szerokości (nie ma nic wyrównanego do prawej).

**Piktogram** (wybór spośród 3 wariantów, panel sędziowski w workflow): musi
(1) czytać się jako **rząd trzech przycisków**, z których **skrajny lewy** jest
tym właściwym (wypełniony/wyróżniony), (2) pokazywać **naciśnięcie** (palec —
kształt jak dotąd: `fillRoundRect` 14×36 r=7) i w miarę możliwości
**puszczenie** (np. dwie kreski „odbicia" nad palcem, jak dotychczasowe iskry),
(3) być czytelny po kwantyzacji do 16 szarości i przy oglądaniu z ręki,
(4) używać wyłącznie prymitywów `fillRect / drawRect / fillRoundRect / drawLine`
w palecie `ink / dark / mid / light / bg`, bez tekstu, (5) mieścić się w ramce
i nie sugerować krawędzi obudowy (góra/dół nieznane) — dopuszczalna neutralna
linia bazowa pod przyciskami. Kwirk `fillRoundRect` przy pełnym kole (2r == w): dwa
`epd_fill_circle` o średnicy 2r+1 wokół środków `x+r` i `x+w-r-1` malują razem
w+2 px **symetrycznie** (po 1 px z każdej strony w poziomie; w pionie przy
h > 2r kwirku nie ma) — sprawdzone symulacją sterownika w implementacji. Nie
przesuwać bazy o +1 tam, gdzie element ma trafić w oś (palec nad klawiszem);
+1 zostaje przy ikonie klucza, gdzie chroni margines, nie oś.
Kryteria sędziego: jednoznaczność „pierwszy od lewej", czytelność, spójność
z ikonografią ekranu (palec, pierścień, kreski 1–2 px), prostota (liczba
prymitywów).

### B.2 Firmware: krótkie naciśnięcie = obecność człowieka

`SleepCycle::setup()`:

```cpp
bool held       = buttonHeld(3000);
bool buttonWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;  // liczone TU, jedna zmienna na cały setup()
bool human      = held || buttonWake;
if (human) g_autoPortalOpens = 0;
bool wantsPortal = held || !settings_.isComplete();
…
// park rozładowanej baterii: nadal tylko `!held` (krótkie naciśnięcie nie ma
// prawa podnieść SoftAP na płaskim ogniwie; ekran parku mówi, co robić)
…
if (wantsPortal) {
  if (!human && g_autoPortalOpens >= kMaxAutoPortalOpens) { welcome; sleep 6 h; return; }
  if (!human) ++g_autoPortalOpens;
  …
}
```

Skutek: na niekompletnej konfiguracji **każde** naciśnięcie (krótkie lub długie)
otwiera portal natychmiast i odnawia pulę; wybudzenia timerowe nadal
limitowane pulą 4. Na kompletnej konfiguracji bez zmian: krótkie = „odśwież
teraz", długie = portal. Dotychczasowa deklaracja `buttonWake` w kroku 3.5 jest
zastępowana wcześniejszą zmienną.

### B.3 Makieta i dokumentacja

`docs/mockups/epaper-welcome.html` — ta sama mapa liczb co C++ (zasada rodziny).
Spec z 2026-08-25 dostaje adnotację o zastąpieniu bloku akcji (nie edytujemy
historii, dodajemy sekcję „Zmiany 2026-09-02"). README: sekcja „Pierwsze
uruchomienie" (gest, przycisk, portal po polsku, tryb „tylko klucz").

## Interfejsy

- `src/net/wm_strings_pl.h` — jak wyżej; **żadnych** zmian sygnatur w
  `WiFiProvisioner.h`.
- `src/render/EpdDashboard.h` — bez zmian (`drawWelcomeScreen(IRenderer&, int)`).
- `tools/epd_textwidth.py` — pomiar szerokości napisów fontami panelu (nowe).
- `tools/portal_preview.py` — odtwarza offline strony portalu z
  `wm_strings_pl.h` (root ×3 stany, `/wifi` ×2 stany z 3 udawanymi sieciami,
  strona po zapisie, root ze statusem błędnego hasła) do katalogu wyjściowego;
  służy do zrzutów w Chrome headless (viewport 390×844).

## Testy

- **Natywne**: bez nowej logiki poza Arduino (portal, rysowanie). Istniejące
  testy muszą przechodzić: `pio test -e native`.
- **Buildy**: `pio run -e t5_epaper_s3 -e t5_epaper_s3_bootstrap -e tdisplay_long
  -e tdisplay_long_bootstrap` — wszystkie SUCCESS; `strings firmware.bin | grep
  "html lang='pl'"` w obu płytkach.
- **Zestaw symboli**: nazwy `const char …[]` w `wm_strings_pl.h` ⊇ nazwy
  z `wm_strings_en.h` (skrypt w weryfikacji).
- **Podgląd portalu**: zrzuty z `tools/portal_preview.py` obejrzane przez
  recenzenta (polszczyzna, hierarchia, czy klucz wygląda na wymagany).
- **Makieta ekranu**: zrzut `docs/mockups/epaper-welcome.html` po zmianie,
  `tools/epd_textwidth.py` dla wszystkich nowych napisów.
- **Na sprzęcie** (ręcznie, po wgraniu): krótkie naciśnięcie na ekranie
  powitalnym otwiera portal (także po wyczerpaniu puli); portal po polsku
  otwiera się automatycznie na telefonie; formularz bez klucza nie da się
  wysłać; złe hasło Wi-Fi → status na stronie głównej, klucz nadal wpisany;
  rekonfiguracja z pustymi polami Wi-Fi i nowym kluczem → „Zapisano", urządzenie
  łączy się ze starą siecią i używa nowego klucza.

## Świadomie poza zakresem

- Ponowne otwieranie portalu z urządzenia po wysłaniu formularza bez klucza
  (druga tura AP) — `required` + skrypt SSID pokrywają to w każdej współczesnej
  przeglądarce; ścieżka awaryjna = ekran powitalny → naciśnięcie → intro
  „Brakuje klucza".
- Prefill hasła Wi-Fi / klucza przy rekonfiguracji (portal nie zdradza sekretów).
- Ekran „Blad klucza API / Przytrzymaj przycisk" (konfiguracja kompletna,
  zły klucz): gest długiego trzymania tam zostaje — krótkie = odświeżenie.
- Ekran konfiguracji e-papieru (`drawSetupScreen`) i ekrany T-Display-Long.
- `setHostname`, kod kraju Wi-Fi, ciemny motyw portalu.

## Pytania otwarte

- Krawędź obudowy z przyciskami (góra/dół) — gdy znana, piktogram może dostać
  sylwetkę krawędzi (jedna stała).
- Ścieżka do klucza w aplikacji Pstryk (`Konto → Urządzenia i integracje →
  Wygeneruj klucz`) — potwierdzić brzmienie przed wydaniem (jak w spec
  z 2026-08-25).
- Jednorazowa weryfikacja na panelu: czytelność wybranego piktogramu po
  kwantyzacji.
