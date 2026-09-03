/**
 * wm_strings_pl.h
 * Polskie teksty, szablony HTML i styl captive portalu "Pstryk-Setup" dla
 * biblioteki WiFiManager (tzapu, 2.0.17). Dołączany zamiast wm_strings_en.h
 * przez build-flagę -DWM_STRINGS_FILE="net/wm_strings_pl.h" (platformio.ini,
 * obie płytki).
 *
 * Zasady (spec: docs/superpowers/specs/2026-09-02-setup-ux-polish-portal-and-button-cue-design.md):
 *  - każdy symbol z wm_strings_en.h istnieje tutaj pod tą samą nazwą i typem,
 *    bo biblioteka używa ich wszystkich; symbole własne portalu mają prefiks
 *    PSTRYK_ i są używane wyłącznie przez src/net/WiFiProvisioner.cpp;
 *  - tokeny {v} {p} {i} {n} {l} {c} {t} {r} {q} {h} {V} {I} {1} {2} stoją tam,
 *    gdzie WiFiManager.cpp je podstawia (String::replace zastępuje WSZYSTKIE
 *    wystąpienia), więc w polskich tekstach nie wolno używać nawiasów
 *    klamrowych;
 *  - tools/portal_preview.py odtwarza z tego pliku strony portalu offline,
 *    dlatego cały HTML/CSS portalu jest tutaj, a nie w kodzie C++;
 *  - portal nie ma internetu: zero zasobów zewnętrznych (fonty, CDN, obrazki
 *    poza wbudowanymi sprite'ami base64);
 *  - forma "Ty", diakrytyki (to przeglądarka, nie e-papier), ton instrukcji
 *    dla nietechnicznego klienta.
 *
 * Na podstawie wm_strings_en.h (c) tzapu, tablatronix -- licencja MIT.
 */

#ifndef _WM_STRINGS_PL_H_
#define _WM_STRINGS_PL_H_


#ifndef WIFI_MANAGER_OVERRIDE_STRINGS
// !!! ABOVE WILL NOT WORK if you define in your sketch, must be build flag, if anyone one knows how to order includes to be able to do this it would be neat.. I have seen it done..

// strings files must include a consts file!
#include "wm_consts_en.h" // include constants, tokens, routes

const char WM_LANGUAGE[] PROGMEM = "pl-PL"; // i18n lang code

// <meta charset> jako pierwszy element <head>: polskie znaki mają być
// zdekodowane, zanim przeglądarka zobaczy pierwszy tekst.
const char HTTP_HEAD_START[]       PROGMEM = "<!DOCTYPE html>"
"<html lang='pl'><head>"
"<meta charset='UTF-8'>"
"<meta name='format-detection' content='telephone=no'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'/>"
"<title>{v}</title>";

// Skrypt biblioteki bez zmian: c(l) wpisuje SSID z klikniętego wiersza do #s
// i włącza/wyłącza #p zależnie od klasy 'l' (sieć zabezpieczona) sąsiedniej
// ikony; f() pokazuje/ukrywa hasło.
const char HTTP_SCRIPT[]           PROGMEM = "<script>function c(l){"
"document.getElementById('s').value=l.getAttribute('data-ssid')||l.innerText||l.textContent;"
"p = l.nextElementSibling.classList.contains('l');"
"document.getElementById('p').disabled = !p;"
"if(p)document.getElementById('p').focus();};"
"function f() {var x = document.getElementById('p');x.type==='password'?x.type='text':x.type='password';}"
"</script>";

// Wspólna rama każdej strony: pasek z wordmarkiem (tekst, bez grafik) i
// wycentrowany .wrap. {c} = klasa body (_bodyClass) -- zostaje dla biblioteki.
const char HTTP_HEAD_END[]         PROGMEM = "</head><body class='{c}'>"
"<header class='bar'><span class='brand'>Pstryk</span><span class='tag'>konfiguracja urządzenia</span></header>"
"<div class='wrap'>";

// Wordmark jest już w ramie, a nazwa hosta / IP nic nie mówi klientowi.
const char HTTP_ROOT_MAIN[]        PROGMEM = "";

