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

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_window_is_local_midnight_plus_48h);
  RUN_TEST(test_base_cadence_30min_before_noon);
  RUN_TEST(test_awaiting_tomorrow_20min_after_noon);
  RUN_TEST(test_after_noon_with_tomorrow_back_to_30min);
  return UNITY_END();
}
