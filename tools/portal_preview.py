#!/usr/bin/env python3
"""Odtwarza offline strony captive portalu Pstryk-Setup z src/net/wm_strings_pl.h.

Skleja szablony dokładnie w kolejności, w jakiej robi to WiFiManager.cpp
(getHTTPHead -> handleRoot / handleWifi / handleWifiSave, getScanItemOut,
getParamOut, reportStatus), z tymi samymi podstawieniami tokenów -- więc zrzut
z Chrome pokazuje to, co zobaczy klient na telefonie, bez płytki i bez Wi-Fi.
Jeden plik HTML na stan:

  root-first       strona główna, pierwsze uruchomienie (brak SSID i klucza)
  root-keymissing  strona główna, SSID zapisany, klucza brak
  root-reconf      strona główna, rekonfiguracja (oba zapisane)
  root-failed      strona główna po błędnym haśle Wi-Fi (status D + OFFPW)
  wifi-first       /wifi, pierwsze uruchomienie: 3 udawane sieci, klucz required
  wifi-reconf      /wifi, rekonfiguracja: placeholdery SSID/hasła, klucz opcjonalny
  saved            strona po "Zapisz i połącz"
  paramsaved       strona po zapisie bez pól Wi-Fi (tryb "tylko klucz")

Użycie:
  python3 tools/portal_preview.py <outdir>               # tylko HTML
  python3 tools/portal_preview.py <outdir> --png         # + zrzuty 390x844 (Chrome headless)
  python3 tools/portal_preview.py <outdir> --png --tall  # + zrzuty pełnej wysokości (<stan>-full.png)

Zrzuty: Chrome (macOS, --headless=new) nie zejdzie poniżej 500 px szerokości
okna (innerWidth=500 nawet przy --window-size=390,...), więc <stan>.html jest
osadzany w <stan>.phone.html w <iframe> o szerokości 390 px i zrzut robi się z
okna 500 px (kadr telefonu na ciemnym tle; z Pillow -- przycięty do 390 px).
Chrome 151 po zapisaniu PNG nie kończy procesu (updater), dlatego skrypt czeka
na plik i sam zamyka przeglądarkę.

Parser czyta plik nagłówkowy jak preprocesor C (aktywne gałęzie: ESP32; bez
ESP8266 / WM_JSTEST / WM_NOHELP), skleja sąsiadujące literały i dekoduje
sekwencje \\" \\n \\\\ -- nie trzeba niczego kompilować.
"""
import argparse
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STRINGS = os.path.join(ROOT, "src", "net", "wm_strings_pl.h")
DEFINES = {"ESP32"}
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

# Stan "urządzenia" w podglądzie.
SSID = "DomowaSiec"
NETWORKS = [  # (nazwa, jakość %, zabezpieczona)
    ("DomowaSiec", 92, True),
    ("Sasiedzi_5G", 61, True),
    ("Kawiarnia Free", 34, False),
]


# --- czytanie nagłówka C ------------------------------------------------------

def preprocess(src, defines):
    """Usuwa komentarze i nieaktywne gałęzie #ifdef/#ifndef/#elif/#else/#endif."""
    out, stack, active = [], [], True
    i, n, line_start = 0, len(src), True
    while i < n:
        if line_start:
            j = i
            while j < n and src[j] in " \t":
                j += 1
            if j < n and src[j] == "#":
                k = src.find("\n", j)
                k = n if k < 0 else k
                parts = src[j + 1:k].split()
                kw = parts[0] if parts else ""
                arg = parts[1] if len(parts) > 1 else ""
                if kw == "ifdef":
                    stack.append([active, arg in defines])
                    active = active and arg in defines
                elif kw == "ifndef":
                    stack.append([active, arg not in defines])
                    active = active and arg not in defines
                elif kw == "elif":
                    parent, taken = stack[-1]
                    name = arg.replace("defined", "").strip("() ")
                    cond = (name in defines) and not taken
                    stack[-1][1] = taken or cond
                    active = parent and cond
                elif kw == "else":
                    parent, taken = stack[-1]
                    stack[-1][1] = True
                    active = parent and not taken
                elif kw == "endif":
                    active = stack.pop()[0]
                elif kw == "define" and active:
                    defines.add(arg)
                i = k
                continue
            line_start = False
        ch = src[i]
        if ch == "\n":
            line_start = True
            out.append(ch)
            i += 1
        elif src.startswith("//", i):
            k = src.find("\n", i)
            i = n if k < 0 else k
        elif src.startswith("/*", i):
            k = src.find("*/", i + 2)
            i = n if k < 0 else k + 2
        elif ch == '"':
            k = i + 1
            while k < n and src[k] != '"':
                k += 2 if src[k] == "\\" else 1
            if active:
                out.append(src[i:k + 1])
            i = k + 1
        else:
            if active:
                out.append(ch)
            i += 1
    return "".join(out)


