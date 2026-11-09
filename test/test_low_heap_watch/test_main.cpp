#include <unity.h>

#include "low_heap_watch.h"

void setUp() {}
void tearDown() {}

void test_restarts_after_consecutive_low_checks() {
  LowHeapWatch watch(20000, 3);
  TEST_ASSERT_FALSE(watch.check(19999));
  TEST_ASSERT_FALSE(watch.check(10000));
  TEST_ASSERT_TRUE(watch.check(15000));
}

void test_a_healthy_check_resets_the_count() {
  LowHeapWatch watch(20000, 3);
  TEST_ASSERT_FALSE(watch.check(10000));
  TEST_ASSERT_FALSE(watch.check(10000));
  TEST_ASSERT_FALSE(watch.check(20000));
  TEST_ASSERT_FALSE(watch.check(10000));
  TEST_ASSERT_FALSE(watch.check(10000));
  TEST_ASSERT_TRUE(watch.check(10000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_restarts_after_consecutive_low_checks);
  RUN_TEST(test_a_healthy_check_resets_the_count);
  return UNITY_END();
}
