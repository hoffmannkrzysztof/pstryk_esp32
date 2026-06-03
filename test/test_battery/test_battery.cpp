#include <unity.h>
#include "core/Battery.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_volts_from_pin_mv_applies_2to1_divider() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.20f, batteryVoltsFromPinMv(2100.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, batteryVoltsFromPinMv(0.0f));
}

void test_percent_endpoints_and_midpoint() {
  TEST_ASSERT_EQUAL_INT(100, batteryPercent(4.20f));
  TEST_ASSERT_EQUAL_INT(0, batteryPercent(3.30f));
  TEST_ASSERT_EQUAL_INT(50, batteryPercent(3.75f));
}

void test_percent_clamps() {
  TEST_ASSERT_EQUAL_INT(100, batteryPercent(4.50f));
  TEST_ASSERT_EQUAL_INT(0, batteryPercent(3.00f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_volts_from_pin_mv_applies_2to1_divider);
  RUN_TEST(test_percent_endpoints_and_midpoint);
  RUN_TEST(test_percent_clamps);
  return UNITY_END();
}
