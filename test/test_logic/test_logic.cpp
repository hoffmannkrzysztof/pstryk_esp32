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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_current_from_is_live);
  RUN_TEST(test_today_split_and_extremes);
  RUN_TEST(test_today_average);
  RUN_TEST(test_next_hour_trend);
  RUN_TEST(test_tomorrow_present);
  RUN_TEST(test_tomorrow_absent);
  return UNITY_END();
}
