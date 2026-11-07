# Hub Basics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ESP32 firmware that reads the AHT20, joins Wi-Fi from a temporary local config, signs in to Firebase anonymously, reports temperature every 60 s, listens for OFF commands, fires a stored OFF code under the safety rules, and writes a result for each command.

**Architecture:** All decision logic lives in a pure C++17 library (`lib/hub_core`, no Arduino headers). It is unit-tested on the laptop with PlatformIO's `native` environment. Thin adapters in `src/` wrap the hardware and cloud (AHT20, Preferences, IRsend, NTP, FirebaseClient, LED). `main.cpp` wires the adapters to the logic. Nothing here needs the physical board: the board-level checks are a checklist for later.

**Tech Stack:** PlatformIO, Arduino-ESP32 core 3.x (pioarduino platform), `mobizt/FirebaseClient`, `crankyoldgit/IRremoteESP8266`, `adafruit/Adafruit AHTX0`, `bblanchon/ArduinoJson` v7, Unity (PlatformIO test runner).

**Spec:** `docs/superpowers/specs/2026-11-06-homeguard-hub-design.md` and `docs/firebase-contract.md`. Read both before starting.

## Global Constraints

- Board: `esp32dev` (ESP32-WROOM-32 DevKit, CH340). Partition table `huge_app.csv` (no OTA; OTA is a non-goal).
- Pins: IR send GPIO4, IR receive GPIO14, I²C SDA GPIO21 / SCL GPIO22 (AHT20 at 0x38), BOOT button GPIO0, status LED GPIO2.
- GPIO4 is never driven HIGH outside IRsend's modulated output.
- Firebase paths and JSON field names are exactly as in `docs/firebase-contract.md`. Base path `devices/{deviceId}`.
- `deviceId` = `homeguard-` + last 3 bytes of the Wi-Fi STA MAC, lowercase hex.
- All timestamps are epoch milliseconds, `int64_t`.
- Temperatures are published rounded to 2 decimals. The app's trend checks work in 0.005 °C/min and 0.15 °C steps, so 1 decimal is too coarse.
- Telemetry/reading interval: 60 000 ms. Command freshness window: 120 000 ms, either direction.
- Firing: `ac-state` codes 3 times, 300 ms apart. `toggle` and `raw` codes once.
- The hub never writes `powerState` or `powerStateUpdatedAt`.
- `include/secrets.h` is gitignored. Only `include/secrets.example.h` is committed.
- Commits: conventional format, no co-author trailer. Branch: the current branch.

## Review Focus

1. **Old command replayed after reboot or reconnect.** The stream's first event re-delivers the current `commands/latest`. It must be ignored when its `requestedAt` ≤ the persisted last-handled value. Pinned in Task 2 (`test_same_request_is_ignored`, `test_older_request_is_ignored`).
2. **Hub clock slightly behind Firebase's server clock.** A command stamped up to 120 s in the future still counts as fresh. Commands are not judged at all until NTP has synced. Pinned in Task 2 (`test_future_within_window_is_fresh`). Enforced in Task 8 (stream starts only after sync).
3. **Stream payload shapes.** Path `/` with the whole object, `/latest` with an object, a partial path like `/latest/requestedAt`, `null` data, and the hub's own `/result` echo. Only complete commands are surfaced. Pinned in Task 3.
4. **Sensor failure or nonsense reading** (NaN, −40, 85). Skip the cycle and write nothing. Pinned in Task 4 (`test_plausible_temperature_bounds`). Enforced in Task 8.
5. **Corrupted or old-format flash data.** Deserializing a stored code must fail cleanly, and the hub then behaves as "no code learned". Pinned in Task 2 (`test_deserialize_rejects_garbage`).

---

## File Structure

