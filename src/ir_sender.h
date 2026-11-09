#pragma once

#include <cstdint>

#include "stored_code.h"

void irSenderBegin();
bool irSend(const StoredCode& code, uint8_t times);
