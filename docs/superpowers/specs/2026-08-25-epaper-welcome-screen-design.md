# Ekran powitalny e-paper (out-of-box) — projekt

Data: 2026-08-25
Płytka: **LilyGo T5 4.7" e-paper S3** (960×540, 16 odcieni szarości)
Zakres: **wyłącznie** `t5_epaper_s3`. T-Display-Long pozostaje bez zmian.
Proces: 4 niezależne koncepcje → panel 3 sędziów (klient nietechniczny /
rzemiosło e-ink / inżynier firmware) → synteza. Zwycięzca: „Wizytówka Pstryk"
(2 z 3 werdyktów), wzbogacona o przeszczepy z pozostałych koncepcji.

## Problem

Gdy portal konfiguracyjny wygasa bez kompletnej konfiguracji,
`SleepCycle::setup()` malowało dwie wycentrowane linie przez `msgScreen()`:

```
                    Konfiguracja niepelna
                    Przytrzymaj przycisk
```

E-papier **trzyma obraz bez zasilania** — więc dokładnie ten ekran zostaje na
panelu po fabrycznym pierwszym rozruchu (portal 10 min → timeout → ta gałąź)
i jedzie w pudełku do klienta. Pierwszą rzeczą, jaką klient widzi po otwarciu
paczki, jest komunikat wyglądający jak błąd — nie produkt.

Trzy braki:

1. **Zero tożsamości.** Ekran nie mówi, co to za urządzenie ani po co jest.
2. **Instrukcja niekompletna.** „Przytrzymaj przycisk" nie mówi jak długo,
   a e-papier nie daje żadnej reakcji przez całe 3 sekundy trzymania — część
   osób puszcza za wcześnie i uznaje, że przycisk nie działa.
3. **Nie uprzedza porażek.** Najczęstsze scenariusze utknięcia: brak klucza API
   pod ręką (portal porzucony → powrót na ten sam ekran) i padnięta w połowie
   konfiguracji bateria. Ekran o żadnym nie wspomina.

## Cel

Jeden ekran-wizytówka, który: wita jak produkt (nie jak błąd), mówi po co
urządzenie jest, prowadzi do JEDNEJ akcji (przytrzymaj przycisk 3 sekundy,
trzymaj aż pokaże ustawienia) i uprzedza, co przygotować przed startem.
Ekran działa w obu rolach: prosto z pudełka **i** po porzuconej konfiguracji.

## Ograniczenia (zweryfikowane w kodzie, nie założone)

Obowiązuje tabela ograniczeń ze spec ekranu konfiguracji
(`2026-08-24-epaper-setup-screen-design.md`): fonty tylko ASCII 0x20–0x7E
(znak spoza zakresu znika bez śladu), dwa rozmiary tekstu (body 30 px /
head 45 px), 7-segmentowe cyfry w dowolnej wysokości (`drawDigit`), prymitywy
`fillRect` / `drawRect` / `fillRoundRect` / `drawLine` / `text` (bez bitmap
i pojedynczych pikseli), odczyt baterii przed Wi-Fi.

Dodatkowe, specyficzne dla tego ekranu:

| Ograniczenie | Źródło | Konsekwencja |
|---|---|---|
| Pozycja fizycznego przycisku na obudowie nieznana | brak zdjęć sprzętu w repo; `docs/screenshots/epaper-board.png` to render pulpitu | Zakaz strzałek wskazujących poza ekran. Piktogram przycisku musi być samoopisowy i abstrakcyjny. |
| Czas może być nieustawiony | ta gałąź biegnie przed seedem RTC/NTP w `setup()` | Zero zegara/daty na ekranie. |
| `fillRoundRect` z `r = w/2` daje koło | `EpdRenderer.cpp:47` — cztery `epd_fill_circle` + dwa `epd_fill_rect` | Pierścień przycisku, palec i główka klucza są wykonalne bez nowych prymitywów. |
| Bateria: dostępny jest wyłącznie `batPct` z kroku 1 `setup()` | `readBatteryPercent()` przed portalem (ADC2 vs Wi-Fi) | Ekran przyjmuje jeden `int batteryPct`; −1 = nieznany, jak wszędzie. |

## Rozwiązanie

Nowa funkcja `drawWelcomeScreen()` obok `drawSetupScreen()`. `drawMessage()`
pozostaje nietknięte (obsługuje ~10 innych ekranów).

