#include <Arduino.h>
#include <esp_mac.h>
#include <esp_task_wdt.h>

#include <string>

#include "cloud.h"
#include "code_store.h"
#include "command_gate.h"
#include "device_id.h"
#include "hold_detector.h"
#include "hub_clock.h"
#include "hub_config.h"
#include "ir_learner.h"
#include "ir_sender.h"
#include "learn_session.h"
#include "low_heap_watch.h"
#include "payloads.h"
#include "provisioning.h"
#include "status_led.h"
#include "temp_sensor.h"

static StatusLed led;
static StoredCode code;
static int64_t lastCmdAt = 0;
static int64_t lastLearnAt = 0;
static LearnSession session;
static HoldDetector resetHold(hg::kFactoryResetHoldMs);
static std::string deviceId;
static bool cloudBegun = false;
static bool started = false;
static uint32_t lastTelemetryMs = 0;
static LowHeapWatch heapWatch(hg::kMinFreeHeapBlockBytes, hg::kLowHeapChecksBeforeRestart);
static uint32_t lastHeapCheckMs = 0;

static void onCommand(const Command& cmd) {
  CommandDecision d = decideCommand(cmd, lastCmdAt, nowMs(), code);
  if (d.status == CommandStatus::Ignore) return;

  saveCommandInFlight(cmd.requestedAt);
  if (shouldRememberCommand(cmd.requestedAt, nowMs())) {
    saveLastCommandAt(cmd.requestedAt);
    lastCmdAt = cmd.requestedAt;
  }
  CommandStatus status = d.status;
  if (status == CommandStatus::Sent && !irSend(code, d.sendCount)) status = CommandStatus::Failed;
  cloudSet("commands/result", commandResultJson(cmd.requestedAt, status, nowMs()));
  saveCommandInFlight(0);
  Serial.printf("Command %s at %lld: %s\n", cmd.value.c_str(), static_cast<long long>(cmd.requestedAt),
                commandStatusName(status));
}

static void reportInterruptedCommand() {
  int64_t requestedAt = loadCommandInFlight();
  if (requestedAt == 0) return;
  cloudSet("commands/result", commandResultJson(requestedAt, CommandStatus::Interrupted, nowMs()));
  saveCommandInFlight(0);
  Serial.println("Command interrupted by restart");
}

static void reportLearnStatus(LearnState state) {
  cloudSet("learn/status", learnStatusJson(session.requestedAt(), state, nowMs()));
  Serial.printf("Learn %s\n", learnStateName(state));
}

static void onLearnRequest(int64_t requestedAt) {
  if (!shouldStartLearn(requestedAt, lastLearnAt, nowMs())) return;

  saveLastLearnAt(requestedAt);
  saveLearnActive(true);
  lastLearnAt = requestedAt;
  session.start(requestedAt, millis());
  irLearnerEnable(true);
  reportLearnStatus(LearnState::Listening);
}

static void finishLearn(LearnState state, StoredCode& candidate) {
  if (state == LearnState::Saved) {
    candidate.present = true;
    candidate.learnedAt = nowMs();
    if (saveCode(candidate)) {
      code = candidate;
      cloudSet("remote", remoteJson(code));
    } else {
      state = LearnState::Rejected;
    }
  }
  reportLearnStatus(state);
  saveLearnActive(false);
  irLearnerEnable(false);
  session.reset();
}

static void reportInterruptedLearn() {
  if (!loadLearnActive()) return;
  cloudSet("learn/status", learnStatusJson(lastLearnAt, LearnState::Interrupted, nowMs()));
  saveLearnActive(false);
  Serial.println("Learn interrupted by restart");
}

static void learnLoop() {
  if (session.state() != LearnState::Listening) return;
  FrameInfo frame;
  StoredCode candidate;
  if (irLearnerPoll(frame, candidate)) session.onFrame(frame, millis());
  LearnState state = session.tick(millis());
  if (state != LearnState::Listening) finishLearn(state, candidate);
}