// Menu: provisioner ustawia {"custom","wifi"}, więc na stronie głównej jest
// tylko intro (custom) i przycisk [0]. Reszta wpisów musi istnieć (biblioteka
// indeksuje tablicę), ale nie jest pokazywana. Kolejność jak w oryginale.
const char * const HTTP_PORTAL_MENU[] PROGMEM = {
"<form action='/wifi'    method='get'><button>Dalej</button></form>\n", // MENU_WIFI
"<form action='/0wifi'   method='get'><button>Wi-Fi bez skanowania</button></form>\n", // MENU_WIFINOSCAN
"<form action='/info'    method='get'><button class='sec'>Informacje</button></form>\n", // MENU_INFO
"<form action='/param'   method='get'><button class='sec'>Ustawienia</button></form>\n",//MENU_PARAM
"<form action='/close'   method='get'><button class='sec'>Zamknij</button></form>\n", // MENU_CLOSE
"<form action='/restart' method='get'><button class='sec'>Uruchom ponownie</button></form>\n",// MENU_RESTART
"<form action='/exit'    method='get'><button class='sec'>Wyjdź</button></form>\n",  // MENU_EXIT
"<form action='/erase'   method='get'><button class='D'>Wymaż ustawienia Wi-Fi</button></form>\n", // MENU_ERASE
"<form action='/update'  method='get'><button class='sec'>Aktualizuj oprogramowanie</button></form>\n",// MENU_UPDATE
"<hr>" // MENU_SEP
};

const char HTTP_PORTAL_OPTIONS[]   PROGMEM = "";
// Ikony sygnału i kłódki bez zmian (klasy q-{q}, l, h obsługuje CSS niżej).
const char HTTP_ITEM_QI[]          PROGMEM = "<div role='img' aria-label='{r}%' title='{r}%' class='q q-{q} {i} {h}'></div>"; // rssi icons
const char HTTP_ITEM_QP[]          PROGMEM = "<div class='q {h}'>{r}%</div>"; // rssi percentage {h} = hidden showperc pref
// Wiersz listy sieci: link wypełnia cały wiersz (duży cel dotyku), ikony {qi}{qp}
// muszą stać bezpośrednio za <a> -- c(l) czyta klasę 'l' z nextElementSibling.
const char HTTP_ITEM[]             PROGMEM = "<div class='net'><a href='#p' onclick='c(this)' data-ssid='{V}'>{v}</a>{qi}{qp}</div>"; // {q} = HTTP_ITEM_QI, {r} = HTTP_ITEM_QP

const char HTTP_FORM_START[]       PROGMEM = "<form method='POST' action='{v}'>";
// Sekcja Wi-Fi: id/name pól (s, p, showpass) i onclick='f()' bez zmian;
// {v} = bieżący SSID, {p} = maska hasła (S_passph) albo pusto.
const char HTTP_FORM_WIFI[]        PROGMEM =
"<h2 class='sec'>Sieć Wi-Fi</h2>"
"<p class='hint'>Wybierz sieć z listy powyżej albo wpisz jej nazwę.</p>"
"<label for='s'>Nazwa sieci</label>"
"<input id='s' name='s' maxlength='32' autocorrect='off' autocapitalize='none' placeholder='{v}'>"
"<label for='p'>Hasło Wi-Fi</label>"
"<input id='p' name='p' maxlength='64' type='password' placeholder='{p}'>"
"<label class='chk' for='showpass'><input type='checkbox' id='showpass' onclick='f()'>Pokaż hasło</label>";
const char HTTP_FORM_WIFI_END[]    PROGMEM = "";
const char HTTP_FORM_STATIC_HEAD[] PROGMEM = "<h2 class='sec'>Adres IP (opcjonalnie)</h2><p class='hint'>Zostaw puste, aby urządzenie pobrało adres automatycznie.</p>";
const char HTTP_FORM_END[]         PROGMEM = "<div class='act'><button type='submit'>Zapisz i połącz</button></div></form>";
const char HTTP_FORM_LABEL[]       PROGMEM = "<label for='{i}'>{t}</label>";
const char HTTP_FORM_PARAM_HEAD[]  PROGMEM = "<h2 class='sec'>Klucz API Pstryk</h2>";
// {c} = atrybuty pola (PSTRYK_ATTR_KEY_*). Bez <br/> (odstępy robi CSS), z końcowym \n.
const char HTTP_FORM_PARAM[]       PROGMEM = "<input id='{i}' name='{n}' maxlength='{l}' value='{v}' {c}>\n"; // do not remove newline!

