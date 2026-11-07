#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "command_gate.h"

struct StreamUpdate {
  std::optional<Command> latest;
  std::optional<int64_t> learnRequestAt;
};

StreamUpdate parseCommandsEvent(const std::string& dataPath, const std::string& data);
