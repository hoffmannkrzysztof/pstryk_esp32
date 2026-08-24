# Ekran konfiguracji e-paper — projekt

Data: 2026-08-24
Płytka: **LilyGo T5 4.7" e-paper S3** (960×540, 16 odcieni szarości)
Zakres: **wyłącznie** `t5_epaper_s3`. T-Display-Long pozostaje bez zmian.

## Problem

Ekran otwierający portal konfiguracyjny to dziś dwa wiersze tekstu przez
`drawMessage()` (`src/render/EpdDashboard.cpp:214`):

```
                    Konfiguracja
          Pstryk-Setup  haslo: 41850372
```

Trzy braki:

1. **Hasło jest nieczytelne z dystansu.** Renderuje się fontem `UiSansBody`
   (30 px) w jednej linii razem z SSID. Urządzenie wisi na ścianie; osiem cyfr
   trzeba przepisać do telefonu.
2. **Nie ma instrukcji.** Ekran nie mówi, co zrobić po połączeniu z siecią
   (`192.168.4.1`, gdyby captive portal nie otworzył się sam).
3. **Nie ostrzega o poborze prądu.** Portal podnosi SoftAP i trzyma radio
   non-stop przez maks. 10 minut. To najdroższy energetycznie stan urządzenia,
   a użytkownik nie ma sygnału, żeby podpiąć ładowarkę.

## Cel

Jeden dedykowany ekran konfiguracji, który: pokazuje hasło czytelnie z kilku
metrów, daje kod QR do dołączenia bez przepisywania, prowadzi krok po kroku
i wprost prosi o podłączenie ładowarki.

## Ograniczenia (zweryfikowane w kodzie, nie założone)

| Ograniczenie | Źródło | Konsekwencja dla projektu |
|---|---|---|
| Fonty pokrywają **tylko ASCII 0x20–0x7E** | `lib/EPD47/src/ui_font_body.h:687`, `ui_font_head.h:1311` — po jednym `UnicodeInterval{0x20, 0x7E, 0}` | Zero polskich znaków diakrytycznych, zero emoji. Ikony rysowane prymitywami. |
| Brakujący glif wypada z renderu | `props.fallback_glyph = 0` (`src/render/EpdRenderer.cpp:78`) → `get_glyph(font, 0, …)` (`lib/EPD47/src/font.c:370`); 0 leży poza jedynym interwałem | Znak spoza ASCII nie da się zauważyć jako „krzaczek" — po prostu zniknie. Tym groźniejsze. |
| Dostępne są **dwa** rozmiary tekstu | `fontForSize()` (`src/render/EpdRenderer.cpp:64`): `size >= 2` → `UiSansHead` (advance_y 45, ascender 36), inaczej `UiSansBody` (advance_y 30, ascender 24) | Duże hasło nie może powstać z tekstu. |
| Prymitywy rysowania | `IRenderer` (`src/render/IRenderer.h`): `fillRect`, `drawRect`, `fillRoundRect`, `drawLine`, `text` | Brak bitmap i brak rysowania pojedynczych pikseli — QR musi być malowany jako `fillRect` per moduł. |
| Odczyt baterii koliduje z Wi-Fi | `readBatteryPercent()` używa ADC2 (`src/app/SleepCycle.cpp:112`), stąd komentarz „battery FIRST" w `setup()` (`:319`) | Poziom baterii musi być odczytany **przed** wejściem na ekran, nigdy w trakcie. |
| Hasło jest losowane raz na boot | `WiFiProvisioner::portalPassword()` (`src/net/WiFiProvisioner.cpp:31`) | QR i cyfry muszą pochodzić z tego samego wywołania — są spójne z definicji, bo funkcja cache'uje w `static`. |

## Rozwiązanie

Nowa funkcja `drawSetupScreen()` obok istniejącej `drawMessage()`.
`drawMessage()` **nie jest modyfikowana** — obsługuje ~10 innych ekranów
(błędy, park baterii, instalacja OTA) i nie ma powodu ich ruszać.