const char HTTP_SCAN_LINK[]        PROGMEM = "<form action='/wifi?refresh=1' method='POST'><button name='refresh' value='1' class='sec'>Odśwież listę sieci</button></form>";
// Po wysłaniu formularza z SSID (także zmiana samego hasła do zapisanej sieci).
const char HTTP_SAVED[]            PROGMEM =
"<div class='msg S'><strong>Zapisano.</strong><br/>Pstryk łączy się teraz z Twoją siecią <span class='nw'>Wi-Fi…</span></div>"
"<p>Za chwilę na ekranie urządzenia pojawią się ceny prądu.</p>"
"<p class='hint'>Jeśli po minucie urządzenie nadal pokazuje ekran konfiguracji, wróć do tej strony (192.168.4.1) albo połącz się ponownie z siecią Pstryk-Setup — zobaczysz, co poszło nie tak.</p>";
// Po wysłaniu bez pól Wi-Fi (tryb "tylko klucz" -- provisioner zamyka portal
// z callbacku i dołącza do zapisanej sieci).
const char HTTP_PARAMSAVED[]       PROGMEM = "<div class='msg S'><strong>Zapisano.</strong><br/>Urządzenie łączy się z zapisaną siecią Wi-Fi.</div>";
const char HTTP_END[]              PROGMEM = "</div></body></html>";
const char HTTP_ERASEBTN[]         PROGMEM = "<form action='/erase' method='get'><button class='D'>Wymaż ustawienia Wi-Fi</button></form>";
const char HTTP_UPDATEBTN[]        PROGMEM = "<form action='/update' method='get'><button class='sec'>Aktualizuj oprogramowanie</button></form>";
const char HTTP_BACKBTN[]          PROGMEM = "<hr><form action='/' method='get'><button class='sec'>Wróć</button></form>";

// Status pod stroną główną i /wifi (reportStatus). {c} = '' albo 'D',
// {v} = zapisany SSID, {r} = jeden z HTTP_STATUS_OFF*, {i} = adres IP.
const char HTTP_STATUS_ON[]        PROGMEM = "<div class='msg S'><strong>Połączono z {v}</strong><br/><small>adres IP {i}</small></div>";
const char HTTP_STATUS_OFF[]       PROGMEM = "<div class='msg {c}'>Urządzenie nie jest teraz połączone z siecią <b>{v}</b>.{r}</div>"; // {c=class} {v=ssid} {r=status_off}
const char HTTP_STATUS_OFFPW[]     PROGMEM = "<br/>Błędne hasło Wi-Fi. Wpisz hasło jeszcze raz."; // STATION_WRONG_PASSWORD,  no eps32
const char HTTP_STATUS_OFFNOAP[]   PROGMEM = "<br/>Nie znaleziono sieci. Sprawdź, czy router jest włączony i w zasięgu.";   // WL_NO_SSID_AVAIL
const char HTTP_STATUS_OFFFAIL[]   PROGMEM = "<br/>Nie udało się połączyć. Spróbuj jeszcze raz."; // WL_CONNECT_FAILED
const char HTTP_STATUS_NONE[]      PROGMEM = "<p class='hint status'>Brak zapisanej sieci Wi-Fi.</p>";
const char HTTP_BR[]               PROGMEM = "<br/>";

