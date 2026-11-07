#pragma once

#include <cstdint>

#include "stored_code.h"

void storeBegin();
StoredCode loadCode();
bool saveCode(const StoredCode& code);
int64_t loadLastCommandAt();
bool saveLastCommandAt(int64_t requestedAt);
int64_t loadLastLearnAt();
bool saveLastLearnAt(int64_t requestedAt);
void clearAll();
