#include "status_pattern.h"

static bool blink(uint32_t t, uint32_t onMs, uint32_t offMs) { return t % (onMs + offMs) < onMs; }

bool ledOnAt(HubMode mode, uint32_t msSinceModeStart) {
  uint32_t t = msSinceModeStart;
  switch (mode) {
    case HubMode::AwaitingSetup:
      return blink(t, 1000, 1000);
    case HubMode::Connecting: {
      uint32_t phase = t % 1500;
      return phase < 150 || (phase >= 300 && phase < 450);
    }
    case HubMode::Online:
      return true;
    case HubMode::Learning:
      return blink(t, 100, 100);
    case HubMode::Resetting:
      return blink(t, 50, 50);
  }
  return false;
}