def decode(body):
    """Dekoduje sekwencje ucieczki literału C (bez otaczających cudzysłowów)."""
    out, i, n = [], 0, len(body)
    while i < n:
        c = body[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        i += 1
        e = body[i]
        if e == "n":
            out.append("\n")
        elif e == "t":
            out.append("\t")
        elif e == "r":
            out.append("\r")
        elif e == "x":
            j = i + 1
            while j < n and body[j] in "0123456789abcdefABCDEF":
                j += 1
            out.append(chr(int(body[i + 1:j], 16)))
            i = j - 1
        elif e in "01234567":
            j = i
            while j < n and j < i + 3 and body[j] in "01234567":
                j += 1
            out.append(chr(int(body[i:j], 8)))
            i = j - 1
        else:  # \" \\ \' \?
            out.append(e)
        i += 1
    return "".join(out)


def tokenize(text):
    """Tokeny: ('str', wartość), ('id', nazwa), ('p', znak)."""
    toks, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif c == '"':
            k = i + 1
            while text[k] != '"':
                k += 2 if text[k] == "\\" else 1
            toks.append(("str", decode(text[i + 1:k])))
            i = k + 1
        elif c.isalpha() or c == "_":
            k = i
            while k < n and (text[k].isalnum() or text[k] == "_"):
                k += 1
            toks.append(("id", text[i:k]))
            i = k
        else:
            toks.append(("p", c))
            i += 1
    return toks


def load_strings(path=STRINGS, defines=None):
    """Zwraca {nazwa: str} dla `const char X[] PROGMEM = "..."` oraz
    {nazwa: [str, ...]} dla `const char * const X[] PROGMEM = {...}`."""
    with open(path, encoding="utf-8") as f:
        toks = tokenize(preprocess(f.read(), set(DEFINES if defines is None else defines)))
    out, i = {}, 0
    while i < len(toks):
        if toks[i] == ("id", "const") and toks[i + 1] == ("id", "char"):
            j = i + 2
            if toks[j] == ("p", "*"):
                j += 2  # "* const"
            name = toks[j][1]
            j += 1
            while toks[j] != ("p", "="):
                j += 1
            j += 1
            if toks[j] == ("p", "{"):
                items, cur = [], []
                j += 1
                while toks[j] != ("p", "}"):
                    if toks[j][0] == "str":
                        cur.append(toks[j][1])
                    elif toks[j] == ("p", ","):
                        items.append("".join(cur))
                        cur = []
                    j += 1
                if cur:
                    items.append("".join(cur))
                out[name] = items
            else:
                cur = []
                while toks[j] != ("p", ";"):
                    if toks[j][0] == "str":
                        cur.append(toks[j][1])
                    j += 1
                out[name] = "".join(cur)
            i = j
        i += 1
    return out


# --- składanie stron jak WiFiManager.cpp -------------------------------------

def html_entities(s, whitespace=False):  # WiFiManager::htmlEntities
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("'", "&#39;")
    return s.replace(" ", "&#160;") if whitespace else s


def http_head(S, title, custom_head=""):
    # getHTTPHead: HEAD_START({v}=tytuł) + SCRIPT + STYLE + _customHeadElement + HEAD_END.
    # {c} (klasa body) zostaje dosłownie -- biblioteka podstawia je tylko przy setClass().
    return (S["HTTP_HEAD_START"].replace("{v}", title) + S["HTTP_SCRIPT"] + S["HTTP_STYLE"]
            + custom_head + S["HTTP_HEAD_END"])


def status_none(S):
    return S["HTTP_STATUS_NONE"]


def status_off(S, ssid, cls="", reason=""):
    return S["HTTP_STATUS_OFF"].replace("{v}", html_entities(ssid)).replace("{c}", cls).replace("{r}", reason)


def scan_items(S, networks):  # getScanItemOut, _scanDispOptions=false, _minimumQuality=-1
    if not networks:
        return S["S_nonetworks"] + "<br/><br/>"
    tpl = S["HTTP_ITEM"]
    tpl = tpl.replace("{qp}", S["HTTP_ITEM_QP"]).replace("{h}", "h")  # procenty ukryte
    tpl = tpl.replace("{qi}", S["HTTP_ITEM_QI"]).replace("{h}", "")   # ikony widoczne
    page = ""
    for ssid, perc, enc in sorted(networks, key=lambda t: -t[1]):
        item = tpl.replace("{V}", html_entities(ssid)).replace("{v}", html_entities(ssid, True))
        item = item.replace("{r}", str(perc))
        item = item.replace("{q}", str(perc * 3 // 100 + 1))  # map(perc,0,100,1,4), arytmetyka całkowita
        item = item.replace("{i}", "l" if enc else "")
        page += item
    return page + S["HTTP_BR"]


def param_input(S, pid, label, length, value, attrs, index):  # getParamOut, WFM_LABEL_BEFORE
    item = S["HTTP_FORM_LABEL"] + S["HTTP_FORM_PARAM"]
    item = item.replace("{I}", S["S_parampre"] + str(index))
    item = item.replace("{i}", pid).replace("{n}", pid)
    item = item.replace("{p}", "{t}").replace("{t}", label)
    return item.replace("{l}", str(length)).replace("{v}", value).replace("{c}", attrs)


def page_root(S, intro, status, custom_head=""):  # handleRoot, menu {"custom","wifi"}
    title = S["S_brand"]
    page = http_head(S, title, custom_head)
    page += S["HTTP_ROOT_MAIN"].replace("{t}", title).replace("{v}", "Pstryk-Setup")
    page += S["HTTP_PORTAL_OPTIONS"]
    page += intro + S["HTTP_PORTAL_MENU"][0]
    return page + status + S["HTTP_END"]


def page_wifi(S, ssid, pass_ph, hint, attrs, status, custom_head="", networks=NETWORKS):  # handleWifi(scan=true)
    page = http_head(S, S["S_titlewifi"], custom_head)
    page += scan_items(S, networks)
    page += S["HTTP_FORM_START"].replace("{v}", "wifisave")
    page += S["HTTP_FORM_WIFI"].replace("{v}", ssid).replace("{p}", pass_ph)
    # getStaticOut() -> "" (bez pól statycznego IP)
    page += S["HTTP_FORM_WIFI_END"]
    page += S["HTTP_FORM_PARAM_HEAD"]
    page += hint  # WiFiManagerParameter(const char* custom): getID()==NULL -> dosłownie
    page += param_input(S, "apikey", "Klucz API", 80, "", attrs, index=1)
    page += S["HTTP_FORM_END"] + S["HTTP_SCAN_LINK"]
    return page + status + S["HTTP_END"]


def page_saved(S):  # handleWifiSave, _ssid != ""
    return http_head(S, S["S_titlewifisaved"]) + S["HTTP_SAVED"] + S["HTTP_END"]


def page_paramsaved(S):  # handleWifiSave, _ssid == "" (tryb "tylko klucz")
    return http_head(S, S["S_titlewifisettings"]) + S["HTTP_PARAMSAVED"] + S["HTTP_END"]


def build_states(S):
    head_first = S["PSTRYK_HTML_HEAD_FIRST"]  # setCustomHeadElement tylko przy pierwszym uruchomieniu
    intro_reconf = S["PSTRYK_HTML_INTRO_RECONF"].replace("{v}", html_entities(SSID))
    return {
        "root-first": page_root(S, S["PSTRYK_HTML_INTRO_FIRST"], status_none(S), head_first),
        "root-keymissing": page_root(S, S["PSTRYK_HTML_INTRO_KEYMISSING"], status_off(S, SSID), head_first),
        "root-reconf": page_root(S, intro_reconf, status_off(S, SSID)),
        "root-failed": page_root(S, S["PSTRYK_HTML_INTRO_FIRST"],
                                 status_off(S, SSID, "D", S["HTTP_STATUS_OFFPW"]), head_first),
        "wifi-first": page_wifi(S, "", "", S["PSTRYK_HTML_KEY_HINT_FIRST"], S["PSTRYK_ATTR_KEY_FIRST"],
                                status_none(S), head_first),
        "wifi-reconf": page_wifi(S, SSID, S["S_passph"], S["PSTRYK_HTML_KEY_HINT_RECONF"],
                                 S["PSTRYK_ATTR_KEY_RECONF"], status_off(S, SSID)),
        "saved": page_saved(S),
        "paramsaved": page_paramsaved(S),
    }


PHONE_W = 390
CHROME_MIN_W = 500  # minimalna szerokość okna Chrome na macOS (--headless=new)


def host_page(name, height):
    return ("<!DOCTYPE html><html><head><meta charset='UTF-8'><style>"
            "html,body{margin:0;background:#2b2b2b}"
            "iframe{display:block;width:%dpx;height:%dpx;margin:0 auto;border:0;background:#fff}"
            "</style></head><body><iframe src='%s.html'></iframe></body></html>" % (PHONE_W, height, name))


def screenshot(html, png, height, profile, timeout=30.0):
    """Zrzut okna CHROME_MIN_W x height; kończy Chrome, gdy tylko PNG się pojawi."""
    import time
    if os.path.exists(png):
        os.remove(png)
    proc = subprocess.Popen([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                             "--no-first-run", "--no-default-browser-check",
                             "--user-data-dir=" + profile, "--window-size=%d,%d" % (CHROME_MIN_W, height),
                             "--screenshot=" + png, "file://" + html],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    try:
        while time.time() - t0 < timeout:
            if os.path.getsize(png) > 0 if os.path.exists(png) else False:
                time.sleep(0.5)  # niech Chrome domknie plik
                break
            if proc.poll() is not None:
                break
            time.sleep(0.2)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()
    if not os.path.exists(png):
        sys.exit("Chrome nie zapisał %s" % png)
    try:  # opcjonalnie: przytnij kadr do szerokości telefonu
        from PIL import Image
        im = Image.open(png)
        x0 = (im.width - PHONE_W) // 2
        im.crop((x0, 0, x0 + PHONE_W, im.height)).save(png)
    except ImportError:
        pass


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--strings", default=STRINGS, help="plik nagłówkowy (domyślnie src/net/wm_strings_pl.h)")
    ap.add_argument("--png", action="store_true", help="zrzuty 390x844 przez Chrome headless")
    ap.add_argument("--tall", action="store_true", help="dodatkowo zrzuty 390x1800 (<stan>-full.png)")
    a = ap.parse_args()

    S = load_strings(a.strings)
    os.makedirs(a.outdir, exist_ok=True)
    states = build_states(S)
    for name, html in states.items():
        path = os.path.join(a.outdir, name + ".html")
        with open(path, "w", encoding="utf-8") as f:
            f.write(html)
        print(path)
    if not a.png:
        return
    if not os.path.exists(CHROME):
        sys.exit("brak Chrome: " + CHROME)
    profile = os.path.join(a.outdir, ".chrome")  # własny profil: równoległe Chrome'y się nie blokują
    sizes = [("", 844)] + ([("-full", 1800)] if a.tall else [])
    for name in states:
        for suffix, height in sizes:
            host = os.path.abspath(os.path.join(a.outdir, name + suffix + ".phone.html"))
            with open(host, "w", encoding="utf-8") as f:
                f.write(host_page(name, height))
            png = os.path.join(a.outdir, name + suffix + ".png")
            screenshot(host, png, height, profile)
            print(png)


if __name__ == "__main__":
    main()
