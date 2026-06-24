#include <unity.h>
#include "core/OtaPolicy.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

static OtaManifest mk(const char* board, const char* ver) {
  OtaManifest m; m.board = board; m.version = ver; m.url = "https://x/y.bin";
  return m;
}

void test_apply_when_newer_same_board() {
  TEST_ASSERT_TRUE(shouldApplyUpdate(mk("epaper", "1.4.0"), "1.3.0", "epaper"));
}
void test_skip_wrong_board() {
  TEST_ASSERT_FALSE(shouldApplyUpdate(mk("amoled", "1.4.0"), "1.3.0", "epaper"));
}
void test_skip_not_newer() {
  TEST_ASSERT_FALSE(shouldApplyUpdate(mk("epaper", "1.3.0"), "1.3.0", "epaper"));
}
void test_skip_dev_build() {
  TEST_ASSERT_FALSE(shouldApplyUpdate(mk("epaper", "9.9.9"), "0.0.0-dev", "epaper"));
}

void test_due_never_checked() { TEST_ASSERT_TRUE(dueForOtaCheck(0, 1000, 100)); }
void test_due_within_interval() { TEST_ASSERT_FALSE(dueForOtaCheck(1000, 1050, 100)); }
void test_due_past_interval() { TEST_ASSERT_TRUE(dueForOtaCheck(1000, 1200, 100)); }
void test_due_no_clock() { TEST_ASSERT_FALSE(dueForOtaCheck(1000, 0, 100)); }
void test_due_clock_backwards() { TEST_ASSERT_TRUE(dueForOtaCheck(2000, 1000, 100)); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_apply_when_newer_same_board);
  RUN_TEST(test_skip_wrong_board);
  RUN_TEST(test_skip_not_newer);
  RUN_TEST(test_skip_dev_build);
  RUN_TEST(test_due_never_checked);
  RUN_TEST(test_due_within_interval);
  RUN_TEST(test_due_past_interval);
  RUN_TEST(test_due_no_clock);
  RUN_TEST(test_due_clock_backwards);
  return UNITY_END();
}
