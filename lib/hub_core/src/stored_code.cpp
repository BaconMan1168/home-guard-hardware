#include "stored_code.h"

namespace {

constexpr uint8_t kFormatVersion = 1;

void put(std::vector<uint8_t>& out, uint64_t v, int bytes) {
  for (int i = 0; i < bytes; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

class Reader {
 public:
  explicit Reader(const std::vector<uint8_t>& in) : in_(in) {}

  bool get(uint64_t& v, int bytes) {
    if (in_.size() - pos_ < static_cast<size_t>(bytes)) return false;
    v = 0;
    for (int i = 0; i < bytes; i++) v |= static_cast<uint64_t>(in_[pos_++]) << (8 * i);
    return true;
  }

  bool atEnd() const { return pos_ == in_.size(); }

 private:
  const std::vector<uint8_t>& in_;
  size_t pos_ = 0;
};

}  // namespace

const char* codeKindName(CodeKind kind) {
  switch (kind) {
    case CodeKind::AcState:
      return "ac-state";
    case CodeKind::Toggle:
      return "toggle";
    case CodeKind::Raw:
      return "raw";
  }
  return "raw";
}

std::vector<uint8_t> serializeCode(const StoredCode& code) {
  std::vector<uint8_t> out;
  put(out, kFormatVersion, 1);
  put(out, code.present ? 1 : 0, 1);
  put(out, static_cast<uint8_t>(code.kind), 1);
  put(out, static_cast<uint16_t>(code.protocol), 2);
  put(out, code.protocolName.size(), 2);
  for (char c : code.protocolName) put(out, static_cast<uint8_t>(c), 1);
  put(out, code.bits, 2);
  put(out, code.value, 8);
  put(out, code.state.size(), 2);
  for (uint8_t b : code.state) put(out, b, 1);
  put(out, code.raw.size(), 2);
  for (uint16_t t : code.raw) put(out, t, 2);
  put(out, static_cast<uint64_t>(code.learnedAt), 8);
  return out;
}

bool deserializeCode(const std::vector<uint8_t>& blob, StoredCode& out) {
  Reader r(blob);
  StoredCode code;
  uint64_t v = 0;

  if (!r.get(v, 1) || v != kFormatVersion) return false;
  if (!r.get(v, 1) || v > 1) return false;
  code.present = v == 1;
  if (!r.get(v, 1) || v > static_cast<uint8_t>(CodeKind::Raw)) return false;
  code.kind = static_cast<CodeKind>(v);
  if (!r.get(v, 2)) return false;
  code.protocol = static_cast<int16_t>(static_cast<uint16_t>(v));

  uint64_t len = 0;
  if (!r.get(len, 2)) return false;
  for (uint64_t i = 0; i < len; i++) {
    if (!r.get(v, 1)) return false;
    code.protocolName.push_back(static_cast<char>(v));
  }
  if (!r.get(v, 2)) return false;
  code.bits = static_cast<uint16_t>(v);
  if (!r.get(code.value, 8)) return false;

  if (!r.get(len, 2)) return false;
  for (uint64_t i = 0; i < len; i++) {
    if (!r.get(v, 1)) return false;
    code.state.push_back(static_cast<uint8_t>(v));
  }
  if (!r.get(len, 2)) return false;
  for (uint64_t i = 0; i < len; i++) {
    if (!r.get(v, 2)) return false;
    code.raw.push_back(static_cast<uint16_t>(v));
  }
  if (!r.get(v, 8)) return false;
  code.learnedAt = static_cast<int64_t>(v);
  if (!r.atEnd()) return false;

  out = code;
  return true;
}
