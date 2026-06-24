#include <unity.h>
#include "core/OtaManifest.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

static const char* kOk =
  R"JSON({"board":"epaper","version":"1.4.0",
  "url":"https://github.com/o/r/releases/download/v1.4.0/firmware-epaper.bin",
  "size":1331840,"sha256":"deadbeef"})JSON";

void test_parses_all_fields() {
  OtaManifest m;
  TEST_ASSERT_TRUE(parseManifest(kOk, m));
  TEST_ASSERT_EQUAL_STRING("epaper", m.board.c_str());
  TEST_ASSERT_EQUAL_STRING("1.4.0", m.version.c_str());
  TEST_ASSERT_EQUAL_STRING(
    "https://github.com/o/r/releases/download/v1.4.0/firmware-epaper.bin",
    m.url.c_str());
  TEST_ASSERT_EQUAL_UINT32(1331840UL, m.size);
  TEST_ASSERT_EQUAL_STRING("deadbeef", m.sha256.c_str());
}

void test_missing_url_fails() {
  OtaManifest m;
  TEST_ASSERT_FALSE(parseManifest(R"JSON({"board":"epaper","version":"1.4.0"})JSON", m));
}

void test_missing_board_fails() {
  OtaManifest m;
  TEST_ASSERT_FALSE(parseManifest(R"JSON({"version":"1.4.0","url":"x"})JSON", m));
}

void test_malformed_fails() {
  OtaManifest m;
  TEST_ASSERT_FALSE(parseManifest("not json", m));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_all_fields);
  RUN_TEST(test_missing_url_fails);
  RUN_TEST(test_missing_board_fails);
  RUN_TEST(test_malformed_fails);
  return UNITY_END();
}
