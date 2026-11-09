# HomeGuard AC Hub: design

Date: 2026-11-06 · Owner: Daniel (firmware + app) · Hardware: Rishabh Modi

## What this is and why

HomeGuard is a startup idea: a small hub on the wall that tells you when you've left home with the AC running, and lets you turn it off from your phone.

The phone app (`~/repos/ac-guard`, Expo/React Native, iOS-first) already does the deciding. It watches a home geofence, waits the user's chosen delay, judges from room temperature whether the AC is probably on, and sends a notification with **Turn it off** and **Keep it on**. The hub's job is narrow: report the room temperature, and when told, fire the AC remote's OFF signal over infrared. The hub never turns the AC off on its own.

This project builds:

1. The hub firmware (this repo).
2. The app changes in ac-guard so a regular person can set the hub up and teach it their remote using only the app, with no laptop.

The product has to be something a customer could set up alone, so setup goes through the app, not through code edits.

## Sources of truth

- Hardware: Rishabh's artifact (revision 3) is the source of truth, with the PDF `HomeGuard_AC_Hub_Hardware_Spec.pdf` (30 Oct 2026) for the pin map and firmware notes. Where they agree, the firmware follows both. Conflicts are listed at the end.
- Hub ↔ app messages: [`docs/firebase-contract.md`](../../firebase-contract.md). Both sides build against it.
- Existing app behaviour: `ac-guard/docs/RUN.md`, `ac-guard/src/services/firebase.ts`, `ac-guard/src/services/notifications.ts`, `ac-guard/src/types/home-guard.ts`.

## Hardware the firmware drives

| Function | Pin | Notes |
|---|---|---|
| IR send | GPIO4 | Through 2N2222 to the IR LEDs. Never hold HIGH; only modulated bursts. LOW at boot via pull-down. |
| IR receive | GPIO14 | VS1838B, already demodulated, idle HIGH. |
| Temperature | GPIO21 SDA / GPIO22 SCL | AHT20 at 0x38. |
| Setup/reset button | GPIO0 | The DevKit's built-in BOOT button. No extra part. |
| Status light | GPIO2 | The DevKit's built-in LED. No extra part. |

## How it works

### 1. First-time setup (Bluetooth)

1. The user plugs in the hub. With no saved Wi-Fi, it advertises over Bluetooth as `HomeGuard-XXXXXX` (last 3 MAC bytes). Status light: slow blink.
2. In the app, the user taps **Add hub**. The app scans for `HomeGuard-` devices, connects, lists nearby Wi-Fi networks from the hub, and the user picks one and enters the password.
3. The app sends the credentials using Espressif's standard provisioning protocol (Arduino `WiFiProv`, BLE scheme, security level 1 with a proof-of-possession code).
4. The hub joins Wi-Fi. A wrong password is reported back to the app, which asks again. On success the hub turns Bluetooth off for good (frees memory for TLS), signs in to Firebase, and starts reporting.
5. The app stores the device ID (`homeguard-xxxxxx`, from the Bluetooth name) as `settings.acDeviceId`, notes the hub's current `telemetry.lastSeenAt` (or none), then waits up to 90 s, by the phone's own timer, for a larger `lastSeenAt` to appear before saying "Hub connected". Comparing against the hub's own earlier value avoids depending on the phone's clock, and a hub being set up again already has an old `lastSeenAt`.
6. The app goes straight to **Teach remote**.

On later boots the hub uses its saved Wi-Fi and skips Bluetooth.

### 2. Teach remote

1. The user taps **Teach remote** (in setup, and later in Settings → AC). The app first asks them to turn the AC on with its remote, then writes `commands/learnRequest`.
2. The hub sets `learn/status.state = listening` and listens on GPIO14 for 30 s. The app shows "Point your remote at the hub and press OFF." The AC will usually turn off too, which is expected. Status light: fast blink. Like commands, a `commands/learnRequest` is only acted on if it is new and under 2 minutes old, so a leftover request never starts listening after a reboot.
3. The hub takes the first frame that is long enough to be real. It saves it to flash (survives power loss), works out its `kind` (see contract), writes `remote`, and sets `state = saved`. Too short or noisy → `rejected`. Nothing in 30 s → `timeout`. The app offers **Try again** for both.
4. After `saved`, the app asks the user to turn the AC back on, then offers **Test it**. Test sends a normal OFF command and asks "Did your AC turn off?" No → back to step 1.

