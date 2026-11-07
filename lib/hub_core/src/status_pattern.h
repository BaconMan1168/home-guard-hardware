#pragma once

#include <cstdint>

enum class HubMode { AwaitingSetup, Connecting, Online, Learning, Resetting };

bool ledOnAt(HubMode mode, uint32_t msSinceModeStart);