```
platformio.ini
.gitignore
include/secrets.example.h      Wi-Fi + Firebase placeholders (committed)
include/secrets.h              real values (gitignored)
lib/hub_core/library.json
lib/hub_core/src/hub_config.h      pins, intervals, limits (constexpr)
lib/hub_core/src/device_id.{h,cpp} MAC → deviceId / Bluetooth name
lib/hub_core/src/stored_code.{h,cpp} CodeKind, StoredCode, (de)serialize
lib/hub_core/src/command_gate.{h,cpp} Command, decideCommand, isFresh
lib/hub_core/src/stream_events.{h,cpp} parse commands-stream events
lib/hub_core/src/payloads.{h,cpp}  JSON written to Firebase
lib/hub_core/src/status_pattern.{h,cpp} HubMode → LED on/off
test/test_device_id/test_main.cpp
test/test_stored_code/test_main.cpp
test/test_command_gate/test_main.cpp
test/test_stream_events/test_main.cpp
test/test_payloads/test_main.cpp
test/test_status_pattern/test_main.cpp
src/main.cpp                   wiring and the main loop
src/hub_clock.{h,cpp}          NTP sync, nowMs()
src/temp_sensor.{h,cpp}        AHT20
src/status_led.{h,cpp}         drives GPIO2 from status_pattern
src/code_store.{h,cpp}         Preferences: code + last-handled timestamps
src/ir_sender.{h,cpp}          IRsend on GPIO4
src/cloud.{h,cpp}              FirebaseClient: auth, writes, commands stream
```

---

### Task 1: Project scaffold and device ID

**Files:**
- Create: `platformio.ini`, `.gitignore`, `include/secrets.example.h`, `lib/hub_core/library.json`, `lib/hub_core/src/hub_config.h`, `lib/hub_core/src/device_id.h`, `lib/hub_core/src/device_id.cpp`, `src/main.cpp` (placeholder: `setup()` prints "HomeGuard hub" at 115200, empty `loop()`)
- Test: `test/test_device_id/test_main.cpp`

**Interfaces:**
- Produces: `std::string deviceIdFromMac(const uint8_t mac[6])`, `std::string bleNameFromMac(const uint8_t mac[6])`. All constants in `hub_config.h` under `namespace hg`: `kPinIrSend = 4`, `kPinIrReceive = 14`, `kPinSda = 21`, `kPinScl = 22`, `kPinBootButton = 0`, `kPinStatusLed = 2`, `kTelemetryIntervalMs = 60000`, `kCommandFreshnessMs = 120000`, `kAcStateRepeatCount = 3`, `kRepeatGapMs = 300`.

- [ ] **Step 1: Install PlatformIO if missing**

Run: `pio --version || brew install platformio`
Expected: prints `PlatformIO Core, version 6.x` or newer.

- [ ] **Step 2: Write `platformio.ini`**

```ini
[platformio]
default_envs = esp32dev

[env]
lib_deps = bblanchon/ArduinoJson@^7
build_flags = -std=gnu++17
build_unflags = -std=gnu++11

[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino
board_build.partitions = huge_app.csv
monitor_speed = 115200
lib_deps =
  ${env.lib_deps}
  mobizt/FirebaseClient
  crankyoldgit/IRremoteESP8266
  adafruit/Adafruit AHTX0

[env:native]
platform = native
test_framework = unity
```

`lib/hub_core/library.json`: name `hub_core`, `"build": {"flags": "-std=gnu++17"}`, no `frameworks` key (so both environments accept it). `.gitignore`: `.pio/`, `include/secrets.h`, `.vscode/`. `secrets.example.h` defines `WIFI_SSID`, `WIFI_PASSWORD`, `FIREBASE_API_KEY`, `FIREBASE_DATABASE_URL` as empty string literals, with a one-line note to copy it to `secrets.h`.

- [ ] **Step 3: Write the failing test**

