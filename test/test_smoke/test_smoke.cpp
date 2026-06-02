#include <unity.h>
#include "core/PriceData.h"

void setUp() {}
void tearDown() {}

void test_pricedata_defaults() {
  pstryk::PriceFrame f;
  TEST_ASSERT_EQUAL_INT(0, (int)f.start);
  TEST_ASSERT_FALSE(f.isLive);
  pstryk::PriceData d;
  TEST_ASSERT_EQUAL_UINT(0, d.frames.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pricedata_defaults);
  return UNITY_END();
}