// Styl: mobile-first, font systemowy, 17 px bazowo, pola i przyciski >= 48 px,
// .wrap do 520 px. Przycisk główny czarny (jak e-papier), wtórny (.sec)
// konturowy, .D czerwony (kasowanie). Bez zasobów zewnętrznych.
const char HTTP_STYLE[]            PROGMEM = "<style>"
"*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}"
"body{margin:0;background:#f3f3f0;color:#111;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;font-size:17px;line-height:1.45}"
// rama
".bar{display:flex;align-items:baseline;justify-content:space-between;gap:12px;padding:14px 20px;background:#111;color:#fff}"
".brand{font-size:22px;font-weight:800;letter-spacing:.01em}.tag{font-size:14px;color:#b5b5b5}"
".wrap{max-width:520px;margin:0 auto;padding:22px 20px 40px}"
// typografia
"h1{margin:6px 0 14px;font-size:26px;line-height:1.2}"
"h2.sec{margin:30px 0 6px;font-size:19px;line-height:1.25}h3{margin:24px 0 6px;font-size:19px}"
"p{margin:0 0 12px}.hint{color:#555;font-size:15px}.status{margin-top:28px}.nw{white-space:nowrap}"
".steps{margin:0 0 14px;padding-left:24px}.steps li{margin:0 0 10px;padding-left:4px}"
"a{color:#111}hr{margin:26px 0;border:0;border-top:1px solid #d9d9d9}form{margin:0}"
// pola
"label{display:block;margin:14px 0 6px;font-weight:600}"
"label:has(+input:required)::after{content:' (wymagane)';font-weight:400;color:#b3261e}"
"input,select{display:block;width:100%;min-height:48px;margin:0;padding:10px 12px;font:inherit;color:#111;background:#fff;border:1.5px solid #8f8f8f;border-radius:8px}"
"input:focus,select:focus{border-color:#111;outline:3px solid #a7c8ff;outline-offset:1px}"
"input[type=checkbox]{display:inline-block;width:24px;height:24px;min-height:0;margin:0 10px 0 0;vertical-align:middle;accent-color:#111}"
".chk{display:flex;align-items:center;min-height:48px;margin:6px 0 0;font-weight:400}"
":disabled{opacity:.5}"
// przyciski
"button{display:block;width:100%;min-height:52px;margin:10px 0;padding:12px 16px;font:inherit;font-size:17px;font-weight:600;color:#fff;background:#111;border:2px solid #111;border-radius:10px;cursor:pointer}"
"button.sec{color:#111;background:#fff}button.D{background:#b3261e;border-color:#b3261e}"
"button:active{opacity:.7}button:focus-visible,a:focus-visible{outline:3px solid #1a73e8;outline-offset:2px}"
".act{margin-top:26px}.act button{min-height:56px;font-size:18px}"
// komunikaty: bez klasy = dyskretny status (biblioteka daje {c}='' gdy nie ma powodu), S = sukces, D = błąd, P = informacja
".msg{margin:22px 0 0;padding:4px 0 4px 12px;color:#555;font-size:15px;border-left:3px solid #bbb}"
".msg.S,.msg.D,.msg.P{margin:18px 0;padding:14px 16px;color:#111;font-size:17px;background:#fff;border:1px solid #ddd;border-left-width:6px;border-radius:8px}"
".msg.S{border-left-color:#2e7d32}.msg.D{border-left-color:#c62828}.msg.P{border-left-color:#1565c0}"
".msg.S strong{color:#1b5e20}.msg.D strong{color:#b71c1c}.msg.P strong{color:#0d47a1}"
// lista sieci: cały wiersz to link, ikona sygnału po prawej nie przechwytuje dotyku
".wrap:has(>.net:first-child)::before{content:'Sieci Wi-Fi w pobliżu';display:block;margin:0 0 10px;font-size:19px;font-weight:700}"
".net{position:relative;margin:0 0 8px}.net+br{display:none}"
".net a{display:block;min-height:52px;padding:13px 68px 13px 14px;font-weight:600;text-decoration:none;color:#111;background:#fff;border:1.5px solid #8f8f8f;border-radius:10px;word-break:break-word}"
".net a:active{background:#e6e6e6}"
".net .q{position:absolute;right:12px;top:50%;margin:0;transform:translateY(-50%);float:none;white-space:nowrap;pointer-events:none}"
// /info
"dl{margin:0}dt{margin-top:10px;font-weight:600}dd{margin:0;color:#444;word-break:break-all}progress{width:100%}"
".h{display:none}"
// quality icons (sprite'y i klasy z wm_strings_en.h -- dosłownie)
".q{height:16px;margin:0;padding:0 5px;text-align:right;min-width:38px;float:right}.q.q-0:after{background-position-x:0}.q.q-1:after{background-position-x:-16px}.q.q-2:after{background-position-x:-32px}.q.q-3:after{background-position-x:-48px}.q.q-4:after{background-position-x:-64px}.q.l:before{background-position-x:-80px;padding-right:5px}.ql .q{float:left}.q:after,.q:before{content:'';width:16px;height:16px;display:inline-block;background-repeat:no-repeat;background-position: 16px 0;"
"background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAAAQCAMAAADeZIrLAAAAJFBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADHJj5lAAAAC3RSTlMAIjN3iJmqu8zd7vF8pzcAAABsSURBVHja7Y1BCsAwCASNSVo3/v+/BUEiXnIoXkoX5jAQMxTHzK9cVSnvDxwD8bFx8PhZ9q8FmghXBhqA1faxk92PsxvRc2CCCFdhQCbRkLoAQ3q/wWUBqG35ZxtVzW4Ed6LngPyBU2CobdIDQ5oPWI5nCUwAAAAASUVORK5CYII=');}"
// icons @2x media query (32px rescaled)
"@media (-webkit-min-device-pixel-ratio: 2),(min-resolution: 192dpi){.q:before,.q:after {"
"background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAALwAAAAgCAMAAACfM+KhAAAALVBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAOrOgAAAADnRSTlMAESIzRGZ3iJmqu8zd7gKjCLQAAACmSURBVHgB7dDBCoMwEEXRmKlVY3L//3NLhyzqIqSUggy8uxnhCR5Mo8xLt+14aZ7wwgsvvPA/ofv9+44334UXXngvb6XsFhO/VoC2RsSv9J7x8BnYLW+AjT56ud/uePMdb7IP8Bsc/e7h8Cfk912ghsNXWPpDC4hvN+D1560A1QPORyh84VKLjjdvfPFm++i9EWq0348XXnjhhT+4dIbCW+WjZim9AKk4UZMnnCEuAAAAAElFTkSuQmCC');"
"background-size: 95px 16px;}}"
"</style>";

