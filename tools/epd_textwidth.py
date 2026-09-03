#!/usr/bin/env python3
"""Dokladna szerokosc napisu na e-papierze (T5 4.7"), liczona z tablic glifow
fontow UiSansBody / UiSansHead wkompilowanych w firmware (lib/EPD47/src/ui_font_*.h).

Zastepuje pesymistyczne szacunki "15 px/znak body, 22 px/znak head" ze spec
ekranow e-papieru: layout mozna domykac na liczbach, ktore zobaczy panel.

  python3 tools/epd_textwidth.py "Nacisnij i pusc" "1. przycisk od lewej"
  python3 tools/epd_textwidth.py --font body "Za chwile pokaze ustawienia."
  python3 tools/epd_textwidth.py --x 488 "Nacisnij i pusc"   # sprawdza prawy margines (W-M = 936)

Zwraca dwie liczby: `adv` = suma advance_x (pozycja kursora po napisie; tego
uzywa uklad dla kolejnego elementu) i `ink` = realny zasieg tuszu
(get_text_bounds w lib/EPD47/src/font.c), zwykle o 1-3 px mniejszy.
Fonty pokrywaja tylko ASCII 0x20-0x7E: znak spoza zakresu jest zglaszany,
bo na panelu po prostu zniknie (fallback_glyph = 0).
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = {
    "body": ("lib/EPD47/src/ui_font_body.h", "UiSansBody"),
    "head": ("lib/EPD47/src/ui_font_head.h", "UiSansHead"),
}
W, M = 960, 24

def load(kind):
    path, name = FONTS[kind]
    src = open(os.path.join(ROOT, path), encoding="utf-8", errors="replace").read()
    m = re.search(r"const GFXglyph %sGlyphs\[\] = \{(.*?)\n\};" % name, src, re.S)
    if not m: sys.exit("brak tablicy glifow %s w %s" % (name, path))
    glyphs = [tuple(int(v) for v in g.split(","))
              for g in re.findall(r"\{\s*([-\d]+\s*,\s*[-\d]+\s*,\s*[-\d]+\s*,\s*[-\d]+\s*,\s*[-\d]+\s*,\s*[-\d]+\s*,\s*[-\d]+)\s*\}", m.group(1))]
    iv = re.search(r"const UnicodeInterval %sIntervals\[\] = \{\s*\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(\d+)" % name, src)
    first, last = int(iv.group(1), 0), int(iv.group(2), 0)
    f = re.search(r"const GFXfont %s = \{(.*?)\};" % name, src, re.S).group(1)
    nums = [int(v) for v in re.findall(r"(?<![\w(])-?\d+(?![\w)])", f.split(name + "Intervals")[1])]
    # ..., interval_count, compressed, advance_y, ascender, descender
    advance_y, ascender, descender = nums[-3], nums[-2], nums[-1]
    return glyphs, first, last, advance_y, ascender, descender

def measure(text, glyphs, first, last):
    cursor, x1, x2, missing = 0, None, None, []
    for ch in text:
        cp = ord(ch)
        if cp < first or cp > last:
            missing.append(ch); continue
        w, h, adv, left, top, _, _ = glyphs[cp - first]
        gx1, gx2 = cursor + left, cursor + left + w
        x1 = gx1 if x1 is None else min(x1, gx1)
        x2 = gx2 if x2 is None else max(x2, gx2)
        cursor += adv
    ink = (x2 - x1) if x1 is not None else 0
    return cursor, ink, missing

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", choices=["body", "head", "both"], default="both")
    ap.add_argument("--x", type=int, default=None, help="x poczatku napisu; raport 'koniec' i margines do W-M")
    ap.add_argument("text", nargs="+")
    a = ap.parse_args()
    kinds = ["body", "head"] if a.font == "both" else [a.font]
    for kind in kinds:
        glyphs, first, last, ay, asc, desc = load(kind)
        print("== %s: advance_y=%d ascender=%d descender=%d, glify %d (0x%02X..0x%02X)" % (kind, ay, asc, desc, len(glyphs), first, last))
        for t in a.text:
            adv, ink, missing = measure(t, glyphs, first, last)
            line = "  %-52s adv=%4d ink=%4d" % (repr(t), adv, ink)
            if a.x is not None:
                end = a.x + adv
                line += "  x=%d -> koniec=%d (%+d px do %d)" % (a.x, end, (W - M) - end, W - M)
            if missing:
                line += "  !!! POZA ASCII (zniknie na panelu): %s" % "".join(sorted(set(missing)))
            print(line)

if __name__ == "__main__":
    main()
