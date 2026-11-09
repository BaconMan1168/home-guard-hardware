#include "low_heap_watch.h"

bool LowHeapWatch::check(uint32_t largestFreeBlockBytes) {
  lowChecks_ = largestFreeBlockBytes < minFreeBlockBytes_ ? lowChecks_ + 1 : 0;
  return lowChecks_ >= lowChecksBeforeRestart_;
}