// Portal nie ma sekcji pomocy (lista tras nic nie mówi klientowi) -- pusta w
// obu gałęziach, symbol musi istnieć.
#ifndef WM_NOHELP
const char HTTP_HELP[]             PROGMEM = "";
#else
const char HTTP_HELP[]             PROGMEM = "";
#endif

const char HTTP_UPDATE[] PROGMEM = "<h2 class='sec'>Aktualizacja oprogramowania</h2><p class='hint'>Wybierz plik .bin z nową wersją i naciśnij Aktualizuj.</p><form method='POST' action='u' enctype='multipart/form-data' onchange=\"(function(el){document.getElementById('uploadbin').style.display = el.value=='' ? 'none' : 'initial';})(this)\"><input type='file' name='update' accept='.bin,application/octet-stream'><button id='uploadbin' type='submit' class='h D'>Aktualizuj</button></form><p class='hint'><a href='http://192.168.4.1/update' target='_blank'>* W oknie logowania do sieci wysyłanie pliku może nie działać — otwórz http://192.168.4.1/update w przeglądarce.</a></p>";
const char HTTP_UPDATE_FAIL[] PROGMEM = "<div class='msg D'><strong>Aktualizacja nie powiodła się.</strong><br/>Uruchom urządzenie ponownie i spróbuj jeszcze raz.</div>";
const char HTTP_UPDATE_SUCCESS[] PROGMEM = "<div class='msg S'><strong>Aktualizacja zakończona.</strong><br/>Urządzenie uruchamia się ponownie…</div>";

#ifdef WM_JSTEST
const char HTTP_JS[] PROGMEM =
"<script>function postAjax(url, data, success) {"
"    var params = typeof data == 'string' ? data : Object.keys(data).map("
"            function(k){ return encodeURIComponent(k) + '=' + encodeURIComponent(data[k]) }"
"        ).join('&');"
"    var xhr = window.XMLHttpRequest ? new XMLHttpRequest() : new ActiveXObject(\"Microsoft.XMLHTTP\");"
"    xhr.open('POST', url);"
"    xhr.onreadystatechange = function() {"
"        if (xhr.readyState>3 && xhr.status==200) { success(xhr.responseText); }"
"    };"
"    xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');"
"    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
"    xhr.send(params);"
"    return xhr;}"
"postAjax('/status', 'p1=1&p2=Hello+World', function(data){ console.log(data); });"
"postAjax('/status', { p1: 1, p2: 'Hello World' }, function(data){ console.log(data); });"
"</script>";
#endif

