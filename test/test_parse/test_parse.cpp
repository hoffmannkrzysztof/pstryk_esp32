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
  // buy/sell live under frames[].metrics.pricing.* in the real API.
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.52f, f.buy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.31f, f.sell);
}

// Cross-board canonical-field guard. Every board (AMOLED, e-paper, and any
// future display) shows PriceView.currentBuy, which is PriceFrame.buy. This
// locks buy=price_gross / sell=price_prosumer_gross so a future edit can't
// silently switch a board to net/tge/full -- the exact regression that made the
// e-paper board read a tax-stripped value. frame[0] carries every real field
// with DISTINCT values, so an assert on 0.30 excludes net(0.24)/tge(0.20)/full(0.45).
void test_buy_is_gross() {
  PriceData d;
  parsePricing(kPricingJson, d);
  const PriceFrame& f = d.frames[0];
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.30f, f.buy);   // price_gross (VAT incl.), NOT net/tge/full
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.18f, f.sell);  // price_prosumer_gross, NOT price_prosumer_net
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
  RUN_TEST(test_buy_is_gross);
  RUN_TEST(test_flags_default_false);
  RUN_TEST(test_today_only);
  RUN_TEST(test_malformed_returns_false);
  return UNITY_END();
}
