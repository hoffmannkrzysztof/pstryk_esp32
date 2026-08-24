#pragma once
#include <cstddef>

namespace pstryk {

// Buduje payload "WIFI:T:WPA;S:<ssid>;P:<psk>;;" do out (rozmiar n).
// Zwraca false gdy sie nie miesci; out jest wtedy pustym stringiem.
// Escapowanie znakow specjalnych WIFI: swiadomie pominiete -- SSID portalu to
// stala bez nich, a PSK to wylacznie cyfry (WiFiProvisioner::portalPassword).
bool buildWifiQrPayload(const char* ssid, const char* psk, char* out, size_t n);

}  // namespace pstryk
