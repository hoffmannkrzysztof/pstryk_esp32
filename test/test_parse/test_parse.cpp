#include <unity.h>
#include "core/PstrykParse.h"
#include "core/TimeService.h"
#include "../fixtures.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_parses_all_frames() {
  PriceData d;
  TEST_ASSERT_TRUE(parsePricing(kPricingJson, d));
  TEST_ASSERT_EQUAL_UINT(6, d.frames.size());
}

void test_maps_fields() {
  PriceData d;
  parsePricing(kPricingJson, d);
  const PriceFrame& f = d.frames[1];
  TEST_ASSERT_EQUAL(parseIso8601Utc("2026-06-02T07:00:00Z"), f.start);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.52f, f.buy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.31f, f.sell);
  TEST_ASSERT_TRUE(f.isLive);
}

void test_flags_default_false() {
  PriceData d;
  parsePricing(kPricingJson, d);
  TEST_ASSERT_FALSE(d.frames[2].isLive);
  TEST_ASSERT_TRUE(d.frames[3].isExpensive);
}

void test_today_only() {
  PriceData d;
  TEST_ASSERT_TRUE(parsePricing(kPricingTodayOnlyJson, d));
  TEST_ASSERT_EQUAL_UINT(2, d.frames.size());
}

void test_malformed_returns_false() {
  PriceData d;
  TEST_ASSERT_FALSE(parsePricing("not json", d));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_all_frames);
  RUN_TEST(test_maps_fields);
  RUN_TEST(test_flags_default_false);
  RUN_TEST(test_today_only);
  RUN_TEST(test_malformed_returns_false);
  return UNITY_END();
}
