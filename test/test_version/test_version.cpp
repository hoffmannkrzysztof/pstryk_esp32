#include <unity.h>
#include "core/Version.h"

using namespace pstryk;

void setUp() {}
void tearDown() {}

void test_newer_patch() { TEST_ASSERT_TRUE(isNewer("1.0.1", "1.0.0")); }
void test_newer_minor() { TEST_ASSERT_TRUE(isNewer("1.1.0", "1.0.9")); }
void test_newer_major() { TEST_ASSERT_TRUE(isNewer("2.0.0", "1.9.9")); }
void test_equal_not_newer() { TEST_ASSERT_FALSE(isNewer("1.2.3", "1.2.3")); }
void test_older_not_newer() { TEST_ASSERT_FALSE(isNewer("1.0.0", "1.0.1")); }
void test_leading_v_ignored() { TEST_ASSERT_TRUE(isNewer("v1.2.0", "v1.1.0")); }
void test_dev_is_dev() {
  TEST_ASSERT_TRUE(isDevVersion("0.0.0-dev"));
  TEST_ASSERT_TRUE(isDevVersion(""));
  TEST_ASSERT_TRUE(isDevVersion(nullptr));
}
void test_release_not_dev() { TEST_ASSERT_FALSE(isDevVersion("1.0.0")); }
void test_bare_zero_not_dev() { TEST_ASSERT_FALSE(isDevVersion("0.0.0")); }
void test_mixed_prefix() { TEST_ASSERT_TRUE(isNewer("v1.2.0", "1.1.0")); }

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_newer_patch);
  RUN_TEST(test_newer_minor);
  RUN_TEST(test_newer_major);
  RUN_TEST(test_equal_not_newer);
  RUN_TEST(test_older_not_newer);
  RUN_TEST(test_leading_v_ignored);
  RUN_TEST(test_dev_is_dev);
  RUN_TEST(test_release_not_dev);
  RUN_TEST(test_bare_zero_not_dev);
  RUN_TEST(test_mixed_prefix);
  return UNITY_END();
}
