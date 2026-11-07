#include "hub_clock.h"

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

static constexpr time_t kEarliestValidEpoch = 1704067200;

void clockBegin() { configTime(0, 0, "pool.ntp.org", "time.google.com"); }

bool clockSynced() { return time(nullptr) > kEarliestValidEpoch; }

int64_t nowMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}