// Strona /info (nie ma jej w menu, ale biblioteka wymaga symboli). {1} {2} = wartości.
#ifdef ESP32
	const char HTTP_INFO_esphead[]    PROGMEM = "<h3>Układ ESP32</h3><hr><dl>";
	const char HTTP_INFO_chiprev[]    PROGMEM = "<dt>Rewizja układu</dt><dd>{1}</dd>";
  	const char HTTP_INFO_lastreset[]  PROGMEM = "<dt>Przyczyna ostatniego restartu</dt><dd>CPU0: {1}<br/>CPU1: {2}</dd>";
  	const char HTTP_INFO_aphost[]     PROGMEM = "<dt>Nazwa hosta punktu dostępu</dt><dd>{1}</dd>";
    const char HTTP_INFO_psrsize[]    PROGMEM = "<dt>Rozmiar PSRAM</dt><dd>{1} B</dd>";
	const char HTTP_INFO_temp[]       PROGMEM = "<dt>Temperatura</dt><dd>{1} &deg;C / {2} &deg;F</dd>";
    const char HTTP_INFO_hall[]       PROGMEM = "<dt>Czujnik Halla</dt><dd>{1}</dd>";
#else
	const char HTTP_INFO_esphead[]    PROGMEM = "<h3>Układ ESP8266</h3><hr><dl>";
	const char HTTP_INFO_fchipid[]    PROGMEM = "<dt>ID układu flash</dt><dd>{1}</dd>";
	const char HTTP_INFO_corever[]    PROGMEM = "<dt>Wersja core</dt><dd>{1}</dd>";
	const char HTTP_INFO_bootver[]    PROGMEM = "<dt>Wersja bootloadera</dt><dd>{1}</dd>";
	const char HTTP_INFO_lastreset[]  PROGMEM = "<dt>Przyczyna ostatniego restartu</dt><dd>{1}</dd>";
	const char HTTP_INFO_flashsize[]  PROGMEM = "<dt>Rzeczywisty rozmiar flash</dt><dd>{1} B</dd>";
#endif

const char HTTP_INFO_memsmeter[]  PROGMEM = "<br/><progress value='{1}' max='{2}'></progress></dd>";
const char HTTP_INFO_memsketch[]  PROGMEM = "<dt>Pamięć – rozmiar programu</dt><dd>Użyte / łącznie (bajty)<br/>{1} / {2}";
const char HTTP_INFO_freeheap[]   PROGMEM = "<dt>Pamięć – wolna sterta</dt><dd>{1} B</dd>";
const char HTTP_INFO_wifihead[]   PROGMEM = "<br/><h3>Wi-Fi</h3><hr>";
const char HTTP_INFO_uptime[]     PROGMEM = "<dt>Czas pracy</dt><dd>{1} min {2} s</dd>";
const char HTTP_INFO_chipid[]     PROGMEM = "<dt>ID układu</dt><dd>{1}</dd>";
const char HTTP_INFO_idesize[]    PROGMEM = "<dt>Rozmiar flash</dt><dd>{1} B</dd>";
const char HTTP_INFO_sdkver[]     PROGMEM = "<dt>Wersja SDK</dt><dd>{1}</dd>";
const char HTTP_INFO_cpufreq[]    PROGMEM = "<dt>Taktowanie CPU</dt><dd>{1} MHz</dd>";
const char HTTP_INFO_apip[]       PROGMEM = "<dt>Adres IP punktu dostępu</dt><dd>{1}</dd>";
const char HTTP_INFO_apmac[]      PROGMEM = "<dt>MAC punktu dostępu</dt><dd>{1}</dd>";
const char HTTP_INFO_apssid[]     PROGMEM = "<dt>Nazwa sieci punktu dostępu</dt><dd>{1}</dd>";
const char HTTP_INFO_apbssid[]    PROGMEM = "<dt>BSSID</dt><dd>{1}</dd>";
const char HTTP_INFO_stassid[]    PROGMEM = "<dt>Zapisana sieć Wi-Fi</dt><dd>{1}</dd>";
const char HTTP_INFO_staip[]      PROGMEM = "<dt>Adres IP urządzenia</dt><dd>{1}</dd>";
const char HTTP_INFO_stagw[]      PROGMEM = "<dt>Brama</dt><dd>{1}</dd>";
const char HTTP_INFO_stasub[]     PROGMEM = "<dt>Maska podsieci</dt><dd>{1}</dd>";
const char HTTP_INFO_dnss[]       PROGMEM = "<dt>Serwer DNS</dt><dd>{1}</dd>";
const char HTTP_INFO_host[]       PROGMEM = "<dt>Nazwa hosta</dt><dd>{1}</dd>";
const char HTTP_INFO_stamac[]     PROGMEM = "<dt>MAC urządzenia</dt><dd>{1}</dd>";
const char HTTP_INFO_conx[]       PROGMEM = "<dt>Połączono</dt><dd>{1}</dd>";
const char HTTP_INFO_autoconx[]   PROGMEM = "<dt>Automatyczne łączenie</dt><dd>{1}</dd>";

