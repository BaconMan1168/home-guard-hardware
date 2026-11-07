#pragma once

#include <cstdint>

class HoldDetector {
 public:
  explicit HoldDetector(uint32_t holdMs) : holdMs_(holdMs) {}
  bool update(bool pressed, uint32_t nowMs);

 private:
  uint32_t holdMs_;
  bool seenRelease_ = false;
  bool holding_ = false;
  bool fired_ = false;
  uint32_t pressStartMs_ = 0;
};
