#include "token_time.h"

#include <ArduinoJson.h>

static int base64UrlValue(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

static std::optional<std::string> decodeBase64Url(const std::string& in) {
  std::string out;
  uint32_t bits = 0;
  int bitCount = 0;
  for (char c : in) {
    int v = base64UrlValue(c);
    if (v < 0) return std::nullopt;
    bits = (bits << 6) | static_cast<uint32_t>(v);
    bitCount += 6;
    if (bitCount >= 8) {
      bitCount -= 8;
      out.push_back(static_cast<char>((bits >> bitCount) & 0xFF));
    }
  }
  return out;
}

std::optional<int64_t> tokenIssuedAtSec(const std::string& jwt) {
  size_t first = jwt.find('.');
  size_t second = first == std::string::npos ? std::string::npos : jwt.find('.', first + 1);
  if (second == std::string::npos) return std::nullopt;

  std::optional<std::string> payload = decodeBase64Url(jwt.substr(first + 1, second - first - 1));
  if (!payload) return std::nullopt;
  JsonDocument doc;
  if (deserializeJson(doc, *payload) || !doc["iat"].is<int64_t>()) return std::nullopt;
  return doc["iat"].as<int64_t>();
}
