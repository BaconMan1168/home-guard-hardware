#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "command_gate.h"

struct CloudCallbacks {
  std::function<void(const Command&)> onCommand;
  std::function<void(int64_t requestedAt)> onLearnRequest;
};

void cloudBegin(const std::string& deviceId, CloudCallbacks callbacks);
void cloudLoop();
bool cloudReady();
std::optional<int64_t> cloudTokenIssuedAtSec();
void cloudStartStream();
void cloudSet(const std::string& relPath, const std::string& json);
void cloudPush(const std::string& relPath, const std::string& json);
