#include <unity.h>

#include "learn_session.h"

static const int64_t kNow = 1789262500000LL;

void setUp() {}
void tearDown() {}

static void assertState(LearnState expected, LearnState actual) {
  TEST_ASSERT_EQUAL_STRING(learnStateName(expected), learnStateName(actual));
}

static LearnSession listening() {
  LearnSession s;
  s.start(kNow - 1000, 0);
  return s;
}

void test_fresh_new_request_starts() { TEST_ASSERT_TRUE(shouldStartLearn(kNow - 1000, 0, kNow)); }

void test_repeat_request_does_not_start() {
  TEST_ASSERT_FALSE(shouldStartLearn(kNow - 1000, kNow - 1000, kNow));
  TEST_ASSERT_FALSE(shouldStartLearn(kNow - 2000, kNow - 1000, kNow));
}

void test_stale_request_does_not_start() { TEST_ASSERT_FALSE(shouldStartLearn(kNow - 120001, 0, kNow)); }

void test_long_frame_is_saved() {
  LearnSession s = listening();
  assertState(LearnState::Saved, s.onFrame({200, false}, 5000));
  assertState(LearnState::Saved, s.state());
}

void test_noise_frames_are_ignored() {
  LearnSession s = listening();
  assertState(LearnState::Listening, s.onFrame({4, false}, 1000));
  assertState(LearnState::Listening, s.onFrame({7, false}, 2000));
  assertState(LearnState::Saved, s.onFrame({100, false}, 3000));
}

void test_mid_length_frame_is_rejected() {
  LearnSession a = listening();
  assertState(LearnState::Rejected, a.onFrame({8, false}, 1000));
  LearnSession b = listening();
  assertState(LearnState::Rejected, b.onFrame({23, false}, 1000));
}

void test_overflow_is_rejected() {
  LearnSession s = listening();
  assertState(LearnState::Rejected, s.onFrame({1024, true}, 1000));
}

void test_timeout_after_window() {
  LearnSession s = listening();
  assertState(LearnState::Listening, s.tick(29999));
  assertState(LearnState::Timeout, s.tick(30000));
}

void test_frames_after_finish_are_ignored() {
  LearnSession s = listening();
  s.onFrame({200, false}, 5000);
  assertState(LearnState::Saved, s.onFrame({8, false}, 6000));
  assertState(LearnState::Saved, s.tick(99999));
}

void test_new_request_restarts_session() {
  const int64_t rA = kNow - 30000;
  const int64_t rB = kNow - 10000;
  LearnSession s;
  s.start(rA, 0);
  s.start(rB, 20000);
  assertState(LearnState::Listening, s.tick(45000));
  TEST_ASSERT_TRUE(s.requestedAt() == rB);
  assertState(LearnState::Timeout, s.tick(50000));
}

void test_reset_returns_to_idle() {
  LearnSession s = listening();
  s.reset();
  assertState(LearnState::Idle, s.state());
  assertState(LearnState::Idle, s.onFrame({200, false}, 1000));
}

void test_classify() {
  TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::AcState), static_cast<int>(classifyCode(true, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::Toggle), static_cast<int>(classifyCode(true, false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::Raw), static_cast<int>(classifyCode(false, false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::Raw), static_cast<int>(classifyCode(false, true, false)));
}

void test_ac_state_with_power_toggle_is_toggle() {
  TEST_ASSERT_EQUAL(static_cast<int>(CodeKind::Toggle), static_cast<int>(classifyCode(true, true, true)));
}

void test_state_names() {
  TEST_ASSERT_EQUAL_STRING("idle", learnStateName(LearnState::Idle));
  TEST_ASSERT_EQUAL_STRING("listening", learnStateName(LearnState::Listening));
  TEST_ASSERT_EQUAL_STRING("saved", learnStateName(LearnState::Saved));
  TEST_ASSERT_EQUAL_STRING("timeout", learnStateName(LearnState::Timeout));
  TEST_ASSERT_EQUAL_STRING("rejected", learnStateName(LearnState::Rejected));
  TEST_ASSERT_EQUAL_STRING("interrupted", learnStateName(LearnState::Interrupted));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fresh_new_request_starts);
  RUN_TEST(test_repeat_request_does_not_start);
  RUN_TEST(test_stale_request_does_not_start);
  RUN_TEST(test_long_frame_is_saved);
  RUN_TEST(test_noise_frames_are_ignored);
  RUN_TEST(test_mid_length_frame_is_rejected);
  RUN_TEST(test_overflow_is_rejected);
  RUN_TEST(test_timeout_after_window);
  RUN_TEST(test_frames_after_finish_are_ignored);
  RUN_TEST(test_new_request_restarts_session);
  RUN_TEST(test_reset_returns_to_idle);
  RUN_TEST(test_classify);
  RUN_TEST(test_ac_state_with_power_toggle_is_toggle);
  RUN_TEST(test_state_names);
  return UNITY_END();
}
