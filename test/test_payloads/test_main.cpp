#include <ArduinoJson.h>
#include <unity.h>

#include <cmath>
#include <limits>

#include "payloads.h"

void setUp() {}
void tearDown() {}

static JsonDocument parse(const std::string& json) {
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  return doc;
}

void test_telemetry_rounds_to_two_decimals() {
  JsonDocument doc = parse(telemetryJson(22.4449f, 1789262500000LL));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.44f, doc["temperatureCelsius"].as<float>());
  TEST_ASSERT_TRUE(doc["lastSeenAt"].as<int64_t>() == 1789262500000LL);
  TEST_ASSERT_TRUE(doc["powerState"].isNull());
  TEST_ASSERT_EQUAL(2, doc.as<JsonObjectConst>().size());
}

void test_reading_fields() {
  JsonDocument doc = parse(readingJson(22.4449f, 1789262500000LL));
  TEST_ASSERT_TRUE(doc["timestamp"].as<int64_t>() == 1789262500000LL);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.44f, doc["temperatureCelsius"].as<float>());
  TEST_ASSERT_EQUAL(2, doc.as<JsonObjectConst>().size());
}

void test_remote_not_learned() {
  JsonDocument doc = parse(remoteJson(StoredCode{}));
  TEST_ASSERT_TRUE(doc["learned"].is<bool>());
  TEST_ASSERT_FALSE(doc["learned"].as<bool>());
  TEST_ASSERT_EQUAL(1, doc.as<JsonObjectConst>().size());
}

void test_remote_learned() {
  StoredCode code;
  code.present = true;
  code.kind = CodeKind::AcState;
  code.protocol = 16;
  code.protocolName = "DAIKIN";
  code.learnedAt = 1789262500000LL;
  JsonDocument doc = parse(remoteJson(code));
  TEST_ASSERT_TRUE(doc["learned"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("ac-state", doc["kind"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("DAIKIN", doc["protocol"].as<const char*>());
  TEST_ASSERT_TRUE(doc["learnedAt"].as<int64_t>() == 1789262500000LL);
}

void test_command_result() {
  JsonDocument doc = parse(commandResultJson(1789262500000LL, CommandStatus::Sent, 1789262501200LL));
  TEST_ASSERT_TRUE(doc["requestedAt"].as<int64_t>() == 1789262500000LL);
  TEST_ASSERT_EQUAL_STRING("sent", doc["status"].as<const char*>());
  TEST_ASSERT_TRUE(doc["handledAt"].as<int64_t>() == 1789262501200LL);

  TEST_ASSERT_EQUAL_STRING("no-code", parse(commandResultJson(1, CommandStatus::NoCode, 2))["status"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("stale", parse(commandResultJson(1, CommandStatus::Stale, 2))["status"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("failed", parse(commandResultJson(1, CommandStatus::Failed, 2))["status"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("unsupported",
                           parse(commandResultJson(1, CommandStatus::Unsupported, 2))["status"].as<const char*>());
}

void test_learn_status_json() {
  JsonDocument doc = parse(learnStatusJson(1789262500000LL, LearnState::Listening, 1789262500400LL));
  TEST_ASSERT_TRUE(doc["requestedAt"].as<int64_t>() == 1789262500000LL);
  TEST_ASSERT_EQUAL_STRING("listening", doc["state"].as<const char*>());
  TEST_ASSERT_TRUE(doc["updatedAt"].as<int64_t>() == 1789262500400LL);
  TEST_ASSERT_EQUAL(3, doc.as<JsonObjectConst>().size());

  TEST_ASSERT_EQUAL_STRING("timeout", parse(learnStatusJson(1, LearnState::Timeout, 2))["state"].as<const char*>());
}

void test_plausible_temperature_bounds() {
  TEST_ASSERT_TRUE(isPlausibleTemperature(22.4f));
  TEST_ASSERT_TRUE(isPlausibleTemperature(-10.0f));
  TEST_ASSERT_TRUE(isPlausibleTemperature(60.0f));
  TEST_ASSERT_FALSE(isPlausibleTemperature(NAN));
  TEST_ASSERT_FALSE(isPlausibleTemperature(-40.0f));
  TEST_ASSERT_FALSE(isPlausibleTemperature(85.0f));
  TEST_ASSERT_FALSE(isPlausibleTemperature(std::numeric_limits<float>::infinity()));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_telemetry_rounds_to_two_decimals);
  RUN_TEST(test_reading_fields);
  RUN_TEST(test_remote_not_learned);
  RUN_TEST(test_remote_learned);
  RUN_TEST(test_command_result);
  RUN_TEST(test_learn_status_json);
  RUN_TEST(test_plausible_temperature_bounds);
  return UNITY_END();
}
