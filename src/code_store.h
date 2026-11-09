#pragma once

#include <cstdint>
#include <string>

#include "stored_code.h"

void storeBegin();
StoredCode loadCode();
bool saveCode(const StoredCode& code);
int64_t loadLastCommandAt();
bool saveLastCommandAt(int64_t requestedAt);
int64_t loadLastLearnAt();
bool saveLastLearnAt(int64_t requestedAt);
bool loadLearnActive();
bool saveLearnActive(bool active);
std::string loadPendingWrite(const std::string& relPath);
bool savePendingWrite(const std::string& relPath, const std::string& json);
void clearPendingWrite(const std::string& relPath);
void clearAll();
