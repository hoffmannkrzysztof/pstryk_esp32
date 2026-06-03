#include "storage/Settings.h"
#include <Preferences.h>

namespace pstryk {

static const char* kNs = "pstryk";

void Settings::load() {
  Preferences p;
  p.begin(kNs, /*readOnly=*/true);
  ssid   = p.getString("ssid", "");
  pass   = p.getString("pass", "");
  apiKey = p.getString("apiKey", "");
  p.end();
}

void Settings::save() {
  Preferences p;
  p.begin(kNs, false);
  p.putString("ssid", ssid);
  p.putString("pass", pass);
  p.putString("apiKey", apiKey);
  p.end();
}

void Settings::clear() {
  Preferences p;
  p.begin(kNs, false);
  p.clear();
  p.end();
  ssid = ""; pass = ""; apiKey = "";
}

}  // namespace pstryk
