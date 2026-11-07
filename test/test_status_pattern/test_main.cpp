#include <unity.h>

#include "status_pattern.h"

void setUp() {}
void tearDown() {}

void test_awaiting_setup_slow_blink() {
  TEST_ASSERT_TRUE(ledOnAt(HubMode::AwaitingSetup, 0));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::AwaitingSetup, 999));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::AwaitingSetup, 1000));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::AwaitingSetup, 1999));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::AwaitingSetup, 2000));
}

void test_connecting_double_blink() {
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Connecting, 0));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Connecting, 149));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Connecting, 300));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Connecting, 449));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::Connecting, 150));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::Connecting, 299));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::Connecting, 450));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::Connecting, 1499));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Connecting, 1500));
}

void test_online_solid() {
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Online, 0));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Online, 12345));
}

void test_learning_fast_blink() {
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Learning, 0));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::Learning, 100));
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Learning, 200));
}

void test_resetting_faster_blink() {
  TEST_ASSERT_TRUE(ledOnAt(HubMode::Resetting, 0));
  TEST_ASSERT_FALSE(ledOnAt(HubMode::Resetting, 50));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_awaiting_setup_slow_blink);
  RUN_TEST(test_connecting_double_blink);
  RUN_TEST(test_online_solid);
  RUN_TEST(test_learning_fast_blink);
  RUN_TEST(test_resetting_faster_blink);
  return UNITY_END();
}