Teaching again replaces the old code.

### 3. Turning the AC off

1. The app writes `commands/latest` (existing behaviour, from the notification or dashboard).
2. The hub listens to that path with a live Firebase stream.
3. It acts only on a command that is **new** and **fresh**:
   - New: its `requestedAt` differs from the last one handled. The last handled value is saved in flash, so a reboot never re-fires an old command (the stream's first event is the current value).
   - Fresh: no more than 2 minutes old by the hub's NTP clock. Otherwise → `stale`.
4. Firing:
   - `kind = ac-state`: send 3 times, 300 ms apart (an AC state frame says "off", so repeats are safe).
   - `kind = toggle` or `raw`: send once (repeating a toggle could turn the AC back on). The app only lets these be sent when there is a strong sign the AC is on (see "Power-toggle remotes" below).
5. The hub writes `commands/result`. The app shows **Command sent**, never "AC off". Infrared can't confirm the AC received it.

### Power-toggle remotes (Daniel's decision)

Some remotes only have a single POWER button. Sending that code to an AC that is already off turns it **on**. These remotes are allowed, but only with a stronger signal that the AC is actually on:

- Applies when `remote.kind` is `toggle` or `raw` (raw is unknown, so it is treated as a possible toggle).
- **Built on the existing Smart check** (`ac-guard/src/services/ac-monitoring.ts`), not a fixed margin. The Smart check reaches `likely-on` when, after the delay, the room stays below the threshold and does not warm across a 15-minute window while outdoor air is warmer. Its weak spot: when outdoor air is only slightly warmer than the room (e.g. a Singapore night), an AC-off room warms too slowly to tell apart from "stable".
- **Extra condition for toggle codes: "would an AC-off room visibly have warmed by now?"** Using the room's learned warming time constant τ (the median of `thermalTimeConstantsMinutes`, via `learnedThermalTimeConstant`), the app predicts the rise an AC-off room would show over the same 15-minute trend window: `predictedRise = (outdoor − room) × (1 − e^(−15/τ))`. Turn it off is offered only if the state is `likely-on` **and** `predictedRise ≥ 0.5 °C`. The Smart check calls a change under 0.15 °C "stable", so 0.5 °C (about 3×) means an AC-off room could not pass as stable. In effect each room gets its own required outdoor/room gap: about 2.3 °C for τ = 60 min, about 4.3 °C for τ = 120 min.
- **No learned τ yet → no Turn it off for toggle codes** (Daniel's decision, for now). The user is told the AC might be on, without the button, and why: their remote uses a power toggle, and HomeGuard needs to learn how the room warms first. A research-based starting τ for Singapore homes may replace this later; see `docs/research/singapore-room-warming.md`.
- The app checks the rule twice: when it decides to send the notification, and again at the moment the user taps Turn it off, since conditions may have changed in between.
- This is enforced in the app. The hub doesn't know the outdoor temperature, and it fires whatever OFF command it receives.
- The real fix comes in phase 2, when the power-monitoring plug can tell whether the AC is drawing power.

### Learning the room's warming rate as it's used *(Claude's proposal)*

Today τ is only learned when the user runs Calibration by hand. Two automatic sources are added, both feeding the existing `observeCalibration` logic (same quality checks, same 8-value history):

1. **After every shut-off.** When `commands/result.status` is `sent`, the app starts a calibration-style observation from that moment, using the room and outdoor temperatures at that time. If the room then warms the way an AC-off room should, the τ estimate is saved. The app can also say "The room is warming, so the AC is likely off now", which is the closest thing to confirmation infrared allows.
   The Teach remote **Test** step sends a normal OFF, so its `sent` result starts one of these too. No separate Teach remote source is needed.
2. **Toggle and raw remotes** can't send OFF until τ is learned (at least 2 events, per `learnedThermalTimeConstant`), so the Test step gives them at most one. After saving such a code, Teach remote and the dashboard's blocked state explain that Turn it off unlocks once HomeGuard has learned how the room warms, and link to the existing Calibration flow.

Either source silently gives up, as manual calibration already does, if the room doesn't warm cleanly (for example, the user turned the AC back on).

### 4. Reporting temperature

Every 60 s: read the AHT20, overwrite `telemetry`, push one `temperatureReadings` entry. A failed sensor read skips that cycle; it never writes a fake value. The hub writes nothing about whether the AC is on.

### 5. Status light (GPIO2)

| Pattern | Meaning |
|---|---|
| Slow blink | Waiting for setup over Bluetooth |
| Double blink | Connecting to Wi-Fi/Firebase |
| Solid | Online |
| Fast blink | Listening to learn the remote |
| Off | No power |

### 6. Factory reset

Hold BOOT for 10 s while running: the status light flashes, the hub wipes Wi-Fi, the learned code, and its saved command state, then restarts into Bluetooth setup. Holding BOOT while plugging in stays the ESP32's normal firmware-upload mode, so the 10 s hold only counts after boot.

### 7. Losing the connection

Wi-Fi or Firebase drops → the hub keeps retrying with backoff and keeps reading the sensor, and the light shows "connecting". Missed readings are not backfilled. Commands that arrive while it's offline are caught by the 2-minute freshness rule.

## App changes (ac-guard)

Built by a subagent on its own branch in `~/repos/ac-guard`, following the contract.

1. **Add hub flow.** A new onboarding step and an entry in Settings → AC: scan, pick hub, choose Wi-Fi, enter password, send, wait for online, then Teach remote. It needs a Bluetooth provisioning library compatible with Expo dev builds (Espressif's iOS/Android provisioning SDKs, via an existing React Native wrapper if one works with Expo SDK 55; the builder verifies). Adds the iOS Bluetooth permission string.
2. **Teach remote screen.** Writes `commands/learnRequest`, follows `learn/status`, has Try again, and the "Test it now?" step.
3. **Honest command feedback.** Reads `commands/result` and shows the table's messages. The dashboard stops implying the AC is off because a command was sent.
4. **Power-toggle guard.** Implements the "Power-toggle remotes" rule above in the AC notification, the notification action handler, and the dashboard button.
5. **"Remote not taught" flag.** When `remote.learned` is `false`, the dashboard and the AC notification point the user to Teach remote.
6. **Real online/offline status.** The app currently shows the hub as online whenever the node exists, because nothing writes `connectionStatus`. It should instead treat the hub as offline when `telemetry.lastSeenAt` is more than 3 minutes old.
7. **Read only what's needed.** The app currently reads all of `devices/{id}`, including `temperatureReadings`. Once the hub adds about 1,440 readings a day, that would re-download the whole history on every update. The app subscribes to the specific children instead.
8. **Demo mode keeps working.** Every new screen has a mock path, like the existing mock Firebase, so the app still demos without hardware.

## Goals

- A new user goes from unboxing to a working **Turn it off** using only the app.
- The hub reports temperature in the format the app already reads, with no change to the app's AC-detection logic.
- Turn it off reaches the AC within a few seconds when the hub is online.
- The user is never told "AC off" when we only know "command sent".

## Non-goals (for now)

- Per-customer Firebase security (per-owner rules, per-device secrets). The hub signs in anonymously and uses the existing prototype rules.
- A unique per-device proof-of-possession code printed on the hub. Prototype uses one fixed code compiled into firmware and app.
- Over-the-air firmware updates.
- More than one hub per account (the app has a single `acDeviceId`).
- Turning the AC **on**, or any setting besides off.
- Pruning old `temperatureReadings` (grows ~1,440/day; fine for a prototype).
- Humidity reporting, the stove sensor, and the phase 2 power plug.

## Anti-goals (would be failure even if it "works")

- The hub turns the AC off without the user tapping Turn it off.
- An old OFF command fires again after a reboot or a reconnect.
- A toggle-style remote code gets repeated and leaves the AC on.
- A toggle-style code is sent without a strong sign the AC is on, and turns an off AC on.
- The app says the AC is off when the hub only sent a signal.
- A reading is invented when the sensor fails.

## Status

The hardware is not built yet (as of 2026-11-07). The firmware is written from the PDF and the artifact. Board-level checks wait until the board exists.

## Prerequisites outside the code

- A Firebase project with Anonymous Auth and Realtime Database enabled, and its config in `ac-guard/.env.local` and the hub firmware. None exists in the repo yet; Daniel creates it.
- Check the demo AC responds to infrared (cover the remote's emitter with a thumb; if it still works, it isn't IR) and note whether its remote has a separate OFF or a single POWER toggle.

## Decided by Claude (Daniel can override)

- Firebase Realtime Database as the relay, not MQTT: the app already uses it.
- The hub reports every 60 s instead of answering on request: the app's Smart delay needs the history.
- Bluetooth setup (Espressif `WiFiProv`) rather than a hub Wi-Fi hotspot: avoids sending iPhone users to Settings mid-setup.
- Device ID from the MAC (`homeguard-xxxxxx`).
- Prototype proof-of-possession for Bluetooth setup: `homeguard-pop-v1`, shared by firmware and app.
- The hub listens on one Firebase stream (`devices/{id}/commands`) to save ESP32 memory, which is why the learn request lives at `commands/learnRequest`.
- TLS certificate checking is off on the hub for the prototype (known gap, fix before customers).
- 30 s learn window, 2-minute command freshness, 3×300 ms repeats only for `ac-state` codes.
- BOOT button for factory reset, onboard LED for status.
- Add hub only has to work on iOS (the app is iOS-first). Android must still build.
- If no React Native wrapper for Espressif's provisioning works with Expo SDK 55 and React Native 0.83 (new architecture only), the fallback is a small Expo native module around Espressif's iOS provisioning SDK, added through a config plugin since `ios/` is generated. Not a Wi-Fi hotspot, and not hardcoded Wi-Fi.
- Teach remote asks the user to turn the AC on first, and to turn it back on before Test, because pressing OFF while teaching also turns the AC off.
- "Teach remote" instead of "Calibrate": the app already has a thermal **calibration** feature.
- Toolchain: PlatformIO with the Arduino framework (`FirebaseClient` by mobizt, `IRremoteESP8266`, `Adafruit AHTX0`, built-in `WiFiProv`). A larger app partition will likely be needed for Bluetooth + Wi-Fi + TLS.

## Left to the builder

- Firmware file layout, task/loop structure, retry timings, how the anonymous Firebase identity is kept across reboots.
- Exactly how `kind` is decided from what IRremoteESP8266 reports.
- App screen layout and copy beyond the messages named above, within ac-guard's existing design system.
- Which tests run on the host versus on the board.

## Build order (each gets its own small plan)

1. Hub basics: sensor, Wi-Fi from a temporary local config, Firebase telemetry, command stream, firing a stored code.
2. Hub Teach remote.
3. Hub Bluetooth setup and factory reset (removes the temporary config).
4. App changes (can run alongside 1–3 once this contract is fixed).

### Testing without the hardware

The hub isn't built yet, so the code is written from the PDF and the artifact and checked without a board:

- Logic that doesn't touch hardware runs as tests on the laptop (PlatformIO's `native` environment). That covers which commands to act on (new, fresh, not already handled), firing rules by `kind`, the learn state machine, the button-hold timing, and the JSON written to Firebase.
- Hardware access (sensor, IR, Wi-Fi, Bluetooth, Firebase) sits behind thin wrappers, so the logic can be tested with fakes.
- Every step must compile for the real board (`esp32dev`), including the partition size.
- The app side is checked in demo mode and against a real Firebase project once one exists.

Done for now: the firmware builds for the ESP32, the laptop tests pass, and the app flows work in demo mode. Done for real, once the board exists: the whole flow works end to end. That means fresh hub → Add hub → Teach remote → leave the geofence → notification → Turn it off → AC turns off, with the app saying "Command sent". Each plan ends with an on-board checklist for that later step, following the PDF's suggested test order.

## Hardware conflicts for Rishabh (firmware works either way)

1. LED wiring: PDF has 3 LEDs in parallel, each with a 47 Ω resistor. The artifact has 3–4 in series with one 33–100 Ω resistor. Four 940 nm LEDs in series won't light from 5 V; three barely will. The PDF's parallel version works.
2. Transistor base resistor: 220 Ω (PDF) vs 470 Ω (artifact).
3. The 10 kΩ pull-down that keeps the LEDs off during boot is in the PDF but missing from the artifact's parts list.
4. Power (in neither source): three parallel IR LEDs at 47 Ω pull roughly 225 mA in bursts, on top of the ESP32's ~350 mA Wi-Fi peaks. On a thin USB cable or a weak charger, that can sag the 5 V rail enough to trip the ESP32's brownout reset mid-send. Add a 470–1000 µF electrolytic plus a 100 nF ceramic across 5 V and GND, next to the 2N2222, and use a 5 V supply rated for at least 1 A. The firmware survives a brownout (it never sends twice and reports `interrupted`), but it can't stop one.
