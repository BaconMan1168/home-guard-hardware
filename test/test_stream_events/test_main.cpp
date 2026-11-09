#include <unity.h>

#include "stream_events.h"

void setUp() {}
void tearDown() {}

static void assertEmpty(const StreamUpdate& u) {
  TEST_ASSERT_FALSE(u.latest.has_value());
  TEST_ASSERT_FALSE(u.learnRequestAt.has_value());
}

void test_root_event_with_both_children() {
  StreamUpdate u = parseCommandsEvent(
      "/",
      R"({"latest":{"value":"OFF","requestedAt":1789262500000,"source":"app"},"result":{"status":"sent"},"learnRequest":{"requestedAt":1789262400000}})");
  TEST_ASSERT_TRUE(u.latest.has_value());
  TEST_ASSERT_EQUAL_STRING("OFF", u.latest->value.c_str());
  TEST_ASSERT_TRUE(u.latest->requestedAt == 1789262500000LL);
  TEST_ASSERT_TRUE(u.learnRequestAt.has_value());
  TEST_ASSERT_TRUE(*u.learnRequestAt == 1789262400000LL);
}

void test_latest_event() {
  StreamUpdate u = parseCommandsEvent(
      "/latest", R"({"value":"OFF","requestedAt":1789262500000,"requestedBy":"u1","source":"notification"})");
  TEST_ASSERT_TRUE(u.latest.has_value());
  TEST_ASSERT_EQUAL_STRING("OFF", u.latest->value.c_str());
  TEST_ASSERT_TRUE(u.latest->requestedAt == 1789262500000LL);
  TEST_ASSERT_FALSE(u.learnRequestAt.has_value());
}

void test_learn_request_event() {
  StreamUpdate u = parseCommandsEvent("/learnRequest", R"({"requestedAt":1789262600000})");
  TEST_ASSERT_FALSE(u.latest.has_value());
  TEST_ASSERT_TRUE(u.learnRequestAt.has_value());
  TEST_ASSERT_TRUE(*u.learnRequestAt == 1789262600000LL);
}

void test_result_echo_is_ignored() {
  assertEmpty(parseCommandsEvent("/result", R"({"requestedAt":1789262500000,"status":"sent","handledAt":1789262501200})"));
}

void test_partial_path_is_ignored() {
  assertEmpty(parseCommandsEvent("/latest/requestedAt", "1789262500000"));
}

void test_null_data_is_ignored() {
  assertEmpty(parseCommandsEvent("/", "null"));
}

void test_missing_fields_are_ignored() {
  assertEmpty(parseCommandsEvent("/latest", R"({"value":"OFF"})"));
  assertEmpty(parseCommandsEvent("/latest", R"({"requestedAt":"soon","value":"OFF"})"));
}

void test_root_event_skips_already_answered_command() {
  StreamUpdate u = parseCommandsEvent(
      "/",
      R"({"latest":{"value":"OFF","requestedAt":1789262500000},"result":{"requestedAt":1789262500000,"status":"sent"}})");
  TEST_ASSERT_FALSE(u.latest.has_value());
}

void test_root_event_keeps_unanswered_command() {
  StreamUpdate u = parseCommandsEvent(
      "/",
      R"({"latest":{"value":"OFF","requestedAt":1789262500000},"result":{"requestedAt":1789262400000,"status":"sent"}})");
  TEST_ASSERT_TRUE(u.latest.has_value());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_root_event_with_both_children);
  RUN_TEST(test_latest_event);
  RUN_TEST(test_learn_request_event);
  RUN_TEST(test_result_echo_is_ignored);
  RUN_TEST(test_partial_path_is_ignored);
  RUN_TEST(test_null_data_is_ignored);
  RUN_TEST(test_missing_fields_are_ignored);
  RUN_TEST(test_root_event_skips_already_answered_command);
  RUN_TEST(test_root_event_keeps_unanswered_command);
  return UNITY_END();
}
