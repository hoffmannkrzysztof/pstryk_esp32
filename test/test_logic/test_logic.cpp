#include <unity.h>
#include "core/PriceLogic.h"
#include "core/PstrykParse.h"
#include "core/TimeService.h"
#include "../fixtures.h"

using namespace pstryk;

void setUp() { timeServiceBegin(); }
void tearDown() {}

static PriceView viewFromFixture(const char* json) {
  PriceData d;
  parsePricing(json, d);
  // "now" = 2026-06-02T07:30:00Z == 09:30 local, inside the is_live frame.
  return buildView(d, parseIso8601Utc("2026-06-02T07:30:00Z"));
}

void test_current_from_is_live() {
  PriceView v = viewFromFixture(kPricingJson);
  TEST_ASSERT_TRUE(v.hasData);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.52f, v.currentBuy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.31f, v.currentSell);
  TEST_ASSERT_EQUAL_INT(9, v.currentHour);  // 07:00Z -> 09:00 local
}

void test_today_split_and_extremes() {
  PriceView v = viewFromFixture(kPricingJson);
  TEST_ASSERT_EQUAL_UINT(4, v.today.size());        // 4 today frames
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.30f, v.todayCheapest.price);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1.12f, v.todayExpensive.price);
  TEST_ASSERT_EQUAL_INT(1, v.liveIndex);            // 2nd today bar is live
}

void test_today_average() {
  PriceView v = viewFromFixture(kPricingJson);
  // (0.30+0.52+0.48+1.12)/4 = 0.605
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.605f, v.todayAvg);
  TEST_ASSERT_TRUE(v.currentBelowAvg);              // 0.52 < 0.605
}

void test_next_hour_trend() {
  PriceView v = viewFromFixture(kPricingJson);
  // current 0.52 -> next today bar 0.48 -> Down
  TEST_ASSERT_EQUAL_INT((int)Trend::Down, (int)v.nextTrend);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.48f, v.nextBuy);
}

void test_tomorrow_present() {
  PriceView v = viewFromFixture(kPricingJson);
  TEST_ASSERT_TRUE(v.hasTomorrow);
  TEST_ASSERT_EQUAL_UINT(2, v.tomorrow.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.19f, v.tomorrowCheapest.price);
}

void test_tomorrow_absent() {
  PriceView v = viewFromFixture(kPricingTodayOnlyJson);
  TEST_ASSERT_FALSE(v.hasTomorrow);
  TEST_ASSERT_EQUAL_UINT(0, v.tomorrow.size());
}

// 25-hour DST fall-back day: local hour 02 occurs twice. During the SECOND
// 02:00 (01:30Z, already CET) the view must carry that frame's price -- matching
// by local hour would silently pick the first 02:00's price for a whole hour.
void test_dst_fall_back_second_2am_matched_by_epoch() {
  PriceData d;
  parsePricing(kPricingDstFallBackJson, d);
  PriceView v = buildView(d, parseIso8601Utc("2026-10-25T01:30:00Z"));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.99f, v.currentBuy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.55f, v.currentSell);
  TEST_ASSERT_EQUAL_INT(2, v.liveIndex);   // positional: 3rd bar of the 25h day
  TEST_ASSERT_EQUAL_INT(2, v.currentHour);
}

// Boundary lock: during the FIRST 02:00 (00:30Z, still CEST) the earlier frame wins.
void test_dst_fall_back_first_2am_matched_by_epoch() {
  PriceData d;
  parsePricing(kPricingDstFallBackJson, d);
  PriceView v = buildView(d, parseIso8601Utc("2026-10-25T00:30:00Z"));
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.10f, v.currentBuy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 0.03f, v.currentSell);
  TEST_ASSERT_EQUAL_INT(1, v.liveIndex);
}

// Negative midday price: the sign must survive into the headline and extremes.
void test_negative_price_flows_into_view() {
  PriceData d;
  parsePricing(kPricingNegativeJson, d);
  PriceView v = buildView(d, parseIso8601Utc("2026-06-02T10:30:00Z"));
  TEST_ASSERT_FLOAT_WITHIN(0.001, -0.15f, v.currentBuy);
  TEST_ASSERT_FLOAT_WITHIN(0.001, -0.05f, v.currentSell);
  TEST_ASSERT_FLOAT_WITHIN(0.001, -0.15f, v.todayCheapest.price);
  TEST_ASSERT_TRUE(v.currentBelowAvg);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_current_from_is_live);
  RUN_TEST(test_today_split_and_extremes);
  RUN_TEST(test_today_average);
  RUN_TEST(test_next_hour_trend);
  RUN_TEST(test_tomorrow_present);
  RUN_TEST(test_tomorrow_absent);
  RUN_TEST(test_dst_fall_back_second_2am_matched_by_epoch);
  RUN_TEST(test_dst_fall_back_first_2am_matched_by_epoch);
  RUN_TEST(test_negative_price_flows_into_view);
  return UNITY_END();
}