```cpp
// test/test_device_id/test_main.cpp
const uint8_t kMac[6] = {0x24, 0x6F, 0x28, 0xA1, 0xB2, 0xC3};
void test_device_id_uses_last_three_bytes_lowercase() {
  TEST_ASSERT_EQUAL_STRING("homeguard-a1b2c3", deviceIdFromMac(kMac).c_str());
}
void test_ble_name_uses_last_three_bytes_uppercase() {
  TEST_ASSERT_EQUAL_STRING("HomeGuard-A1B2C3", bleNameFromMac(kMac).c_str());
}
void test_leading_zero_bytes_are_padded() {
  const uint8_t mac[6] = {0, 0, 0, 0x01, 0x0A, 0x00};
  TEST_ASSERT_EQUAL_STRING("homeguard-010a00", deviceIdFromMac(mac).c_str());
}
```
(Each test file has `int main()` with `UNITY_BEGIN()`, one `RUN_TEST` per test, `return UNITY_END();`.)

- [ ] **Step 4: Run it to verify it fails**

Run: `pio test -e native -f test_device_id`
Expected: FAIL (compile error: `deviceIdFromMac` not declared).

- [ ] **Step 5: Implement `device_id.{h,cpp}` and `hub_config.h`**

- [ ] **Step 6: Run tests and the board build**

Run: `pio test -e native -f test_device_id && pio run -e esp32dev`
Expected: 3 tests PASS. The esp32dev build succeeds (first run downloads the toolchain).

- [ ] **Step 7: Commit**

```bash
git add platformio.ini .gitignore include/secrets.example.h lib src test
git commit -m "feat(hub): scaffold platformio project and device id"
```

---

### Task 2: Stored code and command gate

**Files:**
- Create: `lib/hub_core/src/stored_code.{h,cpp}`, `lib/hub_core/src/command_gate.{h,cpp}`
- Test: `test/test_stored_code/test_main.cpp`, `test/test_command_gate/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class CodeKind : uint8_t { AcState = 0, Toggle = 1, Raw = 2 };
  const char* codeKindName(CodeKind);            // "ac-state" | "toggle" | "raw"
  struct StoredCode {
    bool present = false;
    CodeKind kind = CodeKind::Raw;
    int16_t protocol = -1;           // IRremoteESP8266 decode_type_t; -1 = UNKNOWN
    std::string protocolName;        // e.g. "DAIKIN", "UNKNOWN"
    uint16_t bits = 0;
    uint64_t value = 0;              // Toggle codes
    std::vector<uint8_t> state;      // AcState codes
    std::vector<uint16_t> raw;       // Raw codes (µs timings)
    int64_t learnedAt = 0;
  };
  std::vector<uint8_t> serializeCode(const StoredCode&);   // first byte = format version 1
  bool deserializeCode(const std::vector<uint8_t>&, StoredCode& out);

  struct Command { std::string value; int64_t requestedAt = 0; };
  enum class CommandStatus { Sent, NoCode, Unsupported, Stale, Ignore };
  struct CommandDecision { CommandStatus status; uint8_t sendCount; };  // sendCount 0 unless Sent
  bool isFresh(int64_t requestedAt, int64_t nowMs);   // |nowMs - requestedAt| <= kCommandFreshnessMs
  CommandDecision decideCommand(const Command&, int64_t lastHandledRequestedAt, int64_t nowMs, const StoredCode&);
  ```
- Decision order (first match wins): `requestedAt <= lastHandledRequestedAt` → Ignore; not fresh → Stale; `value != "OFF"` → Unsupported; `!code.present` → NoCode; else Sent with `sendCount` = 3 for AcState, 1 otherwise.

- [ ] **Step 1: Write the failing tests**

`test_stored_code`:
- `test_round_trip_ac_state`: AcState code, protocol 16, name "DAIKIN", bits 280, 35 state bytes, learnedAt 1789262500000 → serialize → deserialize: every field equal, `present == true`.
- `test_round_trip_raw`: Raw code with 300 timings → equal after round trip.
- `test_deserialize_rejects_garbage`: `{}`, `{0x07}` (unknown version), and a valid blob truncated by one byte all return `false`, and `out.present` stays `false`.
- `test_kind_names`: `"ac-state"`, `"toggle"`, `"raw"`.

