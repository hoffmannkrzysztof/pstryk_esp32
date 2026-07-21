#include <unity.h>
#include <cstring>
#include "core/TimeService.h"

using namespace pstryk;

void setUp() { timeServiceBegin(); }
void tearDown() {}

void test_iso_roundtrip() {
  time_t t = parseIso8601Utc("2026-06-02T09:00:00Z");
  char out[21];
  formatIso8601Utc(t, out);
  TEST_ASSERT_EQUAL_STRING("2026-06-02T09:00:00Z", out);
}

void test_local_hour_summer_is_utc_plus_2() {
  // June -> CEST (UTC+2): 09:00Z == 11:00 local
  TEST_ASSERT_EQUAL_INT(11, localHour(parseIso8601Utc("2026-06-02T09:00:00Z")));
}

void test_local_hour_winter_is_utc_plus_1() {
  // January -> CET (UTC+1): 09:00Z == 10:00 local
  TEST_ASSERT_EQUAL_INT(10, localHour(parseIso8601Utc("2026-01-15T09:00:00Z")));
}

void test_day_ordinals_tomorrow() {
  time_t now = parseIso8601Utc("2026-06-02T09:30:00Z");      // 11:30 local, day X
  time_t tmrw = parseIso8601Utc("2026-06-03T06:00:00Z");     // 08:00 local, day X+1
  TEST_ASSERT_EQUAL_INT(localDayOrdinal(now) + 1, localDayOrdinal(tmrw));
}

void test_local_midnight_utc() {
  // Local midnight of 2026-06-02 (CEST, UTC+2) is 2026-06-01T22:00:00Z
  time_t now = parseIso8601Utc("2026-06-02T09:30:00Z");
  char out[21];
  formatIso8601Utc(localMidnightUtc(now), out);
  TEST_ASSERT_EQUAL_STRING("2026-06-01T22:00:00Z", out);
}

// Around a DST switch the PCF8563's local wall time is ambiguous/skewed, so the
// sleeping board must bring up NTP instead of trusting an RTC-only clock.
void test_dst_window_detected_at_fall_back() {
  // 2026-10-25 01:00Z is the fall-back instant (03:00 CEST -> 02:00 CET)
  TEST_ASSERT_TRUE(dstChangesWithin(parseIso8601Utc("2026-10-25T00:30:00Z"), 2 * 3600));
}

void test_dst_window_detected_at_spring_forward() {
  // 2026-03-29 01:00Z is the spring-forward instant (02:00 CET -> 03:00 CEST)
  TEST_ASSERT_TRUE(dstChangesWithin(parseIso8601Utc("2026-03-29T00:30:00Z"), 2 * 3600));
}

void test_dst_window_clear_on_plain_days_and_later_same_day() {
  TEST_ASSERT_FALSE(dstChangesWithin(parseIso8601Utc("2026-06-02T07:30:00Z"), 2 * 3600));
  TEST_ASSERT_FALSE(dstChangesWithin(parseIso8601Utc("2026-01-15T09:00:00Z"), 2 * 3600));
  // Noon of the fall-back day is already well clear of the switch.
  TEST_ASSERT_FALSE(dstChangesWithin(parseIso8601Utc("2026-10-25T12:00:00Z"), 2 * 3600));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_iso_roundtrip);
  RUN_TEST(test_local_hour_summer_is_utc_plus_2);
  RUN_TEST(test_local_hour_winter_is_utc_plus_1);
  RUN_TEST(test_day_ordinals_tomorrow);
  RUN_TEST(test_local_midnight_utc);
  RUN_TEST(test_dst_window_detected_at_fall_back);
  RUN_TEST(test_dst_window_detected_at_spring_forward);
  RUN_TEST(test_dst_window_clear_on_plain_days_and_later_same_day);
  return UNITY_END();
}
