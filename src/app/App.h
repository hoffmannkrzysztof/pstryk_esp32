#pragma once
#include "storage/Settings.h"
#include "net/PstrykClient.h"
#include "net/WiFiProvisioner.h"
#include "render/LongRenderer.h"
#include "render/Pages.h"
#include "view/PriceView.h"
#include "core/PriceData.h"

namespace pstryk {

class App {
 public:
  void setup();
  void loop();

 private:
  void doFetch();
  void advancePage();
  void redraw();
#ifdef PSTRYK_OTA_BOOTSTRAP
  void runBootstrap();   // installer build: provision, force-install latest, reboot
#endif

  LongRenderer    gfx_;
  Settings        settings_;
  WiFiProvisioner provisioner_;
  PriceData       data_;
  PriceView       view_;
  int             lastViewHour_ = -1;   // local hour view_ was built for; rebuild on change

  bool     haveData_ = false;
  bool     authError_ = false;   // sticky: shows the API-key error screen until next OK fetch
  time_t   lastFetchOk_ = 0;     // UTC epoch of last successful fetch
  uint32_t nextFetchAtMs_ = 0;
  uint32_t nextRotateAtMs_ = 0;
  uint32_t lastRedrawMs_ = 0;
  uint32_t nextOtaCheckAtMs_ = 0;
  uint32_t failCount_ = 0;       // consecutive network/parse fetch failures (drives backoff)
  uint32_t lastUiSig_ = 0;       // signature of the last rendered frame (dirty check)
  uint16_t dataGen_ = 0;         // bumped whenever view_ changes (fetch / hour rebuild)
  int      pageIdx_ = 0;
};

}  // namespace pstryk
