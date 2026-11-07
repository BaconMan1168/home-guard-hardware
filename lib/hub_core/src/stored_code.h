#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class CodeKind : uint8_t { AcState = 0, Toggle = 1, Raw = 2 };

const char* codeKindName(CodeKind kind);

struct StoredCode {
  bool present = false;
  CodeKind kind = CodeKind::Raw;
  int16_t protocol = -1;
  std::string protocolName;
  uint16_t bits = 0;
  uint64_t value = 0;
  std::vector<uint8_t> state;
  std::vector<uint16_t> raw;
  int64_t learnedAt = 0;
};

std::vector<uint8_t> serializeCode(const StoredCode& code);
bool deserializeCode(const std::vector<uint8_t>& blob, StoredCode& out);