`test_command_gate` (now = 1789262500000; `acCode`, `toggleCode` present; `noCode` absent):
- `test_new_fresh_off_with_ac_state_sends_three`: `{"OFF", now-1000}`, last 0 → Sent, 3.
- `test_toggle_sends_once`, `test_raw_sends_once` → Sent, 1.
- `test_same_request_is_ignored`: requestedAt == last → Ignore.
- `test_older_request_is_ignored`: requestedAt < last → Ignore.
- `test_old_command_is_stale`: now − 120001 → Stale.
- `test_future_within_window_is_fresh`: now + 120000 → Sent.
- `test_future_beyond_window_is_stale`: now + 120001 → Stale.
- `test_on_is_unsupported`: `{"ON", now}` → Unsupported.
- `test_off_without_code_is_no_code` → NoCode.
- `test_stale_wins_over_no_code`: old command, no code → Stale.

- [ ] **Step 2: Run them to verify they fail**

Run: `pio test -e native -f test_stored_code -f test_command_gate`
Expected: FAIL (types not declared).

- [ ] **Step 3: Implement `stored_code` and `command_gate`**

Serialization is a simple little-endian layout: version byte, then each field in struct order, with each vector/string preceded by a `uint16_t` length. `deserializeCode` checks the version and that every length fits the remaining bytes.

- [ ] **Step 4: Run tests to verify they pass**

Run: `pio test -e native -f test_stored_code -f test_command_gate`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/hub_core test/test_stored_code test/test_command_gate
git commit -m "feat(hub): add stored code format and command safety gate"
```

---

### Task 3: Commands-stream event parsing

**Files:**
- Create: `lib/hub_core/src/stream_events.{h,cpp}`
- Test: `test/test_stream_events/test_main.cpp`

**Interfaces:**
- Consumes: `Command` (Task 2).
- Produces:
  ```cpp
  struct StreamUpdate {
    std::optional<Command> latest;
    std::optional<int64_t> learnRequestAt;   // used by the Teach remote plan
  };
  StreamUpdate parseCommandsEvent(const std::string& dataPath, const std::string& data);
  ```
  `dataPath` and `data` are exactly what the Firebase stream reports for the stream on `devices/{id}/commands` (path relative to that node, JSON text).

- [ ] **Step 1: Write the failing tests**

- `test_root_event_with_both_children`: path `/`, data `{"latest":{"value":"OFF","requestedAt":1789262500000,"source":"app"},"result":{"status":"sent"},"learnRequest":{"requestedAt":1789262400000}}` → `latest == {"OFF", 1789262500000}`, `learnRequestAt == 1789262400000`.
- `test_latest_event`: path `/latest`, data `{"value":"OFF","requestedAt":1789262500000,"requestedBy":"u1","source":"notification"}` → latest set, no learnRequestAt.
- `test_learn_request_event`: path `/learnRequest`, data `{"requestedAt":1789262600000}` → only `learnRequestAt`.
- `test_result_echo_is_ignored`: path `/result`, any data → both empty.
- `test_partial_path_is_ignored`: path `/latest/requestedAt`, data `1789262500000` → both empty.
- `test_null_data_is_ignored`: path `/`, data `null` → both empty.
- `test_missing_fields_are_ignored`: path `/latest`, data `{"value":"OFF"}` → empty. Also `{"requestedAt":"soon","value":"OFF"}` → empty.

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_stream_events`
Expected: FAIL.

- [ ] **Step 3: Implement with ArduinoJson v7 (`JsonDocument`, `deserializeJson`)**

`requestedAt` must be an integer (`is<int64_t>()`). `value` must be a string.

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native -f test_stream_events`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/hub_core test/test_stream_events
git commit -m "feat(hub): parse commands stream events"
```

---

### Task 4: Firebase payloads

**Files:**
- Create: `lib/hub_core/src/payloads.{h,cpp}`
- Test: `test/test_payloads/test_main.cpp`