### Układ (960×540, M = 24)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Pstryk - Pierwsze uruchomienie                          [==  ] BAT 62 %  │  y=10  body
├──────────────────────────────────────────────────────────────────────────┤  y=52  linia mid
│   ┌────────────┐        Czesc, jestem Pstryk.                            │  y=96  HEAD
│   │  ......    │                                                         │
│   │  ..##..    │        Pokaze Ci, kiedy prad jest tani,      ▂▄█▃▆     │  y=157/187 body
│   │  .####.    │        a kiedy lepiej poczekac.             (mini-wykres)│
│   │  ..##..    │                                                         │
│   │  ......    │        \./  Przytrzymaj przycisk                        │  y=252 HEAD
│   │ (piorun)   │        (o)  ▌▐ sekundy                                  │  y=309 7-seg h=60
│   └────────────┘             Trzymaj, az pokaze ustawienia.              │  y=386 body DARK
├──────────────────────────────────────────────────────────────────────────┤  y=428 linia mid
│ Przygotuj:  [] telefon  [] haslo Wi-Fi  [] klucz API  [] ladowarka       │  y=442 body
│ (o-- Klucz API: Pstryk > Konto > Urzadzenia i integracje                 │  y=474 body DARK
│ v1.4.2                                    (nisko: Podlacz mnie do lad.)  │  y=516 body szary
└──────────────────────────────────────────────────────────────────────────┘
```

Współrzędne stref są wiążące; makieta `docs/mockups/epaper-welcome.html`
niesie tę samą mapę liczb co implementacja. Pozycje prawostronne i rząd
checkboxów liczą się w runtime z `textWidth()` — stały `x` tylko tam, gdzie
tekst zaczyna się od lewej (sędzia firmware: pesymistyczna metryka 15 px/znak
body i 22 px/znak head zostawia dwie linie na granicy marginesu; mierzenie
realnym fontem je zabezpiecza).

### Nagłówek

`Pstryk - Pierwsze uruchomienie` — wzorzec rodziny „Pstryk - X". Etykieta jest
prawdziwa w obu rolach ekranu; czyste „Witaj!" kłamałoby przy powrocie po
porzuconym portalu. Badge baterii pikselowo 1:1 z `drawSetupScreen`
(ikona 36×18 **po lewej** tekstu, `batX - 52`, y=16; `BAT ?` przy −1).

### Plakietka marki (lewa kolumna)

Czarna płyta `fillRoundRect(70,112,280,280,56)` na cieniu z `p.light`
przesuniętym o (12,12), z białym piorunem. W pierwszych 2 sekundach po
otwarciu pudełka ekran mówi „jestem produktem" — i wygląda jak ikona
aplikacji, której klient za chwilę poszuka w telefonie.

Piorun: sylwetka symbolu wysokiego napięcia — dwie równoległe przekątne
kreski (plastry 8 px schodzące o 6 px w lewo, ~37°, rysowane z 1 px zakładką),
złamane w połowie **półką wystającą w prawo** `fillRect(142,252,114,32)`:

```cpp
for (int i = 0; i < 12; ++i) r.fillRect(234 - 6*i, 156 + 8*i, 52, 9, p.bg);
r.fillRect(142, 252, 114, 32, p.bg);
for (int i = 0; i < 8; ++i)  r.fillRect(196 - 6*i, 284 + 8*i, 44, 9, p.bg);
```

Pierwsza wersja (6 rzędów po 32 px, krok 24 px — z syntezy) poległa na
makiecie: grube rzędy ząbkują obie krawędzie i półka ginie w szumie —
sylwetka czytała się jak schodki, nie piorun. Cienkie plastry dają gładkie
przekątne, a półka zostaje jedynym elementem łamiącym rytm. To jest powód,
dla którego plastry **muszą** zostać cienkie.

### Powitanie i obietnica (prawa kolumna, x=420)

`Czesc, jestem Pstryk.` (head, y=96) — urządzenie mówi w pierwszej osobie już
na ekranie portalu („Podlacz mnie do ladowarki"). Kropka zamiast wykrzyknika.

`Pokaze Ci, kiedy prad jest tani, / a kiedy lepiej poczekac.` (body,
y=157/187) + mini-wykres 5 słupków `fillRect` (796..894, baza y=212;
ciemniejsze = droższe, ten sam kierunek co realny `drawChart`). Bez liczb
i bez legendy — zero ryzyka odczytu „to już działa".

### Blok akcji (jedyny punkt wejścia)

- Piktogram (422..474, 52×82): pierścień `fillRoundRect(422,300,52,52,26)` + białe
  wnętrze + palec `fillRoundRect(441,282,14,36,7)` rysowany **po** wnętrzu
  (dół palca wchodzi 10 px w biel = wciśnięcie) + dwie iskry `drawLine`.
  Złożenie czyta się jak uniwersalny symbol power (⏻) z iskrami — szczęśliwy
  zbieg, który wzmacnia komunikat „to jest przycisk zasilania/startu".
  Celowo abstrakcyjny: bez sugestii pozycji przycisku na obudowie.
- `Przytrzymaj przycisk` (head, 488,252).
- Wiersz czasu: 7-segmentowa `3` (`drawDigit(r,488,309,35,60,8,p.ink,SEG[3])`)
  + `sekundy` (head, 539,333; dolna krawędź ascendera zgrana z dołem cyfry
  na y=369). Cyfra rymuje się z 7-seg hasłem ekranu portalu; czas trzymania
  w body byłby o pół tonu za cichy (zgodna uwaga dwóch sędziów).
- `Trzymaj, az pokaze ustawienia.` (body, **p.dark**, 420,386) — funkcjonalnie
  krytyczna linia: e-papier nie daje żadnej reakcji przez całe 3 s trzymania
  i bez niej część osób puszcza za wcześnie. Niesie też skutek gestu, więc
  działa w obu rolach ekranu. Dark, nie mid — reguła „treści krytyczne nie
  w mid" (veto sędziów).

### Pasek „Przygotuj"

Linia mid y=428, potem `Przygotuj:` + 4 checkboxy 18×18 (podwójny `drawRect`
= optyczne 2 px) z etykietami `telefon`, `haslo Wi-Fi`, `klucz API`,
`ladowarka`. Rozstaw w runtime: etykieta na `cbX+28`, następny checkbox na
`koniec_etykiety + 28`. Ładowarka jest na liście, bo portal rozdaje Wi-Fi
i szybko drenuje ogniwo — padnięta w połowie konfiguracji bateria to drugi
najczęstszy scenariusz porażki po braku klucza.

Rząd klucza (y=474): ikona klucza (główka `fillRoundRect` 20×20 r=10 z białym
otworem + trzon + dwa zęby, pole 25..77 — baza M+1, bo `fillRoundRect` przy
pełnym kole maluje średnicę 2r+1 i główka na x=M wchodziłaby 1 px w margines)
i ścieżka
`Klucz API: Pstryk > Konto > Urzadzenia i integracje` (body, **p.dark**).
Wariant 61-znakowy z „aplikacja Pstryk" odrzucony: w pesymistycznej metryce
wystawał 3 px za margines (veto warunkowe sędziego firmware).

### Stopka i warianty baterii

`v<wersja>` szarym rgb(120) na (20,516) — konwencja rodziny. Prawy róg
domyślnie pusty; **przy pct 0..19** pojawia się tam
`Podlacz mnie do ladowarki` (body, p.dark, dosunięte do prawego marginesu) —
dosłownie głos rodziny z ekranu portalu. Skład bez descenderów; jedyny ink pod
baseline (wiersz 540) to 1-pikselowy overshoot litery „a", który wypada poza
panel (wiersze 0..539) i jest bezpiecznie obcinany przez rasteryzer fontu. **Przy pct == −1** zmienia się wyłącznie badge
(`BAT ?`, pusta ikona): nieznany poziom to stan czujnika, nie wezwanie do
akcji. Ekran jest poza tym celowo bezstanowy — checklista nigdy nie zmienia
kolejności między wybudzeniami.

## Interfejsy

### `src/render/EpdDashboard.h`

```cpp
// Ekran powitalny (pierwsze uruchomienie / konfiguracja niedokonczona).
// batteryPct: -1 = nieznany. Flushuje panel.
void drawWelcomeScreen(IRenderer& r, int batteryPct);
```

Funkcja mieszka w `EpdDashboard.cpp`, bo używa prywatnych pomocników z jego
anonimowej przestrzeni nazw: `Pal`, `drawBatteryIcon`, `drawDigit`, `SEG`.

## Miejsce wywołania

Jedno: `SleepCycle.cpp`, gałąź „portal wygasł bez kompletnej konfiguracji"
(dotąd `msgScreen(gfx_, "Konfiguracja niepelna", "Przytrzymaj przycisk")`).
Nowy wrapper `welcomeScreen(gfx_, batPct)` wykonuje ten sam rytuał zatrzasków
co `msgScreen()`/`setupScreen()` (`g_lastDrawSig = 0`, `g_authShown = 0`,
`g_lastDeepCleanDay = 0`, `requestDeepClean()`). Deep clean ma tu własny powód
poza spójnością: ekran zostaje na panelu przez cały pobyt w pudełku, a wielkie
czarne pole plakietki najłatwiej ze wszystkiego zostawia ducha pod następnym
pełnym przemalowaniem. `batPct` jest już policzone w kroku 1 `setup()`.

Ścieżka parkowania rozładowanej baterii i ścieżki błędów zostają przy
`msgScreen()` bez zmian.

## Testy

**Natywne**: brak nowej logiki testowalnej poza sprzętem (czyste rysowanie;
payloady i formaty nie występują). Istniejące 92 testy muszą przechodzić.

**Makieta** (`docs/mockups/epaper-welcome.html`): natywna rozdzielczość,
ta sama mapa liczb co implementacja, `BAT` przestawialne na −1 i <20 do
obejrzenia wariantów. Iteracja pioruna odbyła się na makiecie przed
zamrożeniem współrzędnych.

**Na sprzęcie** (ręcznie, jednorazowo):

- ekran pojawia się po timeoucie portalu bez zapisanej konfiguracji;
- przytrzymanie przycisku 3 s z tego ekranu otwiera portal (ext0 → held);
- piorun i piktogram czytelne z ~2 m; `BAT ?` przy odłączonym ogniwie;
- wariant `pct < 20` pokazuje prośbę o ładowarkę w stopce.

## Świadomie poza zakresem (rozstrzygnięcia syntezy i sędziów)

- **Atrapa pulpitu / przykładowa cena** — podwójne veto: zmyślona kwota bez
  słowa „przyklad" grozi odczytem „już działa / pokazuje złe dane", a 7-seg
  w kolorze mid wymagałby zmiany renderera (`p.ink` zaszyte w `drawBigPrice`).
  Obraz produktu niesie bezpiecznie mini-wykres bez liczb.
- **Numerowane kroki 1-2-3** — kolizja z numeracją 1-2-3 ekranu portalu
  („krok 3" tutaj stawałby się „krokiem 1" tam).
- **Strzałka do fizycznego przycisku** — pozycja przycisku na obudowie
  nieznana; do rozważenia osobno, gdy będzie potwierdzona (jedna stała
  + obrót ikony).
- **`pstryk.pl` w stopce** — descendery liter na y=516 (baseline = ostatni
  wiersz panelu) zostałyby obcięte; jeśli rodzina ma dostać adres pomocy,
  zaprojektować to jako zmianę konwencji rodziny, nie wyjątek jednego ekranu.
- **Informacja o samoczynnym ponawianiu portalu** — szum na wizytówce; kandydat
  do papierowej wkładki w pudełku. (Już po tej specyfikacji samo ponawianie
  zostało zlimitowane do 4 automatycznych otwarć od ostatniej interakcji, żeby
  urządzenie czekające w pudełku nie zjadało ogniwa — patrz `g_autoPortalOpens`
  w `SleepCycle.cpp` i README; po wyczerpaniu puli wybudzenie co 6 h nadal
  odświeża ten ekran, więc badge baterii mówi prawdę przy rozpakowaniu.)
- **Reorder checklisty przy niskiej baterii** — ekran ma być bezstanowy;
  pozycje przeskakujące między wybudzeniami myliłyby wracającego użytkownika.

## Pytania otwarte

- Ścieżka „Pstryk > Konto > Urzadzenia i integracje" może się zdezaktualizować
  z aktualizacją aplikacji Pstryk — potwierdzić brzmienie przed wydaniem.
- Jednorazowy render na panelu: czytelność piktogramu przycisku (52×82)
  i równość plastrów pioruna po kwantyzacji do 16 odcieni.
- Pozycja fizycznego przycisku na obudowie: jeśli zostanie potwierdzona,
  strzałka kierunkowa przy piktogramie to tania, silna poprawa intuicyjności.
