#include <Arduino.h>
#include <esp_mac.h>

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

static void onCommand(const Command& cmd) {
  CommandDecision d = decideCommand(cmd, lastCmdAt, nowMs(), code);
  if (d.status == CommandStatus::Ignore) return;

  if (shouldRememberCommand(cmd.requestedAt, nowMs())) {
    saveLastCommandAt(cmd.requestedAt);
    lastCmdAt = cmd.requestedAt;
  }
  CommandStatus status = d.status;
  if (status == CommandStatus::Sent && !irSend(code, d.sendCount)) status = CommandStatus::Failed;
  cloudSet("commands/result", commandResultJson(cmd.requestedAt, status, nowMs()));
  Serial.printf("Command %s at %lld: %s\n", cmd.value.c_str(), static_cast<long long>(cmd.requestedAt),
                commandStatusName(status));
}

static void reportLearnStatus(LearnState state) {
  cloudSet("learn/status", learnStatusJson(session.requestedAt(), state, nowMs()));
  Serial.printf("Learn %s\n", learnStateName(state));
}

static void onLearnRequest(int64_t requestedAt) {
  if (!shouldStartLearn(requestedAt, lastLearnAt, nowMs())) return;

  saveLastLearnAt(requestedAt);
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
  irLearnerEnable(false);
  session.reset();
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
  if (!sensorRead(t) || !isPlausibleTemperature(t)) {
    Serial.println("Sensor read failed or implausible; skipping this cycle");
    return;
  }
  Serial.printf("Temperature %.2f C\n", t);
  if (!online) return;
  int64_t now = nowMs();
  cloudSet("telemetry", telemetryJson(t, now));
  cloudPush("temperatureReadings", readingJson(t, now));
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

void setup() {
  Serial.begin(115200);
  Serial.println("HomeGuard hub");

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

  bool online = wifi && cloudBegun && cloudReady();
  HubMode mode = online ? HubMode::Online : HubMode::Connecting;
  if (setupNow == SetupState::AwaitingSetup) mode = HubMode::AwaitingSetup;
  if (session.state() == LearnState::Listening) mode = HubMode::Learning;
  led.setMode(mode, millis());
  led.update(millis());

  if (!started && online && clockSynced()) {
    started = true;
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
