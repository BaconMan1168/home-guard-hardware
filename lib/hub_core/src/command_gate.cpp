#include "command_gate.h"

#include "hub_config.h"

bool isFresh(int64_t requestedAt, int64_t nowMs) {
  int64_t age = nowMs - requestedAt;
  return age <= hg::kCommandFreshnessMs && -age <= hg::kCommandFreshnessMs;
}

CommandDecision decideCommand(const Command& cmd, int64_t lastHandledRequestedAt, int64_t nowMs,
                              const StoredCode& code) {
  if (cmd.requestedAt <= lastHandledRequestedAt) return {CommandStatus::Ignore, 0};
  if (!isFresh(cmd.requestedAt, nowMs)) return {CommandStatus::Stale, 0};
  if (cmd.value != "OFF") return {CommandStatus::Unsupported, 0};
  if (!code.present) return {CommandStatus::NoCode, 0};
  uint8_t count = code.kind == CodeKind::AcState ? hg::kAcStateRepeatCount : 1;
  return {CommandStatus::Sent, count};
}