const char HTTP_INFO_aboutver[]     PROGMEM = "<dt>WiFiManager</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutarduino[] PROGMEM = "<dt>Arduino</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutsdk[]     PROGMEM = "<dt>ESP-SDK/IDF</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutdate[]    PROGMEM = "<dt>Data kompilacji</dt><dd>{1}</dd>";

const char S_brand[]              PROGMEM = "Pstryk";
const char S_debugPrefix[]        PROGMEM = "*wm:";
const char S_y[]                  PROGMEM = "Tak";
const char S_n[]                  PROGMEM = "Nie";
const char S_enable[]             PROGMEM = "Włączone";
const char S_disable[]            PROGMEM = "Wyłączone";
const char S_GET[]                PROGMEM = "GET";
const char S_POST[]               PROGMEM = "POST";
const char S_NA[]                 PROGMEM = "Nieznane";
const char S_passph[]             PROGMEM = "********";
const char S_titlewifisaved[]     PROGMEM = "Zapisano – Pstryk";
const char S_titlewifisettings[]  PROGMEM = "Zapisano ustawienia – Pstryk";
const char S_titlewifi[]          PROGMEM = "Wi-Fi i klucz API – Pstryk";
const char S_titleinfo[]          PROGMEM = "Informacje – Pstryk";
const char S_titleparam[]         PROGMEM = "Ustawienia – Pstryk";
const char S_titleparamsaved[]    PROGMEM = "Zapisano ustawienia – Pstryk";
const char S_titleexit[]          PROGMEM = "Zamykanie – Pstryk";
const char S_titlereset[]         PROGMEM = "Restart – Pstryk";
const char S_titleerase[]         PROGMEM = "Kasowanie – Pstryk";
const char S_titleclose[]         PROGMEM = "Zamknij – Pstryk";
const char S_options[]            PROGMEM = "Opcje – Pstryk";
// Wstawiane dosłownie zamiast listy (może zawierać HTML).
const char S_nonetworks[]         PROGMEM = "<h2 class='sec'>Sieci Wi-Fi w pobliżu</h2><p class='hint'>Nie znaleziono sieci Wi-Fi. Odśwież listę, aby przeskanować ponownie.</p>";
const char S_staticip[]           PROGMEM = "Stały adres IP";
const char S_staticgw[]           PROGMEM = "Brama";
const char S_staticdns[]          PROGMEM = "Serwer DNS";
const char S_subnet[]             PROGMEM = "Maska podsieci";
const char S_exiting[]            PROGMEM = "Zamykam portal konfiguracji.";
const char S_resetting[]          PROGMEM = "Urządzenie uruchomi się ponownie za kilka sekund.";
const char S_closing[]            PROGMEM = "Możesz zamknąć tę stronę – portal konfiguracji działa dalej.";
const char S_error[]              PROGMEM = "Wystąpił błąd.";
const char S_notfound[]           PROGMEM = "Nie znaleziono strony\n\n";
const char S_uri[]                PROGMEM = "URI: ";
const char S_method[]             PROGMEM = "\nMethod: ";
const char S_args[]               PROGMEM = "\nArguments: ";
const char S_parampre[]           PROGMEM = "param_";

// debug strings
const char D_HR[]                 PROGMEM = "--------------------";


// softap ssid default prefix
#ifdef ESP8266
    const char S_ssidpre[]        PROGMEM = "ESP";
