#pragma once

#include <cstdint>

class LowHeapWatch {
 public:
  LowHeapWatch(uint32_t minFreeBlockBytes, uint8_t lowChecksBeforeRestart)
      : minFreeBlockBytes_(minFreeBlockBytes), lowChecksBeforeRestart_(lowChecksBeforeRestart) {}
  bool check(uint32_t largestFreeBlockBytes);

 private:
  uint32_t minFreeBlockBytes_;
  uint8_t lowChecksBeforeRestart_;
  uint8_t lowChecks_ = 0;
};
