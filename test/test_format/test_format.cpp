#include <unity.h>
#include "core/Format.h"

void setUp() {}
void tearDown() {}

void test_two_decimals_comma() {
  char b[8];
  pstryk::formatPln(0.52f, b);
  TEST_ASSERT_EQUAL_STRING("0,52", b);
}

void test_rounds() {
  char b[8];
  pstryk::formatPln(1.128f, b);
  TEST_ASSERT_EQUAL_STRING("1,13", b);  // rounds up (not a half-way tie)
}

void test_integerish() {
  char b[8];
  pstryk::formatPln(1.0f, b);
  TEST_ASSERT_EQUAL_STRING("1,00", b);
}

// Negative prices are real on the PL market; the minus must survive formatting
// (the renderers must then draw it -- the 7-seg path once dropped it).
void test_negative_keeps_minus() {
  char b[8];
  pstryk::formatPln(-0.15f, b);
  TEST_ASSERT_EQUAL_STRING("-0,15", b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_two_decimals_comma);
  RUN_TEST(test_rounds);
  RUN_TEST(test_integerish);
  RUN_TEST(test_negative_keeps_minus);
  return UNITY_END();
}
