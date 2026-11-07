#include <unity.h>

#include "command_gate.h"

static const int64_t kNow = 1789262500000LL;
static StoredCode acCode;
static StoredCode toggleCode;
static StoredCode rawCode;
static StoredCode noCode;

void setUp() {
  acCode = StoredCode{};
  acCode.present = true;
  acCode.kind = CodeKind::AcState;
  toggleCode = StoredCode{};
  toggleCode.present = true;
  toggleCode.kind = CodeKind::Toggle;
  rawCode = StoredCode{};
  rawCode.present = true;
  rawCode.kind = CodeKind::Raw;
  noCode = StoredCode{};
}
void tearDown() {}

static void assertDecision(CommandStatus status, uint8_t sendCount, const CommandDecision& d) {
  TEST_ASSERT_EQUAL(static_cast<int>(status), static_cast<int>(d.status));
  TEST_ASSERT_EQUAL_UINT8(sendCount, d.sendCount);
}

void test_new_fresh_off_with_ac_state_sends_three() {
  assertDecision(CommandStatus::Sent, 3, decideCommand({"OFF", kNow - 1000}, 0, kNow, acCode));
}

void test_toggle_sends_once() {
  assertDecision(CommandStatus::Sent, 1, decideCommand({"OFF", kNow - 1000}, 0, kNow, toggleCode));
}

void test_raw_sends_once() {
  assertDecision(CommandStatus::Sent, 1, decideCommand({"OFF", kNow - 1000}, 0, kNow, rawCode));
}

void test_same_request_is_ignored() {
  assertDecision(CommandStatus::Ignore, 0, decideCommand({"OFF", kNow - 1000}, kNow - 1000, kNow, acCode));
}

void test_older_request_is_ignored() {
  assertDecision(CommandStatus::Ignore, 0, decideCommand({"OFF", kNow - 2000}, kNow - 1000, kNow, acCode));
}

void test_old_command_is_stale() {
  assertDecision(CommandStatus::Stale, 0, decideCommand({"OFF", kNow - 120001}, 0, kNow, acCode));
}

void test_future_within_window_is_fresh() {
  assertDecision(CommandStatus::Sent, 3, decideCommand({"OFF", kNow + 120000}, 0, kNow, acCode));
}

void test_future_beyond_window_is_stale() {
  assertDecision(CommandStatus::Stale, 0, decideCommand({"OFF", kNow + 120001}, 0, kNow, acCode));
}

void test_on_is_unsupported() {
  assertDecision(CommandStatus::Unsupported, 0, decideCommand({"ON", kNow}, 0, kNow, acCode));
}

void test_off_without_code_is_no_code() {
  assertDecision(CommandStatus::NoCode, 0, decideCommand({"OFF", kNow}, 0, kNow, noCode));
}

void test_stale_wins_over_no_code() {
  assertDecision(CommandStatus::Stale, 0, decideCommand({"OFF", kNow - 120001}, 0, kNow, noCode));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_new_fresh_off_with_ac_state_sends_three);
  RUN_TEST(test_toggle_sends_once);
  RUN_TEST(test_raw_sends_once);
  RUN_TEST(test_same_request_is_ignored);
  RUN_TEST(test_older_request_is_ignored);
  RUN_TEST(test_old_command_is_stale);
  RUN_TEST(test_future_within_window_is_fresh);
  RUN_TEST(test_future_beyond_window_is_stale);
  RUN_TEST(test_on_is_unsupported);
  RUN_TEST(test_off_without_code_is_no_code);
  RUN_TEST(test_stale_wins_over_no_code);
  return UNITY_END();
}
