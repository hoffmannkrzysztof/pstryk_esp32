#include <unity.h>
#include "core/RefreshPolicy.h"
#include "core/TimeService.h"

using namespace pstryk;

void setUp() { timeServiceBegin(); }
void tearDown() {}

void test_window_is_local_midnight_plus_48h() {
  time_t now = parseIso8601Utc("2026-06-02T09:30:00Z");
  Window w = computeWindow(now);
  TEST_ASSERT_EQUAL_STRING("2026-06-01T22:00:00Z", w.start);  // local midnight (CEST)
  TEST_ASSERT_EQUAL_STRING("2026-06-03T22:00:00Z", w.end);    // +48h
}

void test_base_cadence_30min_before_noon() {
  time_t now = parseIso8601Utc("2026-06-02T07:00:00Z");  // 09:00 local
  TEST_ASSERT_EQUAL_UINT32(30u * 60u * 1000u, nextRefreshMs(now, false));
}

void test_awaiting_tomorrow_20min_after_noon() {
  time_t now = parseIso8601Utc("2026-06-02T12:00:00Z");  // 14:00 local, no tomorrow yet
  TEST_ASSERT_EQUAL_UINT32(20u * 60u * 1000u, nextRefreshMs(now, false));
}

void test_after_noon_with_tomorrow_back_to_30min() {
  time_t now = parseIso8601Utc("2026-06-02T12:00:00Z");  // 14:00 local, tomorrow present
  TEST_ASSERT_EQUAL_UINT32(30u * 60u * 1000u, nextRefreshMs(now, true));
}

void test_wake_to_top_of_hour_plus_guard() {
  time_t now = parseIso8601Utc("2026-06-02T07:20:00Z");  // 09:20 local, 40 min to 10:00
  TEST_ASSERT_EQUAL_UINT32(40u * 60u + 5u, secondsUntilNextWake(now, false));
}

void test_wake_exactly_on_hour_waits_full_hour() {
  time_t now = parseIso8601Utc("2026-06-02T07:00:00Z");  // 09:00 local exactly
  TEST_ASSERT_EQUAL_UINT32(60u * 60u + 5u, secondsUntilNextWake(now, false));
}

void test_wake_capped_at_30min_midday_without_tomorrow() {
  time_t now = parseIso8601Utc("2026-06-02T11:05:00Z");  // 13:05 local, 55 min to 14:00
  TEST_ASSERT_EQUAL_UINT32(30u * 60u, secondsUntilNextWake(now, false));
}

void test_wake_not_capped_midday_when_tomorrow_present() {
  time_t now = parseIso8601Utc("2026-06-02T11:05:00Z");  // 13:05 local
  TEST_ASSERT_EQUAL_UINT32(55u * 60u + 5u, secondsUntilNextWake(now, true));
}

void test_wake_not_capped_before_noon() {
  time_t now = parseIso8601Utc("2026-06-02T09:05:00Z");  // 11:05 local, 55 min to 12:00
  TEST_ASSERT_EQUAL_UINT32(55u * 60u + 5u, secondsUntilNextWake(now, false));
}

void test_backoff_first_failure_is_60s() {
  TEST_ASSERT_EQUAL_UINT32(60u, backoffSeconds(1));
}

void test_backoff_doubles_per_failure() {
  TEST_ASSERT_EQUAL_UINT32(120u, backoffSeconds(2));
  TEST_ASSERT_EQUAL_UINT32(240u, backoffSeconds(3));
  TEST_ASSERT_EQUAL_UINT32(480u, backoffSeconds(4));
  TEST_ASSERT_EQUAL_UINT32(960u, backoffSeconds(5));
  TEST_ASSERT_EQUAL_UINT32(1920u, backoffSeconds(6));
}

void test_backoff_caps_at_one_hour() {
  TEST_ASSERT_EQUAL_UINT32(3600u, backoffSeconds(7));
  TEST_ASSERT_EQUAL_UINT32(3600u, backoffSeconds(20));
}

void test_backoff_zero_failures_uses_base_delay() {
  TEST_ASSERT_EQUAL_UINT32(60u, backoffSeconds(0));
}

