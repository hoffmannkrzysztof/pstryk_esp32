#pragma once

namespace pstryk {

// If the running image was just OTA-flashed and is awaiting verification, mark it
// valid so the bootloader keeps it. Safe no-op otherwise. Call once per boot AFTER
// core subsystems (display/PSRAM) have initialized, so a build that crashes during
// boot is never confirmed and the bootloader rolls back to the previous image.
void confirmRunningImageValid();

}  // namespace pstryk