#elif defined(ESP32)
    const char S_ssidpre[]        PROGMEM = "ESP32";
#else
    const char S_ssidpre[]        PROGMEM = "WM";
#endif

// END WIFI_MANAGER_OVERRIDE_STRINGS
#endif

// -----------------------------------------------------------------------------
// Stałe własne portalu Pstryk (używa ich wyłącznie src/net/WiFiProvisioner.cpp).
// Stan portalu wybiera provisioner: firstRun = brak klucza API,
// keyMissing = firstRun && zapisany SSID, reconf = oba zapisane.
// -----------------------------------------------------------------------------

// Intro strony głównej (setCustomMenuHTML) -- pierwsze uruchomienie.
const char PSTRYK_HTML_INTRO_FIRST[] PROGMEM =
"<h1>Cześć! Skonfigurujmy Pstryk.</h1>"
"<ol class='steps'>"
"<li><b>Wi-Fi</b> — wybierz swoją sieć i wpisz hasło.</li>"
"<li><b>Klucz API Pstryk</b> — wklej klucz z aplikacji Pstryk (Konto → Urządzenia i integracje).</li>"
"<li><b>Zapisz</b> — urządzenie samo połączy się i pobierze ceny prądu.</li>"
"</ol>"
"<p class='hint'>Zajmie to około 2 minut. Urządzenie najlepiej mieć w tym czasie na ładowarce.</p>";

// Intro -- sieć zapisana, klucza brak (np. archaiczna przeglądarka ominęła required).
const char PSTRYK_HTML_INTRO_KEYMISSING[] PROGMEM =
"<h1>Dokończmy konfigurację</h1>"
"<div class='msg D'><strong>Brakuje klucza API Pstryk.</strong><br/>Bez niego urządzenie nie pobierze cen. Sieć Wi-Fi jest już zapisana — wystarczy wpisać klucz i zapisać. Chcesz zmienić sieć? Wybierz ją z listy i podaj hasło.</div>";

// Intro -- rekonfiguracja. {v} = zapisany SSID; provisioner podstawia go
// String::replace'em do bufora żyjącego do końca portalu (setCustomMenuHTML
// trzyma wskaźnik). SSID wchodzi do HTML -- warto go przepuścić przez
// wm.htmlEntities() jak robi to sama biblioteka w statusach.
const char PSTRYK_HTML_INTRO_RECONF[] PROGMEM =
"<h1>Ustawienia Pstryk</h1>"
"<p>Zapisana sieć: <b>{v}</b>.</p>"
"<p>Możesz zmienić sieć Wi-Fi, jej hasło albo klucz API. Pola, których nie zmieniasz, zostaw puste.</p>";

// Podpowiedź nad polem klucza (parametr z samym HTML, getID()==NULL -> wstawiany dosłownie).
const char PSTRYK_HTML_KEY_HINT_FIRST[] PROGMEM =
"<p class='hint'>Znajdziesz go w aplikacji Pstryk: Konto → Urządzenia i integracje → Wygeneruj klucz. Zaczyna się od sk-. Pole jest wymagane.</p>";
const char PSTRYK_HTML_KEY_HINT_RECONF[] PROGMEM =
"<p class='hint'>Masz już zapisany klucz. Zostaw puste, aby go zachować.</p>";

// Atrybuty pola klucza ({c} w HTTP_FORM_PARAM). Wewnątrz wartości bez apostrofów.
const char PSTRYK_ATTR_KEY_FIRST[] PROGMEM =
"required autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' placeholder='sk-…'";
const char PSTRYK_ATTR_KEY_RECONF[] PROGMEM =
"autocomplete='off' autocapitalize='none' autocorrect='off' spellcheck='false' placeholder='zostaw puste = bez zmian'";

// <head> przy pierwszym uruchomieniu (setCustomHeadElement): bez SSID nie ma
// czego łączyć, a szablon HTTP_FORM_WIFI jest statyczny -- required na #s
// ustawia skrypt (przy rekonfiguracji puste = bez zmian, więc go nie ma).
const char PSTRYK_HTML_HEAD_FIRST[] PROGMEM =
"<script>document.addEventListener('DOMContentLoaded',function(){var s=document.getElementById('s');if(s)s.required=true;});</script>";

#endif