void test_backoff_huge_counter_does_not_overflow() {
  TEST_ASSERT_EQUAL_UINT32(3600u, backoffSeconds(0xFFFFFFFFu));
}

// --- needsNetwork: the radio-free-wake gate for the RTC price cache ---

static RtcCacheView goodCache() {
  RtcCacheView c; c.coversNow = true; c.hasTomorrow = true; return c;
}

void test_cache_hit_morning_wake_is_radio_free() {
  time_t now = parseIso8601Utc("2026-06-02T07:30:00Z");  // 09:30 local
  RtcCacheView c = goodCache(); c.hasTomorrow = false;   // morning: tomorrow not needed
  TEST_ASSERT_FALSE(needsNetwork(now, false, c, 3600u, false));
}

void test_button_wake_forces_network() {
  time_t now = parseIso8601Utc("2026-06-02T07:30:00Z");
  TEST_ASSERT_TRUE(needsNetwork(now, true, goodCache(), 3600u, false));
}

void test_cache_miss_forces_network() {
  time_t now = parseIso8601Utc("2026-06-02T07:30:00Z");
  RtcCacheView c; c.coversNow = false; c.hasTomorrow = false;
  TEST_ASSERT_TRUE(needsNetwork(now, false, c, 3600u, false));
}

void test_due_ota_check_forces_network() {
  time_t now = parseIso8601Utc("2026-06-02T07:30:00Z");
  TEST_ASSERT_TRUE(needsNetwork(now, false, goodCache(), 3600u, true));
}

void test_stale_ntp_forces_network() {
  time_t now = parseIso8601Utc("2026-06-02T07:30:00Z");
  TEST_ASSERT_TRUE(needsNetwork(now, false, goodCache(), 25u * 3600u, false));
}

void test_afternoon_without_tomorrow_hunts_online() {
  time_t now = parseIso8601Utc("2026-06-02T10:30:00Z");  // 12:30 local
  RtcCacheView c = goodCache(); c.hasTomorrow = false;
  TEST_ASSERT_TRUE(needsNetwork(now, false, c, 3600u, false));
}

void test_afternoon_with_tomorrow_stays_radio_free() {
  time_t now = parseIso8601Utc("2026-06-02T10:30:00Z");  // 12:30 local
  TEST_ASSERT_FALSE(needsNetwork(now, false, goodCache(), 3600u, false));
}

void test_dst_switch_window_forces_network() {
  // Second 02:xx of the 25 h day: the RTC-only local clock is ambiguous here,
  // so NTP must arbitrate even though the cache covers the hour.
  time_t now = parseIso8601Utc("2026-10-25T00:30:00Z");
  TEST_ASSERT_TRUE(needsNetwork(now, false, goodCache(), 3600u, false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_window_is_local_midnight_plus_48h);
  RUN_TEST(test_base_cadence_30min_before_noon);
  RUN_TEST(test_awaiting_tomorrow_20min_after_noon);
  RUN_TEST(test_after_noon_with_tomorrow_back_to_30min);
  RUN_TEST(test_wake_to_top_of_hour_plus_guard);
  RUN_TEST(test_wake_exactly_on_hour_waits_full_hour);
  RUN_TEST(test_wake_capped_at_30min_midday_without_tomorrow);
  RUN_TEST(test_wake_not_capped_midday_when_tomorrow_present);
  RUN_TEST(test_wake_not_capped_before_noon);
  RUN_TEST(test_backoff_first_failure_is_60s);
  RUN_TEST(test_backoff_doubles_per_failure);
  RUN_TEST(test_backoff_caps_at_one_hour);
  RUN_TEST(test_backoff_zero_failures_uses_base_delay);
  RUN_TEST(test_backoff_huge_counter_does_not_overflow);
  RUN_TEST(test_cache_hit_morning_wake_is_radio_free);
  RUN_TEST(test_button_wake_forces_network);
  RUN_TEST(test_cache_miss_forces_network);
  RUN_TEST(test_due_ota_check_forces_network);
  RUN_TEST(test_stale_ntp_forces_network);
  RUN_TEST(test_afternoon_without_tomorrow_hunts_online);
  RUN_TEST(test_afternoon_with_tomorrow_stays_radio_free);
  RUN_TEST(test_dst_switch_window_forces_network);
  return UNITY_END();
}
