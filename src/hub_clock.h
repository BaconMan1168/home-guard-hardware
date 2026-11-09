#pragma once

#include <cstdint>

void clockBegin();
bool clockSynced();
void clockSetFromEpochSec(int64_t epochSec);
int64_t nowMs();