**Interfaces:**
- Consumes: `StoredCode`, `CodeKind`, `CommandStatus` (Task 2).
- Produces:
  ```cpp
  bool isPlausibleTemperature(float c);                  // finite and -10.0 <= c <= 60.0
  std::string telemetryJson(float c, int64_t nowMs);     // {"temperatureCelsius":22.44,"lastSeenAt":nowMs}
  std::string readingJson(float c, int64_t nowMs);       // {"timestamp":nowMs,"temperatureCelsius":22.44}
  std::string remoteJson(const StoredCode&);             // see tests
  const char* commandStatusName(CommandStatus);          // "sent","no-code","unsupported","stale"
  std::string commandResultJson(int64_t requestedAt, CommandStatus, int64_t handledAt);
  ```

- [ ] **Step 1: Write the failing tests** (parse the output with ArduinoJson and assert fields, not exact strings)

- `test_telemetry_rounds_to_two_decimals`: 22.4449f → `temperatureCelsius` 22.44 (within 0.001), `lastSeenAt` exact, and **no** `powerState` key.
- `test_reading_fields`: `timestamp`, `temperatureCelsius`.
- `test_remote_not_learned`: `StoredCode{}` → `{"learned":false}` and nothing else.
- `test_remote_learned`: AcState, "DAIKIN", learnedAt → `learned:true`, `kind:"ac-state"`, `protocol:"DAIKIN"`, `learnedAt`.
- `test_command_result`: (1789262500000, Sent, 1789262501200) → `requestedAt`, `status:"sent"`, `handledAt`. Also NoCode → `"no-code"`, Stale → `"stale"`, Unsupported → `"unsupported"`.
- `test_plausible_temperature_bounds`: 22.4 true; −10.0 and 60.0 true; NaN, −40, 85, +∞ false.

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_payloads`
Expected: FAIL.

- [ ] **Step 3: Implement `payloads.{h,cpp}` with ArduinoJson**

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native -f test_payloads`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/hub_core test/test_payloads
git commit -m "feat(hub): build firebase payloads per contract"
```

---

### Task 5: Status light pattern and LED adapter

**Files:**
- Create: `lib/hub_core/src/status_pattern.{h,cpp}`, `src/status_led.{h,cpp}`
- Test: `test/test_status_pattern/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class HubMode { AwaitingSetup, Connecting, Online, Learning, Resetting };
  bool ledOnAt(HubMode, uint32_t msSinceModeStart);
  // src/status_led.h
  class StatusLed { public: void begin(); void setMode(HubMode, uint32_t nowMs); void update(uint32_t nowMs); };
  ```
- Patterns (from the spec's table): AwaitingSetup = slow blink 1000 ms on / 1000 ms off. Connecting = double blink in a 1500 ms cycle, on for [0,150) and [300,450). Online = solid. Learning = fast blink 100/100. Resetting = 50/50.

- [ ] **Step 1: Write the failing tests**

- `test_awaiting_setup_slow_blink`: on at 0 and 999, off at 1000 and 1999, on at 2000.
- `test_connecting_double_blink`: on at 0, 149, 300, 449; off at 150, 299, 450, 1499; on at 1500.
- `test_online_solid`: on at 0, 12345.
- `test_learning_fast_blink`: on at 0, off at 100, on at 200.
- `test_resetting_faster_blink`: on at 0, off at 50.

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_status_pattern`
Expected: FAIL.

- [ ] **Step 3: Implement `ledOnAt` and `StatusLed`**

`StatusLed` writes GPIO2 only when the computed state changes. `setMode` resets the mode start time only when the mode actually changes.

- [ ] **Step 4: Run tests and board build**

