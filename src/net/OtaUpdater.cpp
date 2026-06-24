#include "net/OtaUpdater.h"
#include "net/ota_public_key.h"
#include "core/Version.h"
#include "core/OtaManifest.h"
#include "core/OtaPolicy.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>  // pulls in Updater_Signing.h (UpdaterRSAVerifier, SHA2Builder) under UPDATE_SIGN

namespace pstryk {

// Stable "latest release" manifest URL; GitHub 302-redirects to the asset CDN.
static const char* kManifestBase =
    "https://github.com/hoffmannkrzysztof/pstryk_esp32/releases/latest/download/manifest-";

OtaResult OtaUpdater::runOnce() {
  // 1) Fetch this board's manifest.
  String manifestUrl = String(kManifestBase) + PSTRYK_BOARD_ID + ".json";
  WiFiClientSecure mClient;
  mClient.setInsecure();                                  // signature is the trust anchor (v1)
  HTTPClient mHttp;
  mHttp.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // GitHub -> CDN redirect
  mHttp.setConnectTimeout(8000);
  mHttp.setTimeout(12000);
  if (!mHttp.begin(mClient, manifestUrl)) return OtaResult::FetchError;
  int mCode = mHttp.GET();
  if (mCode != HTTP_CODE_OK) {
    log_e("OTA manifest fetch failed: %d", mCode);
    mHttp.end();
    return OtaResult::FetchError;
  }
  String body = mHttp.getString();
  mHttp.end();
  if (body.isEmpty()) return OtaResult::FetchError;

  OtaManifest m;
  if (!parseManifest(body.c_str(), m)) return OtaResult::ParseError;

  // 2) Decision gate (pure, unit-tested).
  if (!shouldApplyUpdate(m, FIRMWARE_VERSION, PSTRYK_BOARD_ID)) return OtaResult::NoUpdate;
  log_i("OTA: updating %s -> %s", FIRMWARE_VERSION, m.version.c_str());

  // 3) Download the signed image.
  WiFiClientSecure fClient;
  fClient.setInsecure();
  HTTPClient fHttp;
  fHttp.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  fHttp.setConnectTimeout(8000);
  fHttp.setTimeout(20000);
  if (!fHttp.begin(fClient, String(m.url.c_str()))) return OtaResult::FetchError;
  int fCode = fHttp.GET();
  if (fCode != HTTP_CODE_OK) {
    log_e("OTA firmware fetch failed: %d", fCode);
    fHttp.end();
    return OtaResult::FetchError;
  }
  int contentLen = fHttp.getSize();
  if (contentLen <= 0) { fHttp.end(); return OtaResult::FetchError; }
  size_t total = (size_t)contentLen;

  // 4) Install signature verification BEFORE begin(); begin() takes the TOTAL size
  //    (firmware + appended signature). end() performs the verification.
  UpdaterRSAVerifier verifier(PUBLIC_KEY, PUBLIC_KEY_LEN, HASH_SHA256);
  if (!Update.installSignature(&verifier)) { fHttp.end(); return OtaResult::VerifyError; }
  if (!Update.begin(total)) { fHttp.end(); return OtaResult::FlashError; }

  NetworkClient* stream = fHttp.getStreamPtr();
  if (!stream) { fHttp.end(); return OtaResult::FetchError; }
  size_t written = Update.writeStream(*stream);
  fHttp.end();
  if (written != total) {
    log_e("OTA write incomplete: %u/%u bytes", (unsigned)written, (unsigned)total);
    Update.abort();
    return OtaResult::FlashError;
  }

  if (!Update.end()) {
    OtaResult r = (Update.getError() == UPDATE_ERROR_SIGN)
                      ? OtaResult::VerifyError
                      : OtaResult::FlashError;
    log_e("OTA failed: %s", Update.errorString());
    return r;
  }
  if (!Update.isFinished()) return OtaResult::FlashError;

  log_i("OTA flashed; rebooting into new image");
  delay(200);
  ESP.restart();                                          // boots new image as PENDING_VERIFY
  return OtaResult::NoUpdate;                             // unreachable
}

}  // namespace pstryk
