#pragma once

#include <cstdint>
#include <string>

std::string deviceIdFromMac(const uint8_t mac[6]);
std::string bleNameFromMac(const uint8_t mac[6]);