Run: `pio test -e native -f test_status_pattern && pio run -e esp32dev`
Expected: PASS. Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add lib/hub_core src/status_led.* test/test_status_pattern
git commit -m "feat(hub): add status light patterns"
```

---

### Task 6: Clock, sensor and code store adapters

**Files:**
- Create: `src/hub_clock.{h,cpp}`, `src/temp_sensor.{h,cpp}`, `src/code_store.{h,cpp}`

**Interfaces:**
- Consumes: `StoredCode`, `serializeCode`, `deserializeCode` (Task 2).
- Produces:
  ```cpp
  // hub_clock.h
  void clockBegin();            // configTime(0, 0, "pool.ntp.org", "time.google.com")
  bool clockSynced();           // epoch seconds > 1704067200 (2024-01-01)
  int64_t nowMs();              // gettimeofday → epoch ms
  // temp_sensor.h
  bool sensorBegin();           // Wire.begin(21, 22); Adafruit_AHTX0.begin()
  bool sensorRead(float& celsius);   // false on any read failure
  // code_store.h  (Preferences namespace "hg")
  void storeBegin();
  StoredCode loadCode();        // returns StoredCode{} if missing or deserialize fails
  bool saveCode(const StoredCode&);  // key "code" (blob)
  int64_t loadLastCommandAt();  bool saveLastCommandAt(int64_t);   // key "lastCmdAt"
  int64_t loadLastLearnAt();    bool saveLastLearnAt(int64_t);     // key "lastLearnAt"
  void clearAll();              // removes all keys in "hg"
  ```

- [ ] **Step 1: Implement the three adapters**

No unit tests. These are thin hardware wrappers, and their logic lives in `hub_core`.

- [ ] **Step 2: Build for the board**

Run: `pio run -e esp32dev`
Expected: build succeeds with no warnings from these files.

- [ ] **Step 3: Commit**

```bash
git add src/hub_clock.* src/temp_sensor.* src/code_store.*
git commit -m "feat(hub): add clock, sensor and flash storage adapters"
```

---

### Task 7: IR sender adapter

**Files:**
- Create: `src/ir_sender.{h,cpp}`

**Interfaces:**
- Consumes: `StoredCode`, `CodeKind` (Task 2), `kPinIrSend`, `kRepeatGapMs` (Task 1).
- Produces: `void irSenderBegin();` and `void irSend(const StoredCode& code, uint8_t times);`

- [ ] **Step 1: Implement**

Use `IRsend irsend(kPinIrSend)` and call `irsend.begin()` in `irSenderBegin()`. Per send:
- AcState: `irsend.send((decode_type_t)code.protocol, code.state.data(), code.state.size())`.
- Toggle: `irsend.send((decode_type_t)code.protocol, code.value, code.bits)`.
- Raw: `irsend.sendRaw(code.raw.data(), code.raw.size(), 38)`.

Wait `kRepeatGapMs` between sends, but not after the last one. Do nothing if `!code.present`.

- [ ] **Step 2: Build for the board**

Run: `pio run -e esp32dev`
Expected: success.

- [ ] **Step 3: Commit**

```bash
git add src/ir_sender.*
git commit -m "feat(hub): add ir sender for stored codes"
```

---

### Task 8: Cloud adapter (FirebaseClient)

**Files:**
- Create: `src/cloud.{h,cpp}`

**Interfaces:**
- Consumes: `parseCommandsEvent`, `StreamUpdate` (Task 3).
- Produces:
  ```cpp
  struct CloudCallbacks {
    std::function<void(const Command&)> onCommand;
    std::function<void(int64_t requestedAt)> onLearnRequest;   // Teach remote plan uses this
  };
  void cloudBegin(const std::string& deviceId, CloudCallbacks);
  void cloudLoop();                       // call every loop()
  bool cloudReady();                      // signed in
  void cloudStartStream();                // idempotent; caller only calls once clockSynced()
  void cloudSet(const std::string& relPath, const std::string& json);   // devices/{id}/<relPath>
  void cloudPush(const std::string& relPath, const std::string& json);
  ```

- [ ] **Step 1: Implement with FirebaseClient**

- `#define ENABLE_USER_AUTH` and `#define ENABLE_DATABASE` before `#include <FirebaseClient.h>`.
- Auth: anonymous sign-up with `UserAccount user(FIREBASE_API_KEY)` and `signup(...)`, as in the library's `examples/App/UserManagement/Anonymous`. It signs up fresh on every boot. Keeping the same anonymous identity across reboots is a known gap.
- Two `AsyncClientClass`, each with its own `NetworkClientSecure`: one for writes, one for the single SSE stream on `/devices/{id}/commands` (`Database.get(streamClient, path, cb, true)`). Use `setInsecure()` on both. This is a known gap, already recorded in the spec.
- Writes are async (`Database.set<object_t>` / `Database.push<object_t>` with a callback that only logs errors). They never block `loop()`.
- In the stream callback, read `RealtimeDatabaseResult` (`event()`, `dataPath()`, `to<String>()`). For `put`/`patch` events, call `parseCommandsEvent`, then call `onCommand` / `onLearnRequest` for whichever field is set.

