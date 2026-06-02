#pragma once
#include <Arduino.h>

namespace pstryk {

struct Settings {
  String ssid;
  String pass;
  String apiKey;

  bool isComplete() const { return ssid.length() > 0 && apiKey.length() > 0; }

  void load();   // read from NVS namespace "pstryk"
  void save();   // persist to NVS
  void clear();  // wipe (used by re-provisioning)
};

}  // namespace pstryk
