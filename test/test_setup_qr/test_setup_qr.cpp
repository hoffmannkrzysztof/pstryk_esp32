#include <unity.h>
#include <cstring>
#include "core/SetupQr.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_payload_exact_format() {
  char out[64];
  TEST_ASSERT_TRUE(buildWifiQrPayload("Pstryk-Setup", "41850372", out, sizeof(out)));
  // Dokladny format, z terminatorem ";;" — od tego zalezy skanowalnosc.
  TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:Pstryk-Setup;P:41850372;;", out);
}

void test_payload_fits_qr_v3_medium() {
  // Wersja 3 / ECC M miesci 42 bajty w trybie bajtowym; 12-znakowy SSID
  // + 8-cyfrowy PSK musza sie miescic, inaczej ekran traci QR.
  char out[64];
  TEST_ASSERT_TRUE(buildWifiQrPayload("Pstryk-Setup", "12345678", out, sizeof(out)));
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(42u, (uint32_t)std::strlen(out));
}

void test_small_buffer_fails_with_empty_string() {
  // Za maly bufor -> false i pusty string, nigdy ucieta (skanowalna!) resztka.
  char out[16];
  std::memset(out, 'X', sizeof(out));
  TEST_ASSERT_FALSE(buildWifiQrPayload("Pstryk-Setup", "41850372", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
}

void test_zero_sized_buffer_fails() {
  TEST_ASSERT_FALSE(buildWifiQrPayload("a", "b", nullptr, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_payload_exact_format);
  RUN_TEST(test_payload_fits_qr_v3_medium);
  RUN_TEST(test_small_buffer_fails_with_empty_string);
  RUN_TEST(test_zero_sized_buffer_fails);
  return UNITY_END();
}
