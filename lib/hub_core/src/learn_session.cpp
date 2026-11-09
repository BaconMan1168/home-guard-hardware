#include "learn_session.h"

#include "command_gate.h"
#include "hub_config.h"

const char* learnStateName(LearnState state) {
  switch (state) {
    case LearnState::Idle:
      break;
    case LearnState::Listening:
      return "listening";
    case LearnState::Saved:
      return "saved";
    case LearnState::Timeout:
      return "timeout";
    case LearnState::Rejected:
      return "rejected";
  }
  return "idle";
}

CodeKind classifyCode(bool decoded, bool hasAcState, bool hasPowerToggle) {
  if (!decoded) return CodeKind::Raw;
  return hasAcState && !hasPowerToggle ? CodeKind::AcState : CodeKind::Toggle;
}

bool shouldStartLearn(int64_t requestedAt, int64_t lastLearnAt, int64_t nowMs) {
  return requestedAt > lastLearnAt && isFresh(requestedAt, nowMs);
}

void LearnSession::start(int64_t requestedAt, uint32_t nowMs) {
  state_ = LearnState::Listening;
  requestedAt_ = requestedAt;
  startMs_ = nowMs;
}

LearnState LearnSession::onFrame(const FrameInfo& frame, uint32_t nowMs) {
  if (state_ != LearnState::Listening || tick(nowMs) != LearnState::Listening) return state_;
  if (frame.overflow) {
    state_ = LearnState::Rejected;
  } else if (frame.rawLen > hg::kNoiseMaxRawLen) {
    state_ = frame.rawLen >= hg::kMinFrameRawLen ? LearnState::Saved : LearnState::Rejected;
  }
  return state_;
}

LearnState LearnSession::tick(uint32_t nowMs) {
  if (state_ == LearnState::Listening && nowMs - startMs_ >= hg::kLearnWindowMs) state_ = LearnState::Timeout;
  return state_;
}

void LearnSession::reset() { state_ = LearnState::Idle; }
