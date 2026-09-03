#include "net/WiFiProvisioner.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_attr.h>
#include <esp_random.h>
#include <cstring>

namespace pstryk {

// Last successful association, retained across deep sleep (zeroed on power-on
// reset, RTC RAM semantics). Lets the battery board's wakes skip the AP scan
// (directed channel+BSSID join) and the DHCP handshake (cached lease): about
// 1.5-3 s less radio time per connected wake. Inert on the always-on board.
RTC_DATA_ATTR static uint8_t  g_apBssid[6];
RTC_DATA_ATTR static int32_t  g_apChannel = 0;
RTC_DATA_ATTR static uint32_t g_apIp = 0, g_apGw = 0, g_apMask = 0, g_apDns = 0;

static void rememberAp() {
  const uint8_t* b = WiFi.BSSID();
  if (!b || WiFi.channel() <= 0) return;
  std::memcpy(g_apBssid, b, 6);
  g_apChannel = WiFi.channel();
  g_apIp   = (uint32_t)WiFi.localIP();
  g_apGw   = (uint32_t)WiFi.gatewayIP();
  g_apMask = (uint32_t)WiFi.subnetMask();
  g_apDns  = (uint32_t)WiFi.dnsIP();
}

void WiFiProvisioner::forgetAp() { g_apChannel = 0; }

// Bez znakow specjalnych formatu WIFI: (\ ; , : ") -- buildWifiQrPayload w
// core/SetupQr swiadomie nie escapuje, wychodzac z tego zalozenia.
const char* WiFiProvisioner::portalSsid() { return "Pstryk-Setup"; }

const char* WiFiProvisioner::portalPassword() {
  // Random per boot (NOT MAC-derived: the SoftAP BSSID visible in beacons is
  // built from the same base MAC, so a MAC-derived PSK would be computable by
  // anyone sniffing the air). Digits only -- easy to retype from the screen.
  static char psk[9] = "";
  if (!psk[0]) {
    for (int i = 0; i < 8; ++i) psk[i] = (char)('0' + (esp_random() % 10));
    psk[8] = '\0';
  }
  return psk;
}

bool WiFiProvisioner::ensureConnected(Settings& s, bool forcePortal) {
  if (!forcePortal && s.isComplete()) {
    // Unattended path (hourly wake / boot reconnect): plain STA join, never a
    // portal. WiFiManager's autoConnect falls back to a BLOCKING captive portal
    // when the saved credentials fail, and with no timeout that parks the battery
    // board in SoftAP (~100 mA) until the cell is flat the first time a wake
    // overlaps a router outage. Fail fast instead -- every caller has a working
    // retry/backoff branch for a false return.
    WiFi.mode(WIFI_STA);
    if (g_apChannel > 0) {
      // Fast path: static lease + directed join skips the channel scan and the
      // DHCP DISCOVER/OFFER/REQUEST/ACK round-trips.
      WiFi.config(IPAddress(g_apIp), IPAddress(g_apGw), IPAddress(g_apMask),
                  IPAddress(g_apDns ? g_apDns : g_apGw));
      WiFi.begin(s.ssid.c_str(), s.pass.c_str(), g_apChannel, g_apBssid);
      if (WiFi.waitForConnectResult(5000) == WL_CONNECTED) return true;
      // AP moved channel / lease is stale: forget the cache, restore DHCP
      // (0.0.0.0 re-enables the DHCP client) and re-learn via the full join.
      forgetAp();
      WiFi.disconnect(true, false);
      WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0),
                  IPAddress((uint32_t)0));
      WiFi.mode(WIFI_STA);
    }
    WiFi.begin(s.ssid.c_str(), s.pass.c_str());
    if (WiFi.waitForConnectResult(15000) != WL_CONNECTED) return false;
    rememberAp();
    return true;
  }

  // Attended path: first-boot provisioning or an explicit button-held
  // reconfigure. A human is present, so a blocking portal is right -- but bound
  // it so an abandoned portal cannot burn the battery for days. On timeout the
  // callers retry (e-paper: next wake; Long: restart).
  WiFiManager wm;
  wm.setConfigPortalTimeout(600);
  wm.setConnectTimeout(20);      // bound the post-submit join attempt too

  // Stan portalu wybiera tresc stron (stale PSTRYK_* z wm_strings_pl.h, ktory
  // WiFiManager.h dolacza zamiast wm_strings_en.h): firstRun = brak klucza API
  // (pierwsze uruchomienie albo konfiguracja urwana bez klucza), keyMissing =
  // siec juz zapisana, klucza brak (np. archaiczna przegladarka ominela
  // `required`), reszta = rekonfiguracja skonfigurowanego urzadzenia.
  const bool firstRun    = s.apiKey.length() == 0;
  const bool hasSavedNet = s.ssid.length() > 0;
  const bool keyMissing  = firstRun && hasSavedNet;

  // Strona glowna = polskie intro + jeden przycisk "Dalej": token "custom"
  // wstawia _customMenuHTML, "wifi" = HTTP_PORTAL_MENU[0]. Reszty menu
  // biblioteki (Info/Exit/Update) nietechniczny uzytkownik nie potrzebuje, a
  // tylko go myli. setCustomMenuHTML/setCustomHeadElement trzymaja WSKAZNIK,
  // wiec `intro` musi zyc do konca startConfigPortal -- stad String w tej
  // ramce stosu. SSID w INTRO_RECONF laduje w HTML, wiec przechodzi przez
  // htmlEntities, tak jak biblioteka traktuje go w swoich statusach.
  String intro;
  if (firstRun) {
    intro = FPSTR(keyMissing ? PSTRYK_HTML_INTRO_KEYMISSING
                             : PSTRYK_HTML_INTRO_FIRST);
  } else {
    intro = FPSTR(PSTRYK_HTML_INTRO_RECONF);
    intro.replace("{v}", wm.htmlEntities(s.ssid));
  }
  wm.setTitle("Pstryk");
  const char* menu[] = {"custom", "wifi"};
  wm.setMenu(menu, 2);
  wm.setCustomMenuHTML(intro.c_str());
  // Bez SSID nie ma czego laczyc, a szablon HTTP_FORM_WIFI jest statyczny:
  // bez zapisanej sieci skrypt w <head> robi z pola sieci pole wymagane. Gdy
  // siec JEST zapisana (rekonfiguracja albo keyMissing), puste pola Wi-Fi
  // znacza "bez zmian" i obsluguje je tryb "tylko klucz" nizej -- wiec
  // skryptu tam nie ma.
  if (!hasSavedNet) wm.setCustomHeadElement(PSTRYK_HTML_HEAD_FIRST);

  // Zapisany klucz celowo NIE jest wstawiany do formularza: strona podalaby go
  // kazdemu, kto dolaczy do portalu. Puste pole przy rekonfiguracji = zachowaj
  // obecny klucz (mowia o tym podpowiedz i placeholder), wiec zmiana Wi-Fi
  // nigdy nie wymusza przepisywania klucza -- ale tylko wtedy, gdy jakis klucz
  // JEST. Przy pierwszym uruchomieniu pole jest wymagane: atrybut `required`
  // blokuje wysylke bez klucza, a podpowiedz mowi, skad go wziac. Wczesniej
  // mowila o tym tylko etykieta i uzytkownik, ktory wpisal samo Wi-Fi, ladowal
  // na urzadzeniu zapisanym-ale-niekompletnym bez slowa, czego brakuje.
  // Dwa parametry, dodane raz, w tej ramce stosu (biblioteka trzyma wskazniki):
  // podpowiedz jako sam HTML (getID()==NULL -> wstawiana doslownie) i pole
  // "apikey". Etykieta to samo "Klucz API": naglowek sekcji daje
  // HTTP_FORM_PARAM_HEAD, a dopisek "(wymagane)" dokleja CSS z `required`.
  WiFiManagerParameter keyHint(firstRun ? PSTRYK_HTML_KEY_HINT_FIRST
                                        : PSTRYK_HTML_KEY_HINT_RECONF);
  WiFiManagerParameter apiKeyParam("apikey", "Klucz API", "", 80,
                                   firstRun ? PSTRYK_ATTR_KEY_FIRST
                                            : PSTRYK_ATTR_KEY_RECONF);
  wm.addParameter(&keyHint);
  wm.addParameter(&apiKeyParam);

  // Tryb "tylko klucz" / "nic nie zmieniam" przy rekonfiguracji. WiFiManager z
  // pustymi polami Wi-Fi pomija zapis ("No ssid, skipping wifi save") i trzyma
  // portal otwarty do timeoutu (10 min) -- zmiana samego klucza byla
  // niewykonalna bez przepisania hasla Wi-Fi. Callback zapisu parametrow biegnie
  // w handleWifiSave (doParamSave, juz po odczytaniu pol), wiec zamyka portal
  // sam: stopConfigPortal() na portalu blokujacym ustawia `abort`, po czym
  // startConfigPortal konczy sie false -- stad wlasna flaga, zeby odroznic to
  // od timeoutu. Tylko gdy OBA pola Wi-Fi sa puste: puste s + niepuste p to dla
  // biblioteki zmiana hasla do zapisanej sieci i sama probuje polaczenia. Bez
  // zapisanej sieci nie ma czego zamykac (`required` blokuje pusty formularz,
  // a bez sieci i tak nie byloby sukcesu). Z zapisana siecia i brakiem klucza
  // (keyMissing) tryb dziala jak przy rekonfiguracji: wpisany sam klucz
  // domyka konfiguracje -- jedyna droga, gdy przegladarka ominela `required`.
  bool closedByForm = false;
  wm.setSaveParamsCallback([&]() {
    if (hasSavedNet && wm.server->arg(F("s")).isEmpty() &&
        wm.server->arg(F("p")).isEmpty()) {
      closedByForm = true;
      wm.stopConfigPortal();
    }
  });

  bool ok = wm.startConfigPortal(portalSsid(), portalPassword());

  // trim(): klucz wklejony z telefonu czesto niesie spacje albo nowa linie na
  // koncu, a API odrzuciloby taki naglowek jako zly klucz -- bez sladu w
  // formularzu, ktory wygladal poprawnie.
  String entered = apiKeyParam.getValue();
  entered.trim();
  if (ok) {
    // Capture whatever WiFiManager negotiated/entered.
    s.ssid = WiFi.SSID();
    s.pass = WiFi.psk();
  }
  // Klucz trafia do `s` TYLKO razem z zapisem. Po timeoucie (ok=false,
  // closedByForm=false) apiKeyParam nadal trzyma klucz z formularza, ktory
  // polegl na hasle Wi-Fi (doParamSave kopiuje go przed proba polaczenia);
  // przypisanie do RAM bez save() rozjezdzaloby Settings z NVS: e-papier bral
  // isComplete() za dobra monete (zerowal pule, spal 2 s, po wybudzeniu
  // otwieral portal bez nadzoru), Long uzywal nowego klucza do restartu.
  if (ok || closedByForm) {
    if (entered.length() > 0) s.apiKey = entered;
    s.save();
  }
  if (!ok && closedByForm && s.isComplete()) {
    // Portal zamkniety z formularza: biblioteka nie laczyla (Wi-Fi bez zmian),
    // a shutdownConfigPortal przywraca tylko tryb STA, bez asocjacji. Dolaczamy
    // do zapisanej sieci ta sama procedura co sciezka bezobslugowa (zwykly
    // join, 15 s). mode() stoi tu jawnie, bo tryb po portalu to wewnetrzny
    // domysl biblioteki (_usermode), nie nasza decyzja.
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.ssid.c_str(), s.pass.c_str());
    ok = WiFi.waitForConnectResult(15000) == WL_CONNECTED;
  }
  if (ok) rememberAp();          // the portal may have joined a different AP
  // isComplete() matters as much as the association: a portal that produced Wi-Fi
  // credentials but no API key is not a usable configuration, and reporting it as
  // success made the caller treat the board as provisioned.
  return ok && WiFi.status() == WL_CONNECTED && s.isComplete();
}

}  // namespace pstryk