static void reportTemperature(bool online) {
  float t = 0;
  bool readOk = sensorRead(t) && isPlausibleTemperature(t);
  if (readOk) {
    Serial.printf("Temperature %.2f C\n", t);
  } else {
    Serial.println("Sensor read failed or implausible; sending telemetry without temperature");
  }
  if (!online) return;
  int64_t now = nowMs();
  if (!readOk) {
    cloudSet("telemetry", telemetryWithoutTemperatureJson(now));
    return;
  }
  cloudSet("telemetry", telemetryJson(t, now));
  cloudPush("temperatureReadings", readingJson(t, now));
}

static void heapLoop() {
  if (millis() - lastHeapCheckMs < hg::kHeapCheckIntervalMs) return;
  lastHeapCheckMs = millis();
  uint32_t largestBlock = ESP.getMaxAllocHeap();
  Serial.printf("Heap free %u, largest block %u\n", ESP.getFreeHeap(), largestBlock);
  if (heapWatch.check(largestBlock) && session.state() != LearnState::Listening) {
    Serial.println("Memory too low for a new connection; restarting");
    ESP.restart();
  }
}

static void factoryReset() {
  Serial.println("Factory reset");
  uint32_t startMs = millis();
  led.setMode(HubMode::Resetting, startMs);
  bool online = cloudBegun && cloudReady();
  if (online) cloudSet("remote", remoteJson(StoredCode{}));
  while (millis() - startMs < 1000) {
    if (online) cloudLoop();
    led.update(millis());
    delay(1);
  }
  clearAll();
  wipeWifiCredentials();
  ESP.restart();
}

static void logResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_BROWNOUT) {
    Serial.println("Restarted after a brownout: the 5 V supply sagged");
  } else {
    Serial.printf("Reset reason %d\n", static_cast<int>(reason));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("HomeGuard hub");
  logResetReason();

  pinMode(hg::kPinBootButton, INPUT_PULLUP);
  led.begin();
  led.setMode(HubMode::Connecting, millis());
  irSenderBegin();
  irLearnerBegin();
  storeBegin();
  code = loadCode();
  lastCmdAt = loadLastCommandAt();
  lastLearnAt = loadLastLearnAt();
  if (!sensorBegin()) Serial.println("AHT20 not found");

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  deviceId = deviceIdFromMac(mac);
  Serial.printf("Device ID %s\n", deviceId.c_str());

  provisioningBegin(bleNameFromMac(mac));
  clockBegin();

  esp_task_wdt_config_t watchdog = {hg::kLoopWatchdogMs, 1 << 0, true};
  esp_task_wdt_reconfigure(&watchdog);
  enableLoopWDT();
}

void loop() {
  if (resetHold.update(digitalRead(hg::kPinBootButton) == LOW, millis())) factoryReset();

  provisioningLoop();
  SetupState setupNow = setupState();
  bool wifi = setupNow == SetupState::Connected;
  if (wifi && !cloudBegun) {
    cloudBegin(deviceId, {onCommand, onLearnRequest});
    cloudBegun = true;
  }
  if (cloudBegun) cloudLoop();
  learnLoop();
  heapLoop();

  bool online = wifi && cloudBegun && cloudReady();
  if (online && !clockSynced()) {
    std::optional<int64_t> issuedAt = cloudTokenIssuedAtSec();
    if (issuedAt) {
      clockSetFromEpochSec(*issuedAt);
      Serial.println("NTP not synced yet; clock set from Firebase sign-in time");
    }
  }
  HubMode mode = online ? HubMode::Online : HubMode::Connecting;
  if (setupNow == SetupState::AwaitingSetup) mode = HubMode::AwaitingSetup;
  if (session.state() == LearnState::Listening) mode = HubMode::Learning;
  led.setMode(mode, millis());
  led.update(millis());

  if (!started && online && clockSynced()) {
    started = true;
    reportInterruptedCommand();
    reportInterruptedLearn();
    cloudStartStream();
    cloudSet("remote", remoteJson(code));
    lastTelemetryMs = millis();
    reportTemperature(online);
  }

  if (started && millis() - lastTelemetryMs >= hg::kTelemetryIntervalMs) {
    lastTelemetryMs = millis();
    reportTemperature(online);
  }
}
