#pragma once

#include <string>

enum class SetupState { AwaitingSetup, Connecting, Connected };

void provisioningBegin(const std::string& bleName);
SetupState setupState();
void wipeWifiCredentials();