### Układ (960×540, margines `M = 24` jak w `drawDashboard`)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Pstryk - Konfiguracja                                    [==  ] BAT 62 % │  y=10   body
├──────────────────────────────────────────────────────────────────────────┤  y=46   hairline
│                                                                          │
│  1. Polacz telefon z siecia            ┌────────────────────────┐        │  y=64   body
│                                        │                        │        │
│     Pstryk-Setup                       │      KOD QR 29x29      │        │  y=104  HEAD 45px
│                                        │      modul = 8 px      │        │
│  2. Wpisz haslo                        │      + quiet zone 4    │        │  y=166  body
│                                        │                        │        │
│   ┌──────────────────────────────┐     │                        │        │
│   │   4 1 8 5 0 3 7 2            │     │                        │        │  y=200  7-seg h=80
│   └──────────────────────────────┘     └────────────────────────┘        │
│                                                                          │
│  3. Otworz 192.168.4.1                   Zeskanuj - dolaczysz bez hasla   │  y=336 / y=366
├──────────────────────────────────────────────────────────────────────────┤  y=400  hairline
│ /|  Podlacz mnie do ladowarki                                            │  y=406  HEAD 45px
│ |/  Rozdaje tu Wi-Fi i szybko sie rozladowuje.                           │  y=456  body
│                                                                 v1.4.2   │  y=506  body, szary
└──────────────────────────────────────────────────────────────────────────┘
```

Współrzędne w tabeli są wiążące co do **stref**, nie co do piksela — dokładne
wartości domyka implementacja przeciw makiecie HTML.

### Duże hasło — reużycie 7-segmentowego renderera

Hasło portalu to **osiem cyfr dziesiętnych** (`WiFiProvisioner.cpp:31`:
`psk[i] = '0' + (esp_random() % 10)`). W `EpdDashboard.cpp:24` istnieje już
`drawDigit()` / `drawBigPrice()` — 7-segmentowy renderer napisany dla dużej ceny
na pulpicie. To dokładnie potrzebne narzędzie: cyfry dowolnej wysokości, zero
nowego fontu, zero kosztu flash, kod już w produkcji.

Przy `h = 80`: `w = 46`, `t = 10`, `gap = 12` → osiem cyfr zajmuje
`8*46 + 7*12 = 452 px`. Ramka `drawRect` 500×116 z paddingiem 24 px.

`drawBigPrice()` jest w anonimowej przestrzeni nazw tego samego pliku, więc
`drawSetupScreen()` sięga po nią bez zmiany widoczności. Wywoływana jest na
łańcuchu cyfr — ścieżki `,`/`.`/`-` po prostu się nie uruchamiają.

### Kod QR

Payload w standardzie Wi-Fi Network config (format czytany natywnie przez
aparat iOS i Androida):

```
WIFI:T:WPA;S:Pstryk-Setup;P:41850372;;
```

39 bajtów. Wersja 3 z korekcją M mieści 42 bajty w trybie bajtowym → mieści się
z zapasem 3 bajtów.

**Escapowanie jest świadomie pominięte.** Specyfikacja `WIFI:` wymaga
poprzedzenia `\` dla `\ ; , : "`. SSID to stała `"Pstryk-Setup"` (myślnik nie
jest znakiem specjalnym), a PSK to wyłącznie cyfry — żaden znak wymagający
escapowania nie może tam wystąpić. Kod escapujący byłby martwy i nietestowalny.
Zamiast niego: test jednostkowy pilnujący dokładnego formatu payloadu.

**Zachowanie awaryjne:** `qrcode_initText()` zwraca `-1`, gdy payload nie mieści
się w żądanej wersji (upstream `qrcode.c`: `if (mode < 0) { return -1; }`) — więc
ta gałąź jest realnie osiągalna, np. gdyby ktoś wydłużył SSID. Przy błędzie ekran
rysuje się **bez** kolumny QR, a lewa kolumna rozciąga się na pełną szerokość.
Brak QR nie może wywrócić konfiguracji — cyfry hasła są nadrzędną ścieżką, QR
jest tylko udogodnieniem.

Moduły malowane są jako `fillRect(x0 + mx*8, y0 + my*8, 8, 8, ink)` — `IRenderer`
nie ma rysowania pikseli, a `fillRect` mapuje się na `epd_fill_rect`.

