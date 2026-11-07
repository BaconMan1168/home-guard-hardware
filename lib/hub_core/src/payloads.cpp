#include "payloads.h"

#include <ArduinoJson.h>

#include <cmath>

static double roundTo2(float c) { return std::round(static_cast<double>(c) * 100.0) / 100.0; }

static std::string toJson(const JsonDocument& doc) {
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool isPlausibleTemperature(float c) { return std::isfinite(c) && c >= -10.0f && c <= 60.0f; }

std::string telemetryJson(float c, int64_t nowMs) {
  JsonDocument doc;
  doc["temperatureCelsius"] = roundTo2(c);
  doc["lastSeenAt"] = nowMs;
  return toJson(doc);
}

std::string readingJson(float c, int64_t nowMs) {
  JsonDocument doc;
  doc["timestamp"] = nowMs;
  doc["temperatureCelsius"] = roundTo2(c);
  return toJson(doc);
}

std::string remoteJson(const StoredCode& code) {
  JsonDocument doc;
  doc["learned"] = code.present;
  if (code.present) {
    doc["learnedAt"] = code.learnedAt;
    doc["kind"] = codeKindName(code.kind);
    doc["protocol"] = code.protocolName;
  }
  return toJson(doc);
}

const char* commandStatusName(CommandStatus status) {
  switch (status) {
    case CommandStatus::Sent:
      return "sent";
    case CommandStatus::NoCode:
      return "no-code";
    case CommandStatus::Unsupported:
      return "unsupported";
    case CommandStatus::Stale:
      return "stale";
    case CommandStatus::Ignore:
      break;
  }
  return "ignore";
}

std::string commandResultJson(int64_t requestedAt, CommandStatus status, int64_t handledAt) {
  JsonDocument doc;
  doc["requestedAt"] = requestedAt;
  doc["status"] = commandStatusName(status);
  doc["handledAt"] = handledAt;
  return toJson(doc);
}

std::string learnStatusJson(int64_t requestedAt, LearnState state, int64_t updatedAt) {
  JsonDocument doc;
  doc["requestedAt"] = requestedAt;
  doc["state"] = learnStateName(state);
  doc["updatedAt"] = updatedAt;
  return toJson(doc);
}
