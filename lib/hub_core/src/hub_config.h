#pragma once

#include <cstdint>

namespace hg {

constexpr uint8_t kPinIrSend = 4;
constexpr uint8_t kPinIrReceive = 14;
constexpr uint8_t kPinSda = 21;
constexpr uint8_t kPinScl = 22;
constexpr uint8_t kPinBootButton = 0;
constexpr uint8_t kPinStatusLed = 2;

constexpr uint32_t kTelemetryIntervalMs = 60000;
constexpr int64_t kCommandFreshnessMs = 120000;
constexpr uint8_t kAcStateRepeatCount = 3;
constexpr uint32_t kRepeatGapMs = 300;
constexpr uint32_t kLearnWindowMs = 30000;
constexpr uint16_t kNoiseMaxRawLen = 7;
constexpr uint16_t kMinFrameRawLen = 24;
constexpr uint32_t kFactoryResetHoldMs = 10000;
constexpr uint32_t kWifiRetryMs = 60000;
constexpr uint32_t kHeapCheckIntervalMs = 60000;
constexpr uint32_t kMinFreeHeapBlockBytes = 20000;
constexpr uint8_t kLowHeapChecksBeforeRestart = 3;
constexpr uint32_t kLoopWatchdogMs = 300000;
constexpr const char* kProofOfPossession = "homeguard-pop-v1";

}  // namespace hg
