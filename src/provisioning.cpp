#include "provisioning.h"

#include <WiFi.h>
#include <WiFiProv.h>

#include <atomic>

#include "hub_config.h"

static std::string serviceName;
static std::atomic<bool> provisioning{false};
static std::atomic<bool> awaitingCredentials{false};

static void onProvisioningEvent(arduino_event_t* event) {
  switch (event->event_id) {
    case ARDUINO_EVENT_PROV_START:
      provisioning = true;
      awaitingCredentials = true;
      Serial.println("Bluetooth setup started");
      break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
      awaitingCredentials = false;
      Serial.println("Wi-Fi credentials received");
      break;
    case ARDUINO_EVENT_PROV_CRED_FAIL:
      Serial.println("Wi-Fi join failed; waiting for new credentials");
      network_prov_mgr_reset_wifi_sm_state_on_failure();
      awaitingCredentials = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.println("Wi-Fi got IP");
      break;
    case ARDUINO_EVENT_PROV_END:
      provisioning = false;
      awaitingCredentials = false;
      Serial.println("Bluetooth setup ended");
      break;
    default:
      break;
  }
}

void provisioningBegin(const std::string& bleName) {
  serviceName = bleName;
  WiFi.onEvent(onProvisioningEvent);
  WiFi.setAutoReconnect(true);
  WiFiProv.beginProvision(NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BTDM, NETWORK_PROV_SECURITY_1,
                          hg::kProofOfPossession, serviceName.c_str(), nullptr, nullptr, false);
}

SetupState setupState() {
  if (provisioning && awaitingCredentials) return SetupState::AwaitingSetup;
  if (provisioning || !WiFi.isConnected()) return SetupState::Connecting;
  return SetupState::Connected;
}

void wipeWifiCredentials() { WiFi.disconnect(true, true, 1000); }
