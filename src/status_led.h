#pragma once

#include <cstdint>

#include "status_pattern.h"

class StatusLed {
 public:
  void begin();
  void setMode(HubMode mode, uint32_t nowMs);
  void update(uint32_t nowMs);

 private:
  HubMode mode_ = HubMode::Connecting;
  uint32_t modeStartMs_ = 0;
  bool on_ = false;
};
