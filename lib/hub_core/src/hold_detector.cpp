#include "hold_detector.h"

bool HoldDetector::update(bool pressed, uint32_t nowMs) {
  if (!pressed) {
    seenRelease_ = true;
    holding_ = false;
    fired_ = false;
    return false;
  }
  if (!seenRelease_ || fired_) return false;
  if (!holding_) {
    holding_ = true;
    pressStartMs_ = nowMs;
  }
  if (nowMs - pressStartMs_ < holdMs_) return false;
  fired_ = true;
  return true;
}
