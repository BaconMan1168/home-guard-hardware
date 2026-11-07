#pragma once

#include <cstdint>
#include <string>

#include "command_gate.h"
#include "stored_code.h"

bool isPlausibleTemperature(float c);
std::string telemetryJson(float c, int64_t nowMs);
std::string readingJson(float c, int64_t nowMs);
std::string remoteJson(const StoredCode& code);
const char* commandStatusName(CommandStatus status);
std::string commandResultJson(int64_t requestedAt, CommandStatus status, int64_t handledAt);