- [ ] **Step 2: Build for the board**

Run: `pio run -e esp32dev`
Expected: success. Note the RAM/flash usage line in the commit body.

- [ ] **Step 3: Commit**

```bash
git add src/cloud.*
git commit -m "feat(hub): add firebase cloud adapter with single commands stream"
```

---

### Task 9: Wire it together, with the board checklist

**Files:**
- Modify: `src/main.cpp`
- Create: `docs/board-checklist.md` (shared by later plans)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Implement `main.cpp`**

- `setup()`: Serial 115200, `led.begin()` (a global `StatusLed led;`, mode Connecting), `irSenderBegin()`, `storeBegin()`, `code = loadCode()`, `lastCmdAt = loadLastCommandAt()`, `sensorBegin()`. Read the STA MAC with `esp_read_mac(mac, ESP_MAC_WIFI_STA)`, then `deviceId = deviceIdFromMac(mac)`. Then `WiFi.begin(WIFI_SSID, WIFI_PASSWORD)` with `WiFi.setAutoReconnect(true)`, and `clockBegin()`.
- `loop()`:
  - The first time `WiFi.isConnected()`: `cloudBegin(deviceId, {onCommand, onLearnRequest})`. Once begun, call `cloudLoop()` every loop.
  - `led.update(millis())`.
  - The mode is Online when `WiFi.isConnected() && cloudReady()`, otherwise Connecting.
  - The first time the hub is ready and the clock is synced: `cloudStartStream()` and `cloudSet("remote", remoteJson(code))`.
  - Every `kTelemetryIntervalMs` (first run right after the first ready+synced): if `sensorRead(t)` and `isPlausibleTemperature(t)`, then `cloudSet("telemetry", telemetryJson(t, nowMs()))` and `cloudPush("temperatureReadings", readingJson(t, nowMs()))`. Otherwise, log and write nothing.
- `onCommand(cmd)`: `d = decideCommand(cmd, lastCmdAt, nowMs(), code)`. Return on Ignore. Otherwise, persist first: `saveLastCommandAt(cmd.requestedAt)` and update `lastCmdAt`. If Sent, `irSend(code, d.sendCount)`. Then `cloudSet("commands/result", commandResultJson(cmd.requestedAt, d.status, nowMs()))`.
- `onLearnRequest`: log only (the Teach remote plan fills it in).

- [ ] **Step 2: Run all host tests and the board build**

Run: `pio test -e native && pio run -e esp32dev`
Expected: every test PASSES, and the build succeeds.

- [ ] **Step 3: Write `docs/board-checklist.md` → section "Hub basics"**

Follow the PDF's test order:
1. Flash and open the serial monitor: "HomeGuard hub" prints and the LED double-blinks.
2. Serial shows a plausible AHT20 temperature, and the reading matches a separate thermometer within ±0.5 °C. Test this with the sensor mounted away from the LEDs and the ESP32.
3. In the Firebase console, `devices/<id>/telemetry` updates every ~60 s, `temperatureReadings` grows by one per minute, and `remote` shows `{"learned":false}`.
4. In the console, set `commands/latest` to `{"value":"OFF","requestedAt":{".sv":"timestamp"},"source":"app"}`: `commands/result.status` becomes `"no-code"` within a few seconds.
5. Reboot the hub: no new `commands/result` is written (the old command is ignored).
6. Unplug the router for 2 minutes, then plug it back in: the LED double-blinks and then turns solid, and telemetry resumes.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp docs/board-checklist.md
git commit -m "feat(hub): wire telemetry and off commands end to end"
```
