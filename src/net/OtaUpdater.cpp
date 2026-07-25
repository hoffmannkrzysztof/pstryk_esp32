#include "net/OtaUpdater.h"
#include "net/ota_public_key.h"
#include "core/Version.h"
#include "core/OtaManifest.h"
#include "core/OtaPolicy.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>  // pulls in Updater_Signing.h (UpdaterRSAVerifier, SHA2Builder) under UPDATE_SIGN
#include <esp_attr.h>

namespace pstryk {

// Stable "latest release" manifest URL; GitHub 302-redirects to the asset CDN.
static const char* kManifestBase =
    "https://github.com/hoffmannkrzysztof/pstryk_esp32/releases/latest/download/manifest-";

// ETag of the last seen manifest. Survives deep sleep on the e-paper board
// (RTC RAM), plain uptime-static on the Long board. Lets the daily check end
// in a 304 with no body download/parse when no release was published, which
// is the overwhelmingly common case.
RTC_DATA_ATTR static char g_manifestEtag[64] = "";

// The RSA signature authenticates the IMAGE BYTES and nothing else -- not the
// version the manifest claimed, not the URL it pointed at. The manifest arrives
// over TLS with setInsecure(), so a MITM could hand us a "version 99.0.0" manifest
// aimed at a genuine, correctly-signed OLD release and force a downgrade; v1.0.1
// shipped a portal with no password, no timeout and the API key prefilled into the
// form. Rebuilding the asset URL FROM the manifest's own fields and demanding an
// exact match binds the claimed version to the artifact path: claiming 99.0.0 then
// requires a v99.0.0 release to actually exist under this repo.
// Keep in sync with .github/workflows/release.yml.
static String expectedAssetUrl(const OtaManifest& m) {
  return String("https://github.com/hoffmannkrzysztof/pstryk_esp32/releases/download/v") +
         m.version.c_str() + "/firmware-" + m.board.c_str() + ".bin";
}

OtaResult OtaUpdater::runOnce(bool force) {
  // 1) Fetch this board's manifest.
  String manifestUrl = String(kManifestBase) + PSTRYK_BOARD_ID + ".json";
  WiFiClientSecure mClient;
  mClient.setInsecure();                                  // signature is the trust anchor (v1)
  HTTPClient mHttp;
  mHttp.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // GitHub -> CDN redirect
  mHttp.setConnectTimeout(8000);
  mHttp.setTimeout(12000);
  if (!mHttp.begin(mClient, manifestUrl)) return OtaResult::FetchError;
  static const char* kCollect[] = {"ETag"};
  mHttp.collectHeaders(kCollect, 1);
  // Conditional GET -- but never on a forced (bootstrap) install, which must
  // always read the manifest to flash the current release.
  if (!force && g_manifestEtag[0]) mHttp.addHeader("If-None-Match", g_manifestEtag);
  int mCode = mHttp.GET();
  if (mCode == HTTP_CODE_NOT_MODIFIED) {                  // 304: nothing new
    mHttp.end();
    return OtaResult::NoUpdate;
  }
  if (mCode != HTTP_CODE_OK) {
    log_e("OTA manifest fetch failed: %d", mCode);
    mHttp.end();
    return OtaResult::FetchError;
  }
  String etag = mHttp.hasHeader("ETag") ? mHttp.header("ETag") : String();
  String body = mHttp.getString();
  mHttp.end();
  // The ETag used to be persisted right here, before the decision gate, the
  // download, the signature check and the flash write. Every failure after this
  // point then left it behind, so the next check ended in a 304 and the board
  // SKIPPED that release for good -- waiting for the next publish instead of
  // retrying the one that failed. Drop it now and re-commit it only where the
  // conclusion is genuinely "nothing to install".
  g_manifestEtag[0] = '\0';
  auto keepEtag = [&etag]() {
    if (!etag.isEmpty()) strlcpy(g_manifestEtag, etag.c_str(), sizeof(g_manifestEtag));
  };
  if (body.isEmpty()) return OtaResult::FetchError;

  OtaManifest m;
  if (!parseManifest(body.c_str(), m)) return OtaResult::ParseError;

  // 2) Decision gate. force=true (bootstrap) skips the version/dev gate but still
  //    requires the manifest to target THIS board; the signature is verified later
  //    regardless, so a forced install is still safe.
  if (force) {
    if (m.board != PSTRYK_BOARD_ID) {
      log_e("OTA: manifest board '%s' != '%s'", m.board.c_str(), PSTRYK_BOARD_ID);
      keepEtag();
      return OtaResult::NoUpdate;
    }
  } else if (!shouldApplyUpdate(m, FIRMWARE_VERSION, PSTRYK_BOARD_ID)) {
    keepEtag();                      // nothing to install: safe to 304 next time
    return OtaResult::NoUpdate;
  }
  // 2b) The asset URL must be exactly the one release.yml derives from these very
  //     manifest fields -- see expectedAssetUrl(). This is what stops an unpinned
  //     host and a version/binary mismatch (forced downgrade).
  String expectedUrl = expectedAssetUrl(m);
  if (expectedUrl != m.url.c_str()) {
    log_e("OTA: manifest url '%s' != expected '%s'", m.url.c_str(), expectedUrl.c_str());
    return OtaResult::ParseError;
  }
  log_i("OTA: %s %s -> %s", force ? "installing" : "updating", FIRMWARE_VERSION, m.version.c_str());

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
  // m.size was parsed and then never used ("informational"). Enforce it: the
  // manifest and the asset disagreeing on length is never legitimate.
  if (m.size > 0 && (unsigned long)total != m.size) {
    log_e("OTA size mismatch: manifest %lu, asset %u", m.size, (unsigned)total);
    fHttp.end();
    return OtaResult::FetchError;
  }

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
