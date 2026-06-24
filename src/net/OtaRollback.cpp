#include "net/OtaRollback.h"
#include <esp_ota_ops.h>

// Override arduino-esp32's weak hook so the core does NOT auto-confirm a pending
// OTA image during init(). We confirm explicitly from the app after a health
// check (see confirmRunningImageValid). Requires CONFIG_APP_ROLLBACK_ENABLE, which
// the stock arduino-esp32 ESP32-S3 build enables.
extern "C" bool verifyRollbackLater() {
  return true;
}

namespace pstryk {

void confirmRunningImageValid() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    esp_ota_mark_app_valid_cancel_rollback();
  }
}

}  // namespace pstryk