### Blok o ładowarce

Zawsze widoczny, niezależnie od poziomu naładowania — bo to stan urządzenia
(SoftAP non-stop), a nie stan baterii, jest powodem ostrzeżenia.

```
Podlacz mnie do ladowarki                      (UiSansHead, 45 px)
Rozdaje tu Wi-Fi i szybko sie rozladowuje.     (UiSansBody, 30 px)
```

Bez ogonków — wymuszone zakresem fontu (patrz tabela ograniczeń).

Po lewej ikona ~44×44 px rysowana prymitywami: obrys baterii (`drawRect`),
wypełnienie proporcjonalne do `batteryPct` (`fillRect`), styk (`fillRect`).
Przy `batteryPct < 0` (odczyt nieznany) ikona jest pusta, bez wypełnienia.

### Odznaka baterii w nagłówku

`[==  ] BAT 62 %` po prawej stronie paska nagłówka. Przy `batteryPct < 0`
tekst brzmi `BAT ?` i ikona zostaje pusta — spójnie z `drawDashboard`
(`EpdDashboard.cpp:133`), które już rozróżnia `-1` jako „nieznane".

## Interfejsy

### `src/render/EpdDashboard.h`

```cpp
struct SetupInfo {
  const char* ssid;        // rozgłaszany SSID portalu
  const char* password;    // 8 cyfr z portalPassword()
  const char* portalUrl;   // "192.168.4.1"
  int  batteryPct;         // -1 = nieznany
};

// Pełnoekranowy ekran konfiguracji portalu: SSID, duze haslo, kod QR,
// instrukcja i prosba o podlaczenie ladowarki. Flushuje panel.
void drawSetupScreen(IRenderer& r, const SetupInfo& info);
```

### `src/core/SetupQr.h` (testowalny natywnie)

```cpp
// Buduje payload "WIFI:T:WPA;S:<ssid>;P:<psk>;;" do out (rozmiar n).
// Zwraca false gdy się nie mieści; out jest wtedy pustym stringiem.
bool buildWifiQrPayload(const char* ssid, const char* psk, char* out, size_t n);
```

Trafia do `core/`, bo `env:native` buduje `+<core/> +<view/>` — to jedyna część
tej funkcji, którą da się przetestować bez sprzętu, i jedyna, która może po
cichu zepsuć skanowanie (kolejność pól, podwójny średnik na końcu).

### `src/net/WiFiProvisioner.h`

```cpp
// SSID rozgłaszany przez portal. Jedno źródło prawdy dla ekranu i kodu QR.
static const char* portalSsid();
```

Dziś `"Pstryk-Setup"` jest literałem w `WiFiProvisioner.cpp:95` i osobno w
czterech `snprintf` budujących komunikat. Kod QR pokazujący inny SSID niż
faktycznie rozgłaszany byłby zepsuty w sposób niewidoczny na ekranie, więc
literał zostaje wyekstrahowany i `startConfigPortal()` używa tej samej funkcji.

## Miejsca wywołania

Dwa, oba w `src/app/SleepCycle.cpp`:

| Linia | Ścieżka | Zmiana |
|---|---|---|
| `:345` | rekonfiguracja / pierwszy boot | `msgScreen(…, "Konfiguracja", pskLine)` → `setupScreen(gfx_, batPct)`; `batPct` jest już policzone w `:324` |
| `:275` | `runBootstrap()`, obraz instalatora | to samo, ale **dochodzi odczyt baterii** — dziś `runBootstrap()` w ogóle nie czyta ADC, więc bez tego ekran pokazywałby `BAT ?` |

Nowy wrapper `setupScreen()` obok `msgScreen()` (`:260`) musi wykonać **ten sam
rytuał zatrzasków**, bo to również pełnoekranowe przemalowanie:

```cpp
g_lastDrawSig = 0;         // sygnatura pulpitu nieaktualna
g_authShown = 0;           // ekran zasłonił podpowiedź o kluczu API
g_lastDeepCleanDay = 0;    // następny pulpit robi pełny deghost
g.requestDeepClean();      // ten ekran też: QR wymaga maks. kontrastu
```

