# Hub Bluetooth Setup and Factory Reset Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hardcoded Wi-Fi details with Espressif's Bluetooth provisioning, so a customer sets up Wi-Fi from the phone, and add a 10-second BOOT-button hold that factory-resets the hub.

**Architecture:** Use the core's built-in `WiFiProv` library: BLE scheme, security level 1 with a fixed proof-of-possession, and Bluetooth freed after setup. Firebase starts only after setup finishes, so BLE and TLS never compete for RAM. The button-hold timing is a pure, tested class in `lib/hub_core`. Reset wipes Wi-Fi, the learned code and the last-handled timestamps, then restarts.

**Tech Stack:** Same as Hub Basics, plus `WiFiProv` (bundled with Arduino-ESP32 3.x).

**Spec:** `docs/superpowers/specs/2026-11-06-homeguard-hub-design.md` (sections "1. First-time setup", "5. Status light", "6. Factory reset").

**Requires:** the Hub Basics and Hub Teach Remote plans done.

## Global Constraints

- Everything in Hub Basics' Global Constraints still applies, except that `WIFI_SSID` / `WIFI_PASSWORD` are removed from `secrets.h` and `secrets.example.h` in this plan.
- Bluetooth name: `bleNameFromMac(mac)`, e.g. `HomeGuard-A1B2C3`. Proof-of-possession: `kProofOfPossession = "homeguard-pop-v1"` in `hub_config.h`. The app plan uses the same string.
- Provisioning: BLE scheme, security 1, handler that frees BT and BLE after provisioning (`…_HANDLER_FREE_BTDM`), default UUID, `reset_provisioned = false`.
- Factory reset: BOOT (GPIO0, active LOW) held ≥ 10 000 ms, counted only after the button has been seen released at least once since boot.
- Firebase (`cloudBegin`) starts only after Wi-Fi has an IP **and** provisioning has ended (or the hub was already provisioned).

## Review Focus

1. **Wrong Wi-Fi password typed in the app.** The hub must report the failure and accept new credentials without a reboot. Task 2 resets the provisioning state machine on credential failure. Board check: Task 3, step 3.
2. **Button held from power-on, or stuck LOW.** A hold that began before the first release must never trigger a reset. Pinned in Task 1 (`test_hold_from_boot_does_not_fire`).
3. **Button released at 9.9 s.** No reset. Pinned in Task 1 (`test_release_before_threshold_does_not_fire`).
4. **Holding past 10 s fires once, not repeatedly.** Pinned in Task 1 (`test_fires_once_per_hold`).
5. **Already-provisioned hub boots with the router off.** It must keep retrying Wi-Fi with the LED double-blinking, and must not fall back into Bluetooth setup and lose the saved network. Board check: Task 3, step 5.

---

### Task 1: Button hold detector

**Files:**
- Create: `lib/hub_core/src/hold_detector.{h,cpp}`
- Modify: `lib/hub_core/src/hub_config.h` (add `kFactoryResetHoldMs = 10000`, `kProofOfPossession`)
- Test: `test/test_hold_detector/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class HoldDetector {
   public:
    explicit HoldDetector(uint32_t holdMs);
    bool update(bool pressed, uint32_t nowMs);   // true exactly once per qualifying hold
  };
  ```

- [ ] **Step 1: Write the failing tests** (holdMs = 10000)

- `test_hold_after_release_fires_at_threshold`: released at 0, pressed at 100, `update(true, 10099)` → false, `update(true, 10100)` → true.
- `test_release_before_threshold_does_not_fire`: released, pressed at 0, released at 9900 → never true.
- `test_fires_once_per_hold`: after firing, `update(true, 20000)` and `update(true, 40000)` → false. Release, then hold again for 10 s → true.
- `test_hold_from_boot_does_not_fire`: the first sample is pressed, held to 60000 → false. Then release and hold 10 s → true.

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_hold_detector`
Expected: FAIL.

- [ ] **Step 3: Implement `HoldDetector` and the config constants**

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/hub_core test/test_hold_detector
git commit -m "feat(hub): add factory reset hold detector"
```

---

### Task 2: Bluetooth provisioning adapter

**Files:**
- Create: `src/provisioning.{h,cpp}`

**Interfaces:**
- Consumes: `bleNameFromMac`, `kProofOfPossession`, `HubMode`.
- Produces:
  ```cpp
  enum class SetupState { AwaitingSetup, Connecting, Connected };
  void provisioningBegin(const std::string& bleName);   // starts BLE setup, or STA if already provisioned
  SetupState setupState();       // Connected = got IP and provisioning not running
  void wipeWifiCredentials();    // WiFi.disconnect(true, true)
  ```

