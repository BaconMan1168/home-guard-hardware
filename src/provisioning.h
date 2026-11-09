#pragma once

#include <string>

enum class SetupState { AwaitingSetup, Connecting, Connected };

void provisioningBegin(const std::string& bleName);
void provisioningLoop();
SetupState setupState();
void wipeWifiCredentials();
