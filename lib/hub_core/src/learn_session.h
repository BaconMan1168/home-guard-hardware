#pragma once

#include <cstdint>

#include "stored_code.h"

enum class LearnState { Idle, Listening, Saved, Timeout, Rejected };

const char* learnStateName(LearnState state);

struct FrameInfo {
  uint16_t rawLen;
  bool overflow;
};

CodeKind classifyCode(bool decoded, bool hasAcState);
bool shouldStartLearn(int64_t requestedAt, int64_t lastLearnAt, int64_t nowMs);

class LearnSession {
 public:
  void start(int64_t requestedAt, uint32_t nowMs);
  LearnState onFrame(const FrameInfo& frame, uint32_t nowMs);
  LearnState tick(uint32_t nowMs);
  LearnState state() const { return state_; }
  int64_t requestedAt() const { return requestedAt_; }
  void reset();

 private:
  LearnState state_ = LearnState::Idle;
  int64_t requestedAt_ = 0;
  uint32_t startMs_ = 0;
};