- [ ] **Step 1: Check the installed core's provisioning API**

Run: `ls ~/.platformio/packages/framework-arduinoespressif32/libraries/WiFiProv/examples/WiFiProv/ && grep -n "beginProvision\|_reset_wifi_sm_state_on_failure\|PROV_SCHEME_BLE\|PROV_SECURITY_1" -r ~/.platformio/packages/framework-arduinoespressif32/libraries/WiFiProv/src`

Expected: shows the exact `beginProvision` signature and the enum names for this core. Core 3.x uses `NETWORK_PROV_*`, and the older docs show `WIFI_PROV_*`. Use whatever the installed sources declare. The bundled example `WiFiProv.ino` is the reference.

- [ ] **Step 2: Implement**

- Register a network event handler. `ARDUINO_EVENT_PROV_START` → AwaitingSetup. `…PROV_CRED_RECV` → Connecting. `…PROV_CRED_FAIL` → call the core's `…_reset_wifi_sm_state_on_failure()` so the app can retry; stay AwaitingSetup. `…WIFI_STA_GOT_IP` → mark IP. `…PROV_END` → provisioning finished.
- If the hub is already provisioned, `beginProvision` goes straight to STA. Treat that as Connecting until the IP arrives.

- [ ] **Step 3: Build for the board**

Run: `pio run -e esp32dev`
Expected: success, and the app image still fits the `huge_app` partition (check the "Flash:" line in the output).

- [ ] **Step 4: Commit**

```bash
git add src/provisioning.*
git commit -m "feat(hub): add bluetooth wifi provisioning adapter"
```

---

### Task 3: Wire setup and reset into `main.cpp`, remove the temporary Wi-Fi config

**Files:**
- Modify: `src/main.cpp`, `include/secrets.example.h`, `docs/board-checklist.md`

**Interfaces:**
- Consumes: Tasks 1–2, plus Hub Basics' `code_store` (`clearAll`), `cloud`, `status_led`.

- [ ] **Step 1: Implement**

- Delete `WiFi.begin(WIFI_SSID, …)` and `WIFI_SSID`/`WIFI_PASSWORD` from `secrets.example.h` (and tell the user to delete them from their local `secrets.h`). Call `provisioningBegin(bleNameFromMac(mac))` instead.
- LED mode follows `setupState()`: AwaitingSetup → AwaitingSetup pattern. Connecting → Connecting. Connected and not `cloudReady()` → Connecting. Otherwise Online, or Learning while a learn session runs.
- `cloudBegin` is called the first time `setupState() == Connected`, not in `setup()`.
- Every loop: `if (hold.update(digitalRead(kPinBootButton) == LOW, millis()))`, then factory reset:
  1. LED Resetting for 1000 ms.
  2. If `cloudReady()`, `cloudSet("remote", remoteJson(StoredCode{}))`, best effort, and keep looping up to 1 s so it can send.
  3. `clearAll()`, then `wipeWifiCredentials()`, then `ESP.restart()`.
- Configure `pinMode(kPinBootButton, INPUT_PULLUP)`.

- [ ] **Step 2: Run all host tests and the board build**

Run: `pio test -e native && pio run -e esp32dev`
Expected: all PASS, and the build succeeds.

- [ ] **Step 3: Add the "Bluetooth setup and reset" section to `docs/board-checklist.md`**

Use Espressif's free **ESP BLE Provisioning** app (iOS/Android), so this works before the HomeGuard app's Add hub exists.
1. Fresh flash: the LED slow-blinks, and the phone sees `HomeGuard-XXXXXX`.
2. Connect with PoP `homeguard-pop-v1` and pick the home network. Enter the wrong password first: the app reports failure and the hub still accepts a retry. Then enter the right password: the LED double-blinks and then turns solid, and telemetry appears under `devices/homeguard-xxxxxx`.
3. Reboot: no Bluetooth advertising, and it reconnects straight to Wi-Fi.
4. Hold BOOT for 10 s: the LED blinks fast, `remote` becomes `{"learned":false}`, and the hub restarts slow-blinking. Wi-Fi and the learned code are gone.
5. Power off the router and reboot the hub: the LED keeps double-blinking with no Bluetooth advertising, and it recovers when the router comes back.
6. Hold BOOT for 5 s and release: nothing happens.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp include/secrets.example.h docs/board-checklist.md
git commit -m "feat(hub): bluetooth setup and factory reset"
```