`requestDeepClean()` ma tu dodatkowe uzasadnienie poza spójnością z
`msgScreen()`: pozostałości po poprzednim obrazie obniżają kontrast modułów QR,
a QR ma najmniejszy margines błędu ze wszystkiego, co rysujemy.

Ekran konfiguracji **nie może** trafić do ścieżki parkowania rozładowanej
baterii (`:334`). Ta gałąź celowo nie podnosi radia i zostaje przy
`msgScreen(gfx_, "Bateria rozladowana", "Podlacz ladowarke")` bez zmian.

## Zależności

`platformio.ini`, sekcja `[env:t5_epaper_s3]`:

```ini
lib_deps =
    bblanchon/ArduinoJson@7.4.3
    tzapu/WiFiManager@2.0.17
    ricmoo/QRCode@0.0.1
```

Wersja przypięta dokładnie, zgodnie z zasadą już opisaną w `platformio.ini`
(release'y są podpisywane i publikowane od razu, więc zakres z daszkiem
oznaczałby wysyłkę binarki zbudowanej na nietestowanej wersji biblioteki).

Zweryfikowane: `0.0.1` to jedyna wersja w rejestrze PlatformIO, więc pin nie
może się rozjechać. Nagłówek `qrcode.h` opakowuje API w `extern "C"`, więc
włącza się czysto z C++.

API, którego używamy:

```c
uint16_t qrcode_getBufferSize(uint8_t version);
int8_t   qrcode_initText(QRCode*, uint8_t* modules, uint8_t version,
                         uint8_t ecc, const char* data);
bool     qrcode_getModule(QRCode*, uint8_t x, uint8_t y);
```

Koszt: ~10 KB flash, bufor 106 B na stosie dla wersji 3
(`qrcode_getBufferSize(3)` = `(29*29+7)/8` = 106). Bez alokacji na stercie.

Biblioteka dodawana jest **wyłącznie** do `env:t5_epaper_s3` i dziedziczącego
`env:t5_epaper_s3_bootstrap`. `env:tdisplay_long` i `env:native` nie dostają jej
w `lib_deps` w ogóle.

## Testy

**Natywne** (`env:native`, Unity — nowy katalog `test/test_setup_qr`):

- payload dla znanego SSID i PSK ma dokładnie postać `WIFI:T:WPA;S:Pstryk-Setup;P:41850372;;`
- podwójny średnik na końcu jest obecny (terminator formatu)
- bufor za mały → zwraca `false` i zostawia pusty string, nie ucięty payload
- 8-cyfrowy PSK i 12-znakowy SSID mieszczą się w 42 bajtach pojemności v3-M

**Na sprzęcie** (ręcznie, jednorazowo — brak harnessu do testów panelu):

- ekran po `pio run -e t5_epaper_s3 -t upload` i przytrzymaniu przycisku 3 s
- QR skanuje się z ~40 cm aparatem iOS i Androidem, telefon dołącza do sieci
- osiem cyfr czytelne z ~3 m
- `BAT ?` przy odłączonym ogniwie (weryfikuje ścieżkę `batteryPct == -1`)

**Makieta** (`docs/mockups/epaper-setup.html`) w natywnej rozdzielczości, wzorem
istniejących `long.html` / `epaper.html` — pozwala domknąć układ bez wgrywania
firmware'u.

## Świadomie poza zakresem

- **T-Display-Long** — inny renderer, inny ekran, i nie ma tam baterii ani
  odczytu ADC (`App.cpp` nie zawiera `core/Battery.h`), więc prośba o ładowarkę
  byłaby myląca: płytka jest zasilana z USB 24/7.
- **Escapowanie payloadu `WIFI:`** — martwy kod przy stałym SSID i PSK z cyfr.
- **Zmiany w `drawMessage()`** — obsługuje ~10 innych ekranów, poza zakresem.
- **Drugi QR z adresem portalu** — captive portal otwiera się sam; adres
  `192.168.4.1` zostaje jako tekstowy fallback w kroku 3.
- **Polskie znaki diakrytyczne** — zablokowane zakresem fontu, nie decyzją.
