#include "device_id.h"

#include <cstdio>

static std::string withMacSuffix(const char* prefix, const char* format, const uint8_t mac[6]) {
  char suffix[7];
  std::snprintf(suffix, sizeof(suffix), format, mac[3], mac[4], mac[5]);
  return std::string(prefix) + suffix;
}

std::string deviceIdFromMac(const uint8_t mac[6]) {
  return withMacSuffix("homeguard-", "%02x%02x%02x", mac);
}

std::string bleNameFromMac(const uint8_t mac[6]) {
  return withMacSuffix("HomeGuard-", "%02X%02X%02X", mac);
}
