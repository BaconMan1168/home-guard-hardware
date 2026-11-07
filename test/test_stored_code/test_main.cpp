#include <unity.h>

#include "stored_code.h"

void setUp() {}
void tearDown() {}

static void assertCodesEqual(const StoredCode& a, const StoredCode& b) {
  TEST_ASSERT_EQUAL(a.present, b.present);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(a.kind), static_cast<uint8_t>(b.kind));
  TEST_ASSERT_EQUAL_INT16(a.protocol, b.protocol);
  TEST_ASSERT_EQUAL_STRING(a.protocolName.c_str(), b.protocolName.c_str());
  TEST_ASSERT_EQUAL_UINT16(a.bits, b.bits);
  TEST_ASSERT_TRUE(a.value == b.value);
  TEST_ASSERT_TRUE(a.state == b.state);
  TEST_ASSERT_TRUE(a.raw == b.raw);
  TEST_ASSERT_TRUE(a.learnedAt == b.learnedAt);
}

void test_round_trip_ac_state() {
  StoredCode code;
  code.present = true;
  code.kind = CodeKind::AcState;
  code.protocol = 16;
  code.protocolName = "DAIKIN";
  code.bits = 280;
  for (int i = 0; i < 35; i++) code.state.push_back(static_cast<uint8_t>(i * 7));
  code.learnedAt = 1789262500000LL;

  StoredCode out;
  TEST_ASSERT_TRUE(deserializeCode(serializeCode(code), out));
  TEST_ASSERT_TRUE(out.present);
  assertCodesEqual(code, out);
}

void test_round_trip_raw() {
  StoredCode code;
  code.present = true;
  code.kind = CodeKind::Raw;
  code.protocolName = "UNKNOWN";
  for (int i = 0; i < 300; i++) code.raw.push_back(static_cast<uint16_t>(400 + i * 13));
  code.learnedAt = 1789262500000LL;

  StoredCode out;
  TEST_ASSERT_TRUE(deserializeCode(serializeCode(code), out));
  assertCodesEqual(code, out);
}

void test_deserialize_rejects_garbage() {
  StoredCode out;
  TEST_ASSERT_FALSE(deserializeCode({}, out));
  TEST_ASSERT_FALSE(out.present);

  TEST_ASSERT_FALSE(deserializeCode({0x07}, out));
  TEST_ASSERT_FALSE(out.present);

  StoredCode code;
  code.present = true;
  code.kind = CodeKind::Toggle;
  code.protocol = 3;
  code.protocolName = "NEC";
  code.bits = 32;
  code.value = 0x20DF10EF;
  std::vector<uint8_t> blob = serializeCode(code);
  blob.pop_back();
  TEST_ASSERT_FALSE(deserializeCode(blob, out));
  TEST_ASSERT_FALSE(out.present);
}

void test_kind_names() {
  TEST_ASSERT_EQUAL_STRING("ac-state", codeKindName(CodeKind::AcState));
  TEST_ASSERT_EQUAL_STRING("toggle", codeKindName(CodeKind::Toggle));
  TEST_ASSERT_EQUAL_STRING("raw", codeKindName(CodeKind::Raw));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_ac_state);
  RUN_TEST(test_round_trip_raw);
  RUN_TEST(test_deserialize_rejects_garbage);
  RUN_TEST(test_kind_names);
  return UNITY_END();
}
