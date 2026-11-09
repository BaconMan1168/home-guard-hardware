#pragma once

#include <cstdint>
#include <string>

#include "stored_code.h"

struct Command {
  std::string value;
  int64_t requestedAt = 0;
};

enum class CommandStatus { Sent, Failed, NoCode, Unsupported, Stale, Ignore };

struct CommandDecision {
  CommandStatus status;
  uint8_t sendCount;
};

bool isFresh(int64_t requestedAt, int64_t nowMs);
bool shouldRememberCommand(int64_t requestedAt, int64_t nowMs);
CommandDecision decideCommand(const Command& cmd, int64_t lastHandledRequestedAt, int64_t nowMs,
                              const StoredCode& code);
