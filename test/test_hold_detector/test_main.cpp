#include <unity.h>

#include "hold_detector.h"

void setUp() {}
void tearDown() {}

void test_hold_after_release_fires_at_threshold() {
  HoldDetector d(10000);
  TEST_ASSERT_FALSE(d.update(false, 0));
  TEST_ASSERT_FALSE(d.update(true, 100));
  TEST_ASSERT_FALSE(d.update(true, 10099));
  TEST_ASSERT_TRUE(d.update(true, 10100));
}

void test_release_before_threshold_does_not_fire() {
  HoldDetector d(10000);
  TEST_ASSERT_FALSE(d.update(false, 0));
  TEST_ASSERT_FALSE(d.update(true, 0));
  TEST_ASSERT_FALSE(d.update(true, 9899));
  TEST_ASSERT_FALSE(d.update(false, 9900));
  TEST_ASSERT_FALSE(d.update(false, 20000));
}

void test_fires_once_per_hold() {
  HoldDetector d(10000);
  d.update(false, 0);
  d.update(true, 0);
  TEST_ASSERT_TRUE(d.update(true, 10000));
  TEST_ASSERT_FALSE(d.update(true, 20000));
  TEST_ASSERT_FALSE(d.update(true, 40000));
  TEST_ASSERT_FALSE(d.update(false, 41000));
  TEST_ASSERT_FALSE(d.update(true, 42000));
  TEST_ASSERT_TRUE(d.update(true, 52000));
}

void test_hold_from_boot_does_not_fire() {
  HoldDetector d(10000);
  for (uint32_t t = 0; t <= 60000; t += 1000) TEST_ASSERT_FALSE(d.update(true, t));
  TEST_ASSERT_FALSE(d.update(false, 61000));
  TEST_ASSERT_FALSE(d.update(true, 62000));
  TEST_ASSERT_TRUE(d.update(true, 72000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hold_after_release_fires_at_threshold);
  RUN_TEST(test_release_before_threshold_does_not_fire);
  RUN_TEST(test_fires_once_per_hold);
  RUN_TEST(test_hold_from_boot_does_not_fire);
  return UNITY_END();
}
