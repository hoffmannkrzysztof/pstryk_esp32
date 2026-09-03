"""PlatformIO extra script (post): rekompiluj WiFiManager.cpp po zmianie wm_strings_pl.h.

WiFiManager.h dolacza nasz plik stringow przez `#include WM_STRINGS_FILE` (makro
z platformio.ini). Skaner zaleznosci SCons nie rozwija makr, wiec edycja
src/net/wm_strings_pl.h NIE uniewaznia .pio/build/<env>/lib*/WiFiManager/
WiFiManager.cpp.o -- firmware.bin cicho zostaje ze STARYMI tekstami portalu, a
build raportuje SUCCESS (2026-09-03: angielski portal w binarce e-papieru mimo
przepisanego pliku). Tu dokladamy te zaleznosc jawnie, tylko dla tej biblioteki.
"""
import os

Import("env")

strings = os.path.join(env.subst("$PROJECT_SRC_DIR"), "net", "wm_strings_pl.h")
if os.path.isfile(strings):
    for lb in env.GetLibBuilders():
        if lb.name != "WiFiManager":
            continue
        obj = os.path.join(env.subst(lb.build_dir), "WiFiManager.cpp.o")
        env.Depends(env.File(obj), env.File(strings))
