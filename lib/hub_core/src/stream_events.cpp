#include "stream_events.h"

#include <ArduinoJson.h>

static std::optional<Command> parseLatest(JsonVariantConst v) {
  if (!v["value"].is<const char*>() || !v["requestedAt"].is<int64_t>()) return std::nullopt;
  return Command{v["value"].as<const char*>(), v["requestedAt"].as<int64_t>()};
}

static std::optional<int64_t> parseLearnRequest(JsonVariantConst v) {
  if (!v["requestedAt"].is<int64_t>()) return std::nullopt;
  return v["requestedAt"].as<int64_t>();
}

StreamUpdate parseCommandsEvent(const std::string& dataPath, const std::string& data) {
  StreamUpdate update;
  JsonDocument doc;
  if (deserializeJson(doc, data) || !doc.is<JsonObjectConst>()) return update;

  JsonVariantConst root = doc.as<JsonVariantConst>();
  if (dataPath == "/") {
    update.latest = parseLatest(root["latest"]);
    update.learnRequestAt = parseLearnRequest(root["learnRequest"]);
  } else if (dataPath == "/latest") {
    update.latest = parseLatest(root);
  } else if (dataPath == "/learnRequest") {
    update.learnRequestAt = parseLearnRequest(root);
  }
  return update;
}
