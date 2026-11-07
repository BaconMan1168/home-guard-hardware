#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <string>

#include "cloud.h"
#include "code_store.h"
#include "command_gate.h"
#include "device_id.h"
#include "hub_clock.h"
#include "hub_config.h"
#include "ir_sender.h"
#include "payloads.h"
#include "secrets.h"
#include "status_led.h"
#include "temp_sensor.h"

static StatusLed led;
static StoredCode code;
static int64_t lastCmdAt = 0;
static std::string deviceId;
static bool cloudBegun = false;
static bool started = false;
static uint32_t lastTelemetryMs = 0;

static void onCommand(const Command& cmd) {
  CommandDecision d = decideCommand(cmd, lastCmdAt, nowMs(), code);
  if (d.status == CommandStatus::Ignore) return;

  saveLastCommandAt(cmd.requestedAt);
  lastCmdAt = cmd.requestedAt;
  if (d.status == CommandStatus::Sent) irSend(code, d.sendCount);
  cloudSet("commands/result", commandResultJson(cmd.requestedAt, d.status, nowMs()));
  Serial.printf("Command %s at %lld: %s\n", cmd.value.c_str(), static_cast<long long>(cmd.requestedAt),
                commandStatusName(d.status));
}

static void onLearnRequest(int64_t requestedAt) {
  Serial.printf("Learn request at %lld (not handled yet)\n", static_cast<long long>(requestedAt));
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

void setup() {
  Serial.begin(115200);
  Serial.println("HomeGuard hub");

  led.begin();
  led.setMode(HubMode::Connecting, millis());
  irSenderBegin();
  storeBegin();
  code = loadCode();
  lastCmdAt = loadLastCommandAt();
  if (!sensorBegin()) Serial.println("AHT20 not found");

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  deviceId = deviceIdFromMac(mac);
  Serial.printf("Device ID %s\n", deviceId.c_str());

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setAutoReconnect(true);
  clockBegin();
}

void loop() {
  bool wifi = WiFi.isConnected();
  if (wifi && !cloudBegun) {
    cloudBegin(deviceId, {onCommand, onLearnRequest});
    cloudBegun = true;
  }
  if (cloudBegun) cloudLoop();

  bool online = wifi && cloudBegun && cloudReady();
  led.setMode(online ? HubMode::Online : HubMode::Connecting, millis());
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
