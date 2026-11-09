# App Hub Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user set up the HomeGuard hub and teach it their AC remote from the ac-guard app alone. Turn it off must report honestly ("Command sent"), refuse power-toggle codes without a strong sign the AC is on, and learn the room's warming rate automatically. Every flow keeps working in demo mode.

**Architecture:**
- **Pure logic** lives in three dependency-free modules under `src/lib/`, tested with `node --test` the way the existing tests run. It covers hub data parsing, building the AC snapshot from Firebase children, online/offline, the setup wait, learn phases, result messages and the toggle guard.
- **Firebase:** `src/services/firebase.ts` stops reading `devices/{deviceId}` whole. It listens only to the contract children, and gains a mock hub.
- **AC monitoring:** `src/services/ac-monitoring.ts` gains the notification guard and automatic calibration after a shut-off.
- **Turn it off:** a new `src/services/turn-off.ts` re-checks the guard every time the user taps it.
- **Bluetooth setup** sits behind one `HubProvisioner` interface. One implementation wraps `@orbital-systems/react-native-esp-idf-provisioning`; the other is a mock.
- **Screens:** shared `TeachRemoteFlow` and `AddHubFlow` components, each mounted by a thin onboarding route and a thin settings route.

**Tech Stack:** Expo SDK 55, React Native 0.83 (new architecture only), Expo Router (typed routes), NativeWind 4, Firebase JS SDK v12 Realtime Database, `expo-notifications`, `@orbital-systems/react-native-esp-idf-provisioning@0.7.0` (wraps Espressif `ESPProvision ~> 3.2` on iOS). Tests use Node 24 `node:test` with `typescript.transpileModule`.

**Spec:** `~/repos/home-guard-hardware/docs/superpowers/specs/2026-11-06-homeguard-hub-design.md`. The relevant sections are "App changes (ac-guard)", "1. First-time setup", "2. Teach remote", "Power-toggle remotes", "Learning the room's warming rate as it's used" and "Testing without the hardware". **Contract:** `~/repos/home-guard-hardware/docs/firebase-contract.md`. Read both before starting.

## Global Constraints

- Work in `~/repos/ac-guard` on a new branch `feat/hub-integration` cut from `main`. Commit after every task with a conventional commit message. **No `Co-Authored-By` trailer, no "Generated with" line.**
- `ac-guard/AGENTS.md` asks you to read the Expo v55 docs (https://docs.expo.dev/versions/v55.0.0/) before writing code. Use Context7 for current docs on Expo, expo-notifications, expo-router and Firebase v12.
- **Firebase paths**, exactly as in the contract:
  - `devices/{deviceId}/telemetry`
  - `remote`
  - `commands/latest`
  - `commands/result`
  - `commands/learnRequest`: the app writes `{ requestedAt: serverTimestamp(), requestedBy: uid }`
  - `learn/status`

  The app never writes `remote`, `commands/result` or `learn/status`.
- **AC reads:** the AC device is never read as the whole `devices/{deviceId}` node. The app reads only the six child paths above. Room-temperature history keeps using the existing indexed `temperatureReadings` query.
- **Hub identity:** Bluetooth name `HomeGuard-XXXXXX` (6 uppercase hex digits). deviceId is the name lowercased, e.g. `homeguard-a1b2c3`.
- **Provisioning:** proof-of-possession `homeguard-pop-v1`, BLE transport, security 1 (`ESPSecurity.secure`).
- **Hub offline** when `telemetry.lastSeenAt` is more than 3 minutes (180 000 ms) old.
- **"Hub connected":** a later snapshot shows a `telemetry.lastSeenAt` larger than the value read when setup started (`null` if none). This never compares the hub's clock with the phone's. The timeout is 90 s (90 000 ms) on the phone's own timer.
- **Toggle guard:**
  - It applies when `remote.kind` is `toggle` or `raw`.
  - Turn it off is allowed only if the status is `likely-on` AND `(outdoor − room) × (1 − e^(−15/τ)) ≥ 0.5 °C`, with τ from `learnedThermalTimeConstant` (2 or more events).
  - With no learned τ, there is no Turn it off for those codes, and the app explains why.
  - The guard is checked when deciding to notify and again on every Turn it off tap, from the dashboard and from the notification action.
- **Automatic calibration** has one source: a `commands/result` with status `sent`. The Teach remote Test step produces one. It reuses `observeCalibration` and requires outdoor air at least 1 °C warmer than the room.
- **Copy:**
  - Say "Command sent", never "AC off".
  - Result messages, exactly:
    - `sent`: "Command sent"
    - `failed`: "Hub couldn't send the OFF signal. Teach your remote again"
    - `no-code`: "Teach your remote first"
    - `unsupported`: "The hub can only turn the AC off"
    - `stale`: "Hub was offline; command not sent"
  - The feature is called **Teach remote** everywhere, never "calibrate".
  - Listening copy: "Point your remote at the hub and press OFF. Keep the hub plugged in until it's saved."
- **Sensor failure:** a `telemetry` with `lastSeenAt` but no `temperatureCelsius` means the hub is online and its temperature sensor failed. Show the hub as online with "Temperature sensor not responding", and never treat the missing reading as a temperature.
- **Demo mode** (no Firebase env values, `getFirebaseMode() === 'mock'`) must run every new flow without hardware.
- **Testable modules:** files in `src/lib/` use `import type` only, apart from `src/lib/ac-device.ts` importing `@/lib/hub`. Tests copy the `load()` helper from `tests/stove-detection.test.cjs` lines 1–23 verbatim; it injects `require` fakes and a fixed `Date`.
- **Platforms and dependencies:** Add hub has to work on iOS; Android must still build. The only new dependency is the provisioning library. Don't touch unrelated code, labels or styling.
- **Checks:** `node --test tests/*.test.cjs`, `npm run typecheck` and `npm run lint`. After adding route files, run `npx expo start` once and stop it when Metro is ready. That regenerates `.expo/types/router.d.ts` with the new routes; do it before `npm run typecheck`.

## Review Focus

1. **A result from an earlier attempt shown as this attempt's result.** For example, after a `saved`, the user taps Try again and the old `learn/status` still reads `saved`. The app must wait for the status that answers the new request. Pinned in Task 1 (the "previous attempt" assertions for `getLearnPhase`).
2. **Malformed or unknown hub data**, such as an unknown `kind`, a `status` outside the contract, or string timestamps. An unknown kind must be treated as `raw`, so the guard applies; malformed results are ignored. Pinned in Task 1 (parser test) and Task 3 (`buildAcSnapshot` with missing children).
3. **A hub that goes silent.** When telemetry stops, no Firebase event arrives, so offline must come from the clock, not from a new snapshot. The boundary is pinned in Task 1. In Task 8, the 15 s `now` tick recomputes it, and the walkthrough checks it.
4. **Setup problems:** wrong Wi-Fi password, a 5 GHz-only network, Bluetooth dropping mid-setup, or the phone offline when setup starts. Each must show a specific message. The user can retry because the hub stays in Bluetooth mode. Pinned in Task 1 (`classifyProvisionError`) and wired in Task 10.
5. **Turn it off tapped on an old notification after conditions changed**, for example back home or the room now warming. The toggle re-check must block it. Pinned in Task 8 (`requestTurnOff re-evaluates before deciding`).

## Not in scope

- The stove keeps its whole-node reads (`getStoveSnapshot`, `subscribeToStoveDevice`). Those nodes get no per-minute history from the hub, and they don't share the AC code.
- The onboarding reminder screen's "Demo device connected" card is unchanged.

---

### Task 1: Hub types and pure hub logic

**Files:**
- Modify: `src/types/home-guard.ts`
- Create: `src/lib/hub.ts`
- Test: `tests/hub.test.cjs`

**Interfaces:**
- Produces (types, `src/types/home-guard.ts`):
  ```ts
  export type RemoteKind = 'ac-state' | 'toggle' | 'raw';
  export type HubRemote = { learned: boolean; learnedAt: number | null; kind: RemoteKind | null; protocol: string | null };
  export type CommandResultStatus = 'sent' | 'failed' | 'no-code' | 'unsupported' | 'stale';
  export type CommandResult = { requestedAt: number; status: CommandResultStatus; handledAt: number };
  export type LearnState = 'listening' | 'saved' | 'timeout' | 'rejected' | 'interrupted';
  export type LearnStatus = { requestedAt: number; state: LearnState; updatedAt: number };
  export type HubCandidate = { name: string; deviceId: string };
  export type WifiNetwork = { ssid: string; rssi: number; secured: boolean };
  // AcDeviceSnapshot gains (AC only, not BaseDeviceSnapshot):
  //   remote: HubRemote | null; commandResult: CommandResult | null;
  //   learnStatus: LearnStatus | null; learnRequestedAt: number | null;
  // AcCalibration gains: source?: 'manual' | 'shut-off';
  // AcMonitoringState gains: lastCalibratedCommandAt: number | null;
  ```
- Produces (`src/lib/hub.ts`):
  ```ts
  export const HUB_NAME_PREFIX = 'HomeGuard-';
  export const HUB_PROOF_OF_POSSESSION = 'homeguard-pop-v1';
  export const HUB_OFFLINE_AFTER_MS = 180_000;
  export const HUB_SETUP_WAIT_MS = 90_000;
  export const LEARN_ACK_TIMEOUT_MS = 15_000;     // no matching learn/status yet
  export const LEARN_LISTEN_TIMEOUT_MS = 45_000;  // hub's 30 s window + 15 s margin
  export function deviceIdFromHubName(name: string): string | null;
  export function getHubConnectionStatus(lastSeenAt: number, now: number): ConnectionStatus;
  export type SetupWaitOutcome = 'waiting' | 'connected' | 'timed-out';
  export function getSetupWaitOutcome(input: { lastSeenAtBefore: number | null; lastSeenAt: number | null; waitStartedAt: number; now: number }): SetupWaitOutcome;
  export function parseHubRemote(value: unknown): HubRemote | null;
  export function parseCommandResult(value: unknown): CommandResult | null;
  export function parseLearnStatus(value: unknown): LearnStatus | null;
  export const COMMAND_RESULT_MESSAGES: Record<CommandResultStatus, string>;
  export function describeLastCommand(command: DeviceCommand | null, result: CommandResult | null): string;
  export type LearnPhase = 'waiting' | 'listening' | 'saved' | 'timeout' | 'rejected' | 'interrupted' | 'no-response';
  export const LEARN_PHASE_MESSAGES: Record<LearnPhase, string>;
  export function getLearnPhase(input: { requestBefore: number | null; currentRequest: number | null; status: LearnStatus | null; attemptStartedAt: number; now: number }): LearnPhase;
  export type ProvisionFailure = 'wrong-password' | 'network-not-found' | 'disconnected' | 'unknown';
  export const PROVISION_FAILURE_MESSAGES: Record<ProvisionFailure, string>;
  export function classifyProvisionError(error: unknown): ProvisionFailure;
  export function tidyWifiNetworks(networks: WifiNetwork[]): WifiNetwork[];
  ```

- [ ] **Step 1: Create the branch**

```bash
cd ~/repos/ac-guard && git checkout main && git pull --ff-only && git checkout -b feat/hub-integration
```

- [ ] **Step 2: Write the failing tests** in `tests/hub.test.cjs`, with `const hub = load('src/lib/hub.ts');`, `NOW = 1_800_000_000_000` and `MINUTE = 60_000`.

```js
test('deviceIdFromHubName lowercases valid hub names and rejects others', () => {
  assert.equal(hub.deviceIdFromHubName('HomeGuard-A1B2C3'), 'homeguard-a1b2c3');
  for (const name of ['HomeGuard-a1b2c3', 'HomeGuard-A1B2', 'HomeGuard-A1B2C3D', 'PROV_A1B2C3', ''])
    assert.equal(hub.deviceIdFromHubName(name), null);
});

test('hub is offline once lastSeenAt is more than 3 minutes old', () => {
  assert.equal(hub.getHubConnectionStatus(NOW - 3 * MINUTE, NOW), 'online');
  assert.equal(hub.getHubConnectionStatus(NOW - 3 * MINUTE - 1, NOW), 'offline');
  assert.equal(hub.getHubConnectionStatus(0, NOW), 'offline');
  assert.equal(hub.getHubConnectionStatus(NOW + 30_000, NOW), 'online'); // hub clock slightly ahead
});

test('setup is connected only when lastSeenAt moves past the value seen at setup start', () => {
  const start = NOW;
  const wait = (lastSeenAtBefore, lastSeenAt, now = start + 5_000, waitStartedAt = start) =>
    hub.getSetupWaitOutcome({ lastSeenAtBefore, lastSeenAt, waitStartedAt, now });
  assert.equal(wait(1_000, 1_000), 'waiting');    // re-setup hub, same old value
  assert.equal(wait(1_000, 500), 'waiting');      // smaller is not progress
  assert.equal(wait(1_000, 2_000), 'connected');  // hub clock unrelated to phone clock
  assert.equal(wait(null, 2_000), 'connected');   // brand-new hub
  assert.equal(wait(null, null), 'waiting');
  assert.equal(wait(null, 0), 'waiting');
  assert.equal(wait(null, null, start + 90_000), 'waiting');
  assert.equal(wait(null, null, start + 90_001), 'timed-out');
  assert.equal(wait(1_000, 2_000, start + 200_000), 'connected');
  assert.equal(wait(null, null, start + 120_000, start + 100_000), 'waiting'); // "Keep waiting"
});

test('hub data parsers ignore malformed values and treat unknown kinds as raw', () => {
  assert.deepEqual(hub.parseHubRemote({ learned: true, learnedAt: 5, kind: 'ac-state', protocol: 'DAIKIN' }), { learned: true, learnedAt: 5, kind: 'ac-state', protocol: 'DAIKIN' });
  assert.deepEqual(hub.parseHubRemote({ learned: true, kind: 'mystery' }), { learned: true, learnedAt: null, kind: 'raw', protocol: null });
  assert.deepEqual(hub.parseHubRemote({ learned: false, kind: 'toggle' }), { learned: false, learnedAt: null, kind: null, protocol: null });
  assert.equal(hub.parseHubRemote(null), null);
  assert.deepEqual(hub.parseCommandResult({ requestedAt: 1, status: 'sent', handledAt: 2 }), { requestedAt: 1, status: 'sent', handledAt: 2 });
  assert.equal(hub.parseCommandResult({ requestedAt: 1, status: 'done', handledAt: 2 }), null);
  assert.equal(hub.parseCommandResult({ requestedAt: '1', status: 'sent', handledAt: 2 }), null);
  assert.deepEqual(hub.parseLearnStatus({ requestedAt: 1, state: 'listening', updatedAt: 2 }), { requestedAt: 1, state: 'listening', updatedAt: 2 });
  assert.equal(hub.parseLearnStatus({ requestedAt: 1, state: 'learning', updatedAt: 2 }), null);
});

test('command results map to the contract messages', () => {
  assert.deepEqual({ ...hub.COMMAND_RESULT_MESSAGES }, { sent: 'Command sent', failed: "Hub couldn't send the OFF signal. Teach your remote again", 'no-code': 'Teach your remote first', unsupported: 'The hub can only turn the AC off', stale: 'Hub was offline; command not sent' });
  const command = { value: 'OFF', requestedAt: 100, source: 'app' };
  assert.equal(hub.describeLastCommand(null, null), 'No command');
  assert.equal(hub.describeLastCommand(command, null), 'Waiting for the hub');
  assert.equal(hub.describeLastCommand(command, { requestedAt: 99, status: 'sent', handledAt: 101 }), 'Waiting for the hub');
  assert.equal(hub.describeLastCommand(command, { requestedAt: 100, status: 'stale', handledAt: 101 }), 'Hub was offline; command not sent');
});

test('learn phase follows learn/status for the current request only', () => {
  const s = (requestedAt, state) => ({ requestedAt, state, updatedAt: requestedAt + 1 });
  const phase = (o) => hub.getLearnPhase({ requestBefore: 50, currentRequest: 100, status: null, attemptStartedAt: NOW, now: NOW + 1_000, ...o });
  assert.equal(phase({}), 'waiting');
  assert.equal(phase({ status: s(100, 'listening') }), 'listening');
  for (const state of ['saved', 'timeout', 'rejected', 'interrupted']) assert.equal(phase({ status: s(100, state), now: NOW + 120_000 }), state);
  assert.equal(phase({ currentRequest: 50, status: s(50, 'saved') }), 'waiting'); // previous attempt, new request not seen yet
  assert.equal(phase({ status: s(50, 'saved') }), 'waiting');                     // previous attempt's status
  assert.equal(phase({ now: NOW + 15_001 }), 'no-response');
  assert.equal(phase({ status: s(100, 'listening'), now: NOW + 45_000 }), 'listening');
  assert.equal(phase({ status: s(100, 'listening'), now: NOW + 45_001 }), 'no-response');
  assert.equal(hub.LEARN_PHASE_MESSAGES.listening, "Point your remote at the hub and press OFF. Keep the hub plugged in until it's saved.");
});

test('provisioning errors are classified for the Wi-Fi step', () => {
  assert.equal(hub.classifyProvisionError(new Error('Wi-Fi status: authentication error')), 'wrong-password'); // iOS ESPProvision
  assert.equal(hub.classifyProvisionError(new Error('AUTH_FAILED')), 'wrong-password');                        // Android
  assert.equal(hub.classifyProvisionError(new Error('Wi-Fi status: network not found')), 'network-not-found');
  assert.equal(hub.classifyProvisionError(new Error('NETWORK_NOT_FOUND')), 'network-not-found');
  assert.equal(hub.classifyProvisionError(Object.assign(new Error('Device is not connected.'), { code: 'E_DEVICE_DISCONNECTED' })), 'disconnected');
  assert.equal(hub.classifyProvisionError('boom'), 'unknown');
  assert.equal(hub.classifyProvisionError(null), 'unknown');
});

test('wifi list drops blank names, keeps the strongest duplicate, sorts by signal', () => {
  assert.deepEqual(hub.tidyWifiNetworks([
    { ssid: 'Home', rssi: -70, secured: true }, { ssid: 'Home', rssi: -50, secured: true },
    { ssid: '', rssi: -40, secured: false }, { ssid: 'Cafe', rssi: -60, secured: false },
  ]), [{ ssid: 'Home', rssi: -50, secured: true }, { ssid: 'Cafe', rssi: -60, secured: false }]);
});
```

- [ ] **Step 3: Run to verify failure**

Run: `node --test tests/hub.test.cjs`
Expected: FAIL (`ENOENT` on `src/lib/hub.ts`).

- [ ] **Step 4: Add the types and implement `src/lib/hub.ts`**

These rules are not fully fixed by the tests:
- `deviceIdFromHubName`: match `/^HomeGuard-[0-9A-F]{6}$/`, then lowercase.
- `getHubConnectionStatus`: online if and only if `lastSeenAt > 0 && now - lastSeenAt <= HUB_OFFLINE_AFTER_MS`.
- `getSetupWaitOutcome`:
  - Check `connected` first: `lastSeenAt !== null && lastSeenAt > 0 && (lastSeenAtBefore === null || lastSeenAt > lastSeenAtBefore)`.
  - Otherwise `timed-out` if `now - waitStartedAt > HUB_SETUP_WAIT_MS`, else `waiting`.
  - `now` and `waitStartedAt` are both phone-clock values.
- Parsers: every timestamp field must be a finite number. `learnedAt` may be missing, which gives `null`. For `parseHubRemote`, `learned !== true` returns the all-null shape.
- `getLearnPhase`:
  - A status counts only if `currentRequest !== null && currentRequest !== requestBefore && status.requestedAt === currentRequest`.
  - Final states win at any time.
  - A matched `listening` becomes `no-response` after `LEARN_LISTEN_TIMEOUT_MS`.
  - With no matched status, the phase is `waiting`, then `no-response` after `LEARN_ACK_TIMEOUT_MS`.
  - Both timeouts are measured from `attemptStartedAt`.
- `classifyProvisionError`: read `code` and `message` from an object error.
  - `/authentication error|AUTH_FAILED/i` → `wrong-password`
  - `/network not found|NETWORK_NOT_FOUND/i` → `network-not-found`
  - `code === 'E_DEVICE_DISCONNECTED'` or `/disconnected/i` → `disconnected`
- Copy for `LEARN_PHASE_MESSAGES`:
  - waiting: "Waiting for the hub…"
  - listening: as in the test above
  - saved: "Remote saved."
  - timeout: "We didn't hear your remote. Try again."
  - rejected: "The signal was too short or unclear. Try again."
  - interrupted: "The hub restarted before it saved your remote. Keep it plugged in, then try again."
  - no-response: "The hub didn't answer. Check it's plugged in and online, then try again."
- Copy for `PROVISION_FAILURE_MESSAGES`:
  - wrong-password: "That Wi-Fi password didn't work. Check it and try again."
  - network-not-found: "The hub couldn't find that network. It only works with 2.4 GHz Wi-Fi."
  - disconnected: "Lost the Bluetooth connection to the hub. Move closer and try again."
  - unknown: "Setup didn't finish. Try again."

Add the new snapshot and state fields to the types now. Until Task 3, `npm run typecheck` fails where literals lack them; that's expected. Only `node --test` gates this task.

- [ ] **Step 5: Run to verify pass**

Run: `node --test tests/*.test.cjs`
Expected: everything passes, including the existing 21 tests.

- [ ] **Step 6: Commit**

```bash
git add src/types/home-guard.ts src/lib/hub.ts tests/hub.test.cjs
git commit -m "feat(hub): add hub contract types and pure hub logic"
```

---

### Task 2: Power-toggle guard

**Files:**
- Create: `src/lib/turn-off-guard.ts`
- Test: `tests/turn-off-guard.test.cjs`

**Interfaces:**
- Consumes: `HubRemote`, `AcStatus` (types).
- Produces:
  ```ts
  export const TOGGLE_TREND_WINDOW_MINUTES = 15;
  export const TOGGLE_MIN_PREDICTED_RISE_CELSIUS = 0.5;
  export type TurnOffBlockedReason = 'not-likely-on' | 'no-learned-tau' | 'missing-readings' | 'gap-too-small';
  export type TurnOffAvailability =
    | { state: 'available' }
    | { state: 'teach-remote'; message: string }
    | { state: 'blocked'; reason: TurnOffBlockedReason; message: string };
  export type TurnOffInput = { remote: HubRemote | null; status: AcStatus; outdoorCelsius: number | null; roomCelsius: number; tauMinutes: number | null };
  export const TEACH_REMOTE_MESSAGE: string;
  export const TURN_OFF_BLOCKED_MESSAGES: Record<TurnOffBlockedReason, string>;
  export const TOGGLE_LEARNING_HINT: string;
  export function predictedOffRoomRise(outdoorCelsius: number, roomCelsius: number, tauMinutes: number): number;
  export function getTurnOffAvailability(input: TurnOffInput): TurnOffAvailability;
  export function needsWarmingRateHint(remote: HubRemote | null, tauMinutes: number | null): boolean;
  ```

- [ ] **Step 1: Write the failing tests**, with `const guard = load('src/lib/turn-off-guard.ts');`.

```js
const toggle = { learned: true, learnedAt: 1, kind: 'toggle', protocol: 'NEC' };
const base = { remote: toggle, status: 'likely-on', outdoorCelsius: 30, roomCelsius: 22, tauMinutes: 60 };
const check = (o) => guard.getTurnOffAvailability({ ...base, ...o });
const blocked = (reason) => ({ state: 'blocked', reason, message: guard.TURN_OFF_BLOCKED_MESSAGES[reason] });

test('predicted rise of an AC-off room over the 15-minute window', () => {
  assert.ok(Math.abs(guard.predictedOffRoomRise(30, 22.4, 60) - 1.6811) < 1e-3);
});
test('tau 60 needs about a 2.26 °C outdoor/room gap', () => {
  assert.deepEqual(check({ outdoorCelsius: 24.27 }), { state: 'available' });
  assert.deepEqual(check({ outdoorCelsius: 24.25 }), blocked('gap-too-small'));
});
test('tau 120 needs about a 4.26 °C outdoor/room gap', () => {
  assert.deepEqual(check({ tauMinutes: 120, outdoorCelsius: 26.26 }), { state: 'available' });
  assert.deepEqual(check({ tauMinutes: 120, outdoorCelsius: 26.25 }), blocked('gap-too-small'));
});
test('raw codes are guarded like toggle codes', () => {
  assert.deepEqual(check({ remote: { ...toggle, kind: 'raw' }, outdoorCelsius: 24.25 }), blocked('gap-too-small'));
});
test('toggle codes require likely-on, checked before tau', () => {
  for (const status of ['home', 'waiting', 'warming', 'off', 'unable-to-verify'])
    assert.deepEqual(check({ status, tauMinutes: null }), blocked('not-likely-on'));
});
test('no learned tau means no Turn it off for toggle codes', () => {
  for (const tauMinutes of [null, 0, -5, NaN]) assert.deepEqual(check({ tauMinutes }), blocked('no-learned-tau'));
});
test('missing readings block toggle codes', () => {
  assert.deepEqual(check({ outdoorCelsius: null }), blocked('missing-readings'));
  assert.deepEqual(check({ roomCelsius: NaN }), blocked('missing-readings'));
});
test('outdoor colder than the room never passes', () => {
  assert.deepEqual(check({ outdoorCelsius: 20 }), blocked('gap-too-small'));
});
test('ac-state codes are not guarded', () => {
  assert.deepEqual(check({ remote: { ...toggle, kind: 'ac-state' }, status: 'home', tauMinutes: null, outdoorCelsius: null }), { state: 'available' });
});
test('missing or untaught remotes point to Teach remote', () => {
  const teach = { state: 'teach-remote', message: guard.TEACH_REMOTE_MESSAGE };
  assert.deepEqual(check({ remote: null }), teach);
  assert.deepEqual(check({ remote: { learned: false, learnedAt: null, kind: null, protocol: null } }), teach);
});
test('toggle and raw remotes without a learned tau get the warming-rate hint', () => {
  assert.equal(guard.needsWarmingRateHint(toggle, null), true);
  assert.equal(guard.needsWarmingRateHint({ ...toggle, kind: 'raw' }, NaN), true);
  assert.equal(guard.needsWarmingRateHint(toggle, 60), false);
  assert.equal(guard.needsWarmingRateHint({ ...toggle, kind: 'ac-state' }, null), false);
  assert.equal(guard.needsWarmingRateHint({ learned: false, learnedAt: null, kind: null, protocol: null }, null), false);
  assert.equal(guard.needsWarmingRateHint(null, null), false);
});
```

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/turn-off-guard.test.cjs`
Expected: FAIL (module missing).

- [ ] **Step 3: Implement `src/lib/turn-off-guard.ts`**

- `getTurnOffAvailability` checks in this order:
  1. teach-remote: remote is null or not learned
  2. available: kind is `ac-state`
  3. not-likely-on
  4. no-learned-tau: τ not finite, or ≤ 0
  5. missing-readings
  6. gap-too-small: `predictedOffRoomRise < 0.5`
- `predictedOffRoomRise` = `(outdoor − room) × (1 − Math.exp(−15 / τ))`.
- `needsWarmingRateHint` = remote learned, and kind is `toggle` or `raw`, and τ not finite or ≤ 0. It needs no new math.
- Copy:
  - `TEACH_REMOTE_MESSAGE`: "Teach your remote first so the hub can turn the AC off."
  - `TOGGLE_LEARNING_HINT`: "Your remote uses a power button, so Turn it off unlocks once Home Guard has learned how your room warms. Switch the AC off and run Calibration to teach it. It needs two good runs."
  - `TURN_OFF_BLOCKED_MESSAGES`:
    - not-likely-on: "Your remote uses a power button, so the hub only sends it when the AC is likely on. Right now that isn't clear."
    - no-learned-tau: "Your remote uses a power button, so the hub won't send it until Home Guard has learned how your room warms. Please turn the AC off by hand this time."
    - missing-readings: "Your remote uses a power button, so the hub needs fresh room and outdoor readings before sending it."
    - gap-too-small: "Your remote uses a power button. Outdoor air is too close to room temperature to be sure the AC is on, so the hub won't send it."

- [ ] **Step 4: Run to verify pass**

Run: `node --test tests/*.test.cjs`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/lib/turn-off-guard.ts tests/turn-off-guard.test.cjs
git commit -m "feat(ac): add power-toggle turn-off guard"
```

---

### Task 3: Read the AC from its contract children only

Today `subscribeToDevice` and `getDeviceSnapshot` read all of `devices/{deviceId}`, including `temperatureReadings`. Once the hub pushes a reading every minute, every update would re-download the whole history. This task fixes that before any hub writes data.

**Decision on legacy fields:** `parseAcSnapshot` currently also reads top-level fields:
- `temperature`
- `temperatureCelsius`
- `lastSeenAt`
- `powerState`
- `powerStateUpdatedAt`
- `connectionStatus`
- `connected`
- `lastCommand`

These are dropped. Only `docs/RUN.md` used that flat layout, and it already documents the nested `telemetry` / `commands/latest` layout. No device writes the flat layout. `telemetry.powerState` and `telemetry.powerStateUpdatedAt` are still read, because they are optional in RUN.md. Task 11 records this in RUN.md.

**Files:**
- Create: `src/lib/ac-device.ts`
- Modify: `src/services/firebase.ts`: delete `parseAcSnapshot`, move `parseCommand` into `src/lib/ac-device.ts` and import it back for the stove parser, then rewrite the live branches of `getDeviceSnapshot` and `subscribeToDevice` and add `requestRemoteLearning`.
- Modify: `src/constants/demo.ts`, `src/services/ac-monitoring.ts` (`emptyAcMonitoringState`)
- Test: `tests/ac-device.test.cjs`

**Interfaces:**
- Consumes: Task 1 parsers and `getHubConnectionStatus`.
- Produces:
  ```ts
  // src/lib/ac-device.ts
  export type AcDeviceChildPath = 'telemetry' | 'remote' | 'commands/latest' | 'commands/result' | 'commands/learnRequest' | 'learn/status';
  export const AC_DEVICE_CHILD_PATHS: readonly AcDeviceChildPath[];
  export type AcDeviceChildren = Partial<Record<AcDeviceChildPath, unknown>>;
  export function parseCommand(value: unknown): DeviceCommand | null;   // moved unchanged from firebase.ts
  export function buildAcSnapshot(deviceId: string, children: AcDeviceChildren, now: number): AcDeviceSnapshot;
  // src/services/firebase.ts
  export async function requestRemoteLearning(deviceId: string): Promise<void>;
  ```

- [ ] **Step 1: Write the failing tests**, with `const acDevice = load('src/lib/ac-device.ts', { '@/lib/hub': load('src/lib/hub.ts') });`.

```js
const build = (children) => acDevice.buildAcSnapshot('homeguard-a1b2c3', children, NOW);
test('AC reads are limited to the contract children', () => {
  assert.deepEqual([...acDevice.AC_DEVICE_CHILD_PATHS], ['telemetry', 'remote', 'commands/latest', 'commands/result', 'commands/learnRequest', 'learn/status']);
});
test('snapshot is built from the contract children', () => {
  const s = build({
    telemetry: { temperatureCelsius: 22.4, lastSeenAt: NOW - MINUTE },
    remote: { learned: true, learnedAt: 5, kind: 'toggle', protocol: 'NEC' },
    'commands/latest': { value: 'OFF', requestedAt: 100, requestedBy: 'uid', source: 'notification' },
    'commands/result': { requestedAt: 100, status: 'sent', handledAt: 101 },
    'commands/learnRequest': { requestedAt: 90, requestedBy: 'uid' },
    'learn/status': { requestedAt: 90, state: 'saved', updatedAt: 95 },
  });
  assert.equal(s.deviceType, 'ac');
  assert.equal(s.roomTemperatureCelsius, 22.4);
  assert.equal(s.lastSeenAt, NOW - MINUTE);
  assert.equal(s.connectionStatus, 'online');
  assert.equal(s.powerState, null);
  assert.deepEqual(s.lastCommand, { value: 'OFF', requestedAt: 100, source: 'notification' });
  assert.equal(s.commandResult.status, 'sent');
  assert.equal(s.learnRequestedAt, 90);
  assert.equal(s.learnStatus.state, 'saved');
  assert.equal(s.remote.kind, 'toggle');
});
test('missing children give an offline, empty snapshot', () => {
  const s = build({});
  assert.equal(s.connectionStatus, 'offline');
  assert.ok(Number.isNaN(s.roomTemperatureCelsius));
  assert.equal(s.lastSeenAt, 0);
  for (const key of ['lastCommand', 'remote', 'commandResult', 'learnStatus', 'learnRequestedAt']) assert.equal(s[key], null);
});
test('stale telemetry is offline and optional powerState is still read', () => {
  const s = build({ telemetry: { temperatureCelsius: 22, lastSeenAt: NOW - 3 * MINUTE - 1, powerState: 'ON', powerStateUpdatedAt: NOW - MINUTE } });
  assert.equal(s.connectionStatus, 'offline');
  assert.equal(s.powerState, 'ON');
  assert.equal(s.powerStateUpdatedAt, NOW - MINUTE);
});
```

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/ac-device.test.cjs`
Expected: FAIL (module missing).

- [ ] **Step 3: Implement.**
  - **`buildAcSnapshot`:**
    - Reads `telemetry.temperatureCelsius`. A missing or non-finite value gives `NaN`.
    - `lastSeenAt`: finite, or `0`.
    - `powerState`: `'ON' | 'OFF' | null`.
    - `powerStateUpdatedAt`: positive, or `null`.
    - `connectionStatus`: `getHubConnectionStatus(lastSeenAt, now)`.
    - `lastCommand`: `parseCommand(children['commands/latest'])`.
    - Parsers for the rest. `learnRequestedAt` must be finite, or `null`.
  - **Live `getDeviceSnapshot`:** run `Promise.all` over `AC_DEVICE_CHILD_PATHS`, calling `get(ref(db, \`devices/${deviceId}/${path}\`))` for each, then `buildAcSnapshot(deviceId, children, Date.now())`.
  - **Live `subscribeToDevice`:** one `onValue` per child path, each storing its value into a shared `children` object.
    - Emit `buildAcSnapshot(...)` only once all six paths have delivered their first value. That avoids a flash of "offline" or "Teach remote" from partial data. After that, emit on every change.
    - Keep the existing `cancelled` handling.
    - Unsubscribe all six on cleanup.
  - **`getRoomTemperatureReadings`:** unchanged.
  - **Live `requestRemoteLearning(deviceId)`:** `set(ref(db, \`devices/${deviceId}/commands/learnRequest\`), { requestedAt: serverTimestamp(), requestedBy: uid })`. Don't read the value back: screens match `learn/status.requestedAt` against the snapshot's `learnRequestedAt`.
  - **Mock `requestRemoteLearning`:** a no-op until Task 4.
  - **`DEMO_AC_SNAPSHOT`:**
    - Add `remote: { learned: true, learnedAt: Date.now() - 7 * 24 * 60 * 60_000, kind: 'ac-state', protocol: 'DAIKIN' }`, `commandResult: null`, `learnStatus: null` and `learnRequestedAt: null`.
    - Set `lastCommand: null`. The old demo `ON` command has no result and would show "Waiting for the hub".
  - **`emptyAcMonitoringState()`:** add `lastCalibratedCommandAt: null`.

- [ ] **Step 4: Verify**

Run: `node --test tests/*.test.cjs && npm run typecheck && npm run lint`
Expected: everything passes, and `grep -n "\`devices/\${deviceId}\`)" src/services/firebase.ts` matches only the two stove functions.

- [ ] **Step 5: Commit**

```bash
git add src/lib/ac-device.ts src/services/firebase.ts src/constants/demo.ts src/services/ac-monitoring.ts tests/ac-device.test.cjs
git commit -m "perf(firebase): read the AC from its contract children instead of the whole device node"
```

---

### Task 4: Mock hub

**Files:**
- Modify: `src/services/firebase.ts` (mock branches only)

**Interfaces:**
- Produces:
  ```ts
  export function setMockRemote(kind: RemoteKind | null): void;            // null = not taught
  export function setMockLearnOutcome(outcome: Exclude<LearnState, 'listening'>): void;
  export function setMockHubOnline(online: boolean): void;
  export function markMockHubProvisioned(deviceId: string): void;
  ```

- [ ] **Step 1: Implement.**
  - **Constants:** `MOCK_RESULT_DELAY_MS = 800`, `MOCK_LISTEN_DELAY_MS = 600`, `MOCK_LEARN_DELAY_MS = 4_000`, `MOCK_HUB_BOOT_DELAY_MS = 3_000`.
  - **Mock state:** `mockLearnOutcome = 'saved'`, `mockTeachKind: RemoteKind = 'ac-state'`.
  - Every change notifies `mockAcListeners`, the way `setMockTemperature` does.
  - **`sendDeviceCommand` (mock branch):**
    - **Stop setting `powerState` and `powerStateUpdatedAt`.** A command never proves the AC is off.
    - Set `lastCommand`.
    - If the mock hub is online, after `MOCK_RESULT_DELAY_MS` set `commandResult = { requestedAt: command.requestedAt, status, handledAt: Date.now() }`.
    - `status` is `'unsupported'` for `ON`, `'no-code'` if `remote?.learned !== true`, otherwise `'sent'`.
    - If the mock hub is offline, write no result.
  - **`requestRemoteLearning` (mock):**
    - Set `learnRequestedAt = Date.now()`.
    - After `MOCK_LISTEN_DELAY_MS`, set `learnStatus` to `listening`.
    - After `MOCK_LEARN_DELAY_MS`, set it to `mockLearnOutcome`.
    - On `saved`, also set `remote = { learned: true, learnedAt: Date.now(), kind: mockTeachKind, protocol: mockTeachKind === 'ac-state' ? 'DAIKIN' : 'UNKNOWN' }`.
  - **`setMockRemote(kind)`:** `null` sets the untaught shape. Any other kind sets a learned remote of that kind and stores `mockTeachKind = kind`.
  - **`setMockHubOnline(online)`:** sets `connectionStatus`, and sets `lastSeenAt` to `Date.now()` when online or `Date.now() - 4 * 60_000` when offline.
  - **`markMockHubProvisioned(deviceId)`:** after `MOCK_HUB_BOOT_DELAY_MS`, set `deviceId`, `lastSeenAt = Date.now()`, `connectionStatus = 'online'` and an untaught remote.

- [ ] **Step 2: Verify**

Run: `npm run typecheck && npm run lint && node --test tests/*.test.cjs`
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/services/firebase.ts
git commit -m "feat(firebase): add mock hub for results, Teach remote and setup"
```

---

### Task 5: Guard the AC notification

**Files:**
- Modify: `src/services/ac-monitoring.ts:287-295` (the `likely-on` notification block)
- Modify: `src/services/notifications.ts` (`ScheduleAcReminderOptions`, `scheduleAcReminder`)
- Test: `tests/ac-monitoring-hub.test.cjs`

**Interfaces:**
- Consumes: `getTurnOffAvailability`, `TurnOffAvailability` (Task 2).
- Produces: `ScheduleAcReminderOptions` gains `turnOff?: TurnOffAvailability`, defaulting to `{ state: 'available' }`. `sendAcReminderNow` passes it through.

- [ ] **Step 1: Write the failing tests.**

  The fixture `monitoring({ remote, tau, outdoorCelsius, departure = true, calibration, stateReadings, settingsPatch, snapshotPatch })` loads `src/services/ac-monitoring.ts` with these fakes:
  - `@react-native-async-storage/async-storage`: in-memory `{ getItem, setItem }`, seeded under `'home_guard.ac_monitoring.v1'`.
  - `@/services/firebase`: `{ getDeviceSnapshot, getRoomTemperatureReadings: async () => [] }`.
  - `@/services/notifications`: `{ cancelAcReminders: async () => {}, sendAcReminderNow: async (o) => { reminders.push(o); return 'id'; } }`.
  - `@/services/outdoor-temperature`: `{ getNearbyOutdoorTemperature: async () => outdoor }`.
  - `@/services/settings`: `{ loadSettings: async () => settings }`.
  - `@/lib/turn-off-guard`: `load('src/lib/turn-off-guard.ts')`.

  Defaults:
  - **Settings:** `{ ...DEFAULT_SETTINGS, setupComplete: true, acDeviceId: 'homeguard-a1b2c3', phoneDepartedAt: NOW - 25 * MINUTE }`.
  - **Snapshot:** room 22, `lastSeenAt: NOW - MINUTE`, `connectionStatus: 'online'`, `powerState: 'ON'` with `powerStateUpdatedAt: NOW - MINUTE`. A fresh power ON is the shortest path to `likely-on`.
  - **Stored state:** a departure at `NOW - 25 * MINUTE` with `alertSent: false`, `temperatureAtDeparture: 22` and threshold 26. `thermalDeviceId` is set and `thermalTimeConstantsMinutes: tau`.
  - **Outdoor:** `{ temperatureCelsius: outdoorCelsius ?? 30, timestamp: NOW - 5 * MINUTE, stationName: 'Test' }`.
  - **Helpers:** `m.snapshot`, `m.settings`, `m.setOutdoor(c)`, `stateReadings: n` (n readings one minute apart before `NOW`), and `calibration` (a partial object, completed with `deviceId`, `temperatureAtStart: 22` and `outdoorAtStart`).

```js
const toggle = { learned: true, learnedAt: 1, kind: 'toggle', protocol: 'NEC' };
test('toggle remote without a learned warming rate notifies without Turn it off', async () => {
  const m = monitoring({ remote: toggle, tau: [] });
  await m.evaluateAcMonitoring();
  assert.equal(m.reminders.length, 1);
  assert.equal(m.reminders[0].turnOff.reason, 'no-learned-tau');
});
test('toggle remote with learned tau and a clear gap keeps Turn it off', async () => {
  const m = monitoring({ remote: toggle, tau: [60, 60] });
  await m.evaluateAcMonitoring();
  assert.deepEqual(m.reminders[0].turnOff, { state: 'available' });
});
test('toggle remote on a mild night is blocked by the gap rule', async () => {
  const m = monitoring({ remote: toggle, tau: [60, 60], outdoorCelsius: 23 });
  await m.evaluateAcMonitoring();
  assert.equal(m.reminders[0].turnOff.reason, 'gap-too-small');
});
test('ac-state remote is not guarded', async () => {
  const m = monitoring({ remote: { ...toggle, kind: 'ac-state' }, tau: [] });
  await m.evaluateAcMonitoring();
  assert.deepEqual(m.reminders[0].turnOff, { state: 'available' });
});
test('untaught remote points the notification to Teach remote', async () => {
  const m = monitoring({ remote: { learned: false, learnedAt: null, kind: null, protocol: null } });
  await m.evaluateAcMonitoring();
  assert.equal(m.reminders[0].turnOff.state, 'teach-remote');
});
```

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/ac-monitoring-hub.test.cjs`
Expected: FAIL (`turnOff` undefined).

- [ ] **Step 3: Implement.**
  - In the notification block, compute `getTurnOffAvailability({ remote: snapshot.remote, status: state.status, outdoorCelsius: outdoor?.temperatureCelsius ?? null, roomCelsius: snapshot.roomTemperatureCelsius, tauMinutes: learnedThermalTimeConstant(state) })` and pass it as `turnOff` to `sendAcReminderNow`.
  - In `scheduleAcReminder`, when `turnOff.state !== 'available'`:
    - Leave out `categoryIdentifier`, so the notification has no Turn It Off / Keep It On buttons.
    - Append `' ' + turnOff.message` to the existing body.
    - Keep the titles and the `data` payload, so `cancelAcReminders` still finds the notification.

- [ ] **Step 4: Run to verify pass**

Run: `node --test tests/*.test.cjs && npm run typecheck`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/services/ac-monitoring.ts src/services/notifications.ts tests/ac-monitoring-hub.test.cjs
git commit -m "feat(ac): apply power-toggle guard to the AC notification"
```

---

### Task 6: Automatic calibration after a shut-off

There is one source: a matching `commands/result` with status `sent`. The Teach remote Test step creates one, so it covers the spec's "after Teach remote" case too. That case gets the same 1 °C outdoor/room requirement.

**Files:**
- Modify: `src/services/ac-monitoring.ts` (`observeCalibration`, `beginAcDeparture`, `startAcCalibration`, new export)
- Modify: `src/state/app-provider.tsx` (device subscription effect)
- Test: `tests/ac-monitoring-hub.test.cjs` (append)

**Interfaces:**
- Consumes: snapshot `lastCommand` and `commandResult` (Task 3).
- Produces:
  ```ts
  export const AUTO_CALIBRATION_MAX_RESULT_AGE_MS = 10 * 60_000;
  export function calibrateAfterCommandResult(snapshot: AcDeviceSnapshot): Promise<AcMonitoringState>;
  ```

- [ ] **Step 1: Write the failing tests** (same fixture). Use whole degrees at the 1 °C boundary: in floating point, `23.4 - 22.4` comes out below 1.

```js
const off = (requestedAt, status = 'sent', handledAt = requestedAt + 1_000) => ({
  powerState: null,
  lastCommand: { value: 'OFF', requestedAt, source: 'notification' },
  commandResult: { requestedAt, status, handledAt },
});
test('a matching sent result starts a silent shut-off calibration without wiping readings', async () => {
  const m = monitoring({ outdoorCelsius: 30, snapshotPatch: off(NOW - 30_000), stateReadings: 3 });
  const state = await m.calibrateAfterCommandResult(m.snapshot);
  assert.equal(state.calibration.source, 'shut-off');
  assert.equal(state.calibration.startedAt, NOW - 29_000); // handledAt
  assert.equal(state.calibration.temperatureAtStart, 22);
  assert.equal(state.calibration.outdoorAtStart.temperatureCelsius, 30);
  assert.ok(state.readings.length >= 3);
  assert.equal(state.lastCalibratedCommandAt, NOW - 30_000);
  assert.equal(state.calibrationMessage, '');
});
test('needs outdoor at least 1 °C warmer than the room', async () => {
  for (const [outdoorCelsius, started] of [[22.9, false], [23, true]]) {
    const m = monitoring({ departure: false, outdoorCelsius, snapshotPatch: off(NOW - 30_000) });
    assert.equal((await m.calibrateAfterCommandResult(m.snapshot)).calibration !== null, started);
  }
});
test('each result is handled once, even when it could not start', async () => {
  const m = monitoring({ outdoorCelsius: 22.5, snapshotPatch: off(NOW - 30_000) });
  await m.calibrateAfterCommandResult(m.snapshot);
  m.setOutdoor(30);
  assert.equal((await m.calibrateAfterCommandResult(m.snapshot)).calibration, null);
});
test('non-sent, unmatched or old results start nothing', async () => {
  for (const patch of [off(NOW - 30_000, 'no-code'), { ...off(NOW - 30_000), commandResult: { requestedAt: NOW - 90_000, status: 'sent', handledAt: NOW - 89_000 } }, off(NOW - 11 * MINUTE)]) {
    const m = monitoring({ snapshotPatch: patch });
    assert.equal((await m.calibrateAfterCommandResult(m.snapshot)).calibration, null);
  }
});
test('a running calibration is never replaced', async () => {
  const m = monitoring({ calibration: { source: 'manual', startedAt: NOW - 5 * MINUTE }, snapshotPatch: off(NOW - 30_000) });
  assert.equal((await m.calibrateAfterCommandResult(m.snapshot)).calibration.source, 'manual');
});
test('shut-off calibrations give up silently; manual ones keep their message', async () => {
  const run = (source) => monitoring({ departure: false, calibration: { source, startedAt: NOW - 121 * MINUTE }, settingsPatch: { phoneDepartedAt: null } }).evaluateAcMonitoring();
  assert.equal((await run('shut-off')).calibrationMessage, '');
  assert.match((await run('manual')).calibrationMessage, /^Calibration stopped/);
});
test('leaving home keeps a shut-off calibration but stops a manual one', async () => {
  const leave = (source) => { const m = monitoring({ departure: false, calibration: { source, startedAt: NOW - 5 * MINUTE }, settingsPatch: { phoneDepartedAt: NOW } }); return m.beginAcDeparture(m.settings); };
  assert.equal((await leave('shut-off')).calibration.source, 'shut-off');
  assert.equal((await leave('manual')).calibration, null);
});
```

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/ac-monitoring-hub.test.cjs`
Expected: the new tests FAIL.

- [ ] **Step 3: Implement.**
  - **`calibrateAfterCommandResult`** (wrapped in `serialize`; don't call other serialized functions inside it, because the queue would deadlock):
    - Proceed only if all of these hold: `commandResult.status === 'sent'`, `commandResult.requestedAt === lastCommand?.requestedAt`, and that value `!== state.lastCalibratedCommandAt`.
    - When those match, always set `lastCalibratedCommandAt`.
    - Start the calibration only if all of these hold:
      - `Date.now() - handledAt <= AUTO_CALIBRATION_MAX_RESULT_AGE_MS`
      - no calibration is running
      - `snapshot.deviceId === settings.acDeviceId`
      - `freshTemperature(snapshot, now)`
      - `freshPowerState(snapshot, now) !== 'ON'`
      - outdoor (from `getNearbyOutdoorTemperature(settings.homeLocation)`) is at least 1 °C warmer than the room
    - To start it:
      - Apply the same `thermalDeviceId` reset as `startAcCalibration`.
      - **Merge** the reading with `mergeReadings`; don't reset.
      - Set `state.outdoor`.
      - Set `state.calibration = { deviceId, startedAt: handledAt, temperatureAtStart: snapshot.roomTemperatureCelsius, outdoorAtStart: outdoor, source: 'shut-off' }`.
      - Leave `calibrationMessage` untouched.
  - **`startAcCalibration`** sets `source: 'manual'`.
  - **`observeCalibration`** for `shut-off`:
    - The stop branch clears `calibration` without touching `calibrationMessage`.
    - On success, prefix the existing success message with "The room is warming, so the AC is likely off now. ".
  - **`beginAcDeparture`** stops the calibration, with its existing message, only when `source` is `'manual'` or missing.
  - **`app-provider.tsx`:** in the `subscribeToDevice` effect, next to `recordAcReading`, add `void calibrateAfterCommandResult(snapshot).then((state) => { if (state.calibration) void startAcBackgroundMonitoring().catch(() => undefined); })`.

- [ ] **Step 4: Run to verify pass**

Run: `node --test tests/*.test.cjs && npm run typecheck && npm run lint`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/services/ac-monitoring.ts src/state/app-provider.tsx tests/ac-monitoring-hub.test.cjs
git commit -m "feat(ac): learn room warming automatically after a shut-off"
```

---

### Task 7: Teach remote screen

**Files:**
- Create: `src/components/teach-remote-flow.tsx`
- Create: `src/components/hub-card.tsx`
- Create: `src/app/(tabs)/settings/teach.tsx`, `src/app/onboarding/teach.tsx`
- Modify: `src/app/(tabs)/settings/ac.tsx` (render `<HubCard />` above `<AcSettings />`)
- Modify: `src/state/app-provider.tsx`, `src/app/(tabs)/settings/developer.tsx`

**Interfaces:**
- Consumes:
  - From Task 3/4: `requestRemoteLearning`, `sendTurnOffCommand`, `setMockLearnOutcome`, `setMockRemote`.
  - From Task 1: `getLearnPhase`, `LEARN_PHASE_MESSAGES`, `describeLastCommand`.
  - From Task 2: `needsWarmingRateHint`, `TOGGLE_LEARNING_HINT`.
  - `learnedThermalTimeConstant`.
- Produces:
  ```ts
  // AppContextValue additions
  acDevice: AcDeviceSnapshot;
  startRemoteLearning: () => Promise<void>;   // requestRemoteLearning(settings.acDeviceId)
  testRemote: () => Promise<void>;            // sendTurnOffCommand(settings.acDeviceId, 'app'); no guard: the user just turned the AC on
  setDemoLearnOutcome: (outcome: 'saved' | 'timeout' | 'rejected') => void;
  setDemoRemote: (kind: RemoteKind | null) => void;
  // components
  export function TeachRemoteFlow(props: { onDone: () => void; onOpenCalibration: () => void; onSkip?: () => void }): JSX.Element;
  export function HubCard(): JSX.Element;
  ```

- [ ] **Step 1: Context.** Add the members above to `app-provider.tsx`, with `acDevice: device`.

- [ ] **Step 2: `TeachRemoteFlow`**, following spec "2. Teach remote". Layout and any other copy are free, within the existing design system.
  1. **Prepare:** "Turn your AC on with its remote, then tap Start." Button **Start**.
  2. **Start:**
     - Record `requestBefore = acDevice.learnRequestedAt` and `attemptStartedAt = Date.now()`, then call `startRemoteLearning()`.
     - Tick `now` every second. Render `LEARN_PHASE_MESSAGES[getLearnPhase(...)]`, with `currentRequest = acDevice.learnRequestedAt` and `status = acDevice.learnStatus`.
     - While listening, also show "Your AC will probably turn off too. That's expected."
  3. **`timeout` / `rejected` / `no-response`:** button **Try again**, which goes back to Prepare.
  4. **`saved`:** "Turn your AC back on with its remote, then test it." Buttons **Test it** and **Skip test**. Skip test goes to Success.
  5. **Test it:**
     - Record `commandBefore = acDevice.lastCommand?.requestedAt ?? null`, then `await testRemote()`.
     - Show `describeLastCommand(...)` once `acDevice.lastCommand.requestedAt !== commandBefore`.
     - On a matched `sent`, ask "Did your AC turn off?" **Yes** goes to Success. **No** goes back to Prepare.
     - On any other matched status, show its message and **Try again**.
     - The `sent` result already starts the shut-off calibration (Task 6). Nothing else is needed here.
  6. **Success:** "Remote taught." If `needsWarmingRateHint(acDevice.remote, learnedThermalTimeConstant(acMonitoring))`, also show `TOGGLE_LEARNING_HINT` and a button **Open Calibration** (`onOpenCalibration`). Button **Done** (`onDone`).
  7. Disable Start when `connectionStatus === 'offline'`, and say "The hub is offline."

- [ ] **Step 3: Routes.**
  - **`settings/teach.tsx`:** `SettingsPage` titled "Teach remote". `onDone={() => router.back()}`, `onOpenCalibration={() => router.replace('/settings/ac')}`. The calibration card is in `AcSettings` on that screen.
  - **`onboarding/teach.tsx`:** an onboarding shell like the header of `onboarding/reminder.tsx`. `onDone`, `onSkip` and `onOpenCalibration` all go to `router.push('/onboarding/reminder')`; that screen also renders `AcSettings` with the calibration card.

- [ ] **Step 4: `HubCard`** (Settings → AC) shows:
  - `settings.acDeviceId` and online/offline
  - "Remote taught · {protocol}" or "Remote not taught"
  - a button **Teach remote** (`router.push('/settings/teach')`)

  Task 10 adds **Add hub** to this card.

- [ ] **Step 5: Demo lab.** In `developer.tsx`, add a "Hub" block with:
  - `SegmentedControl` "Remote" (Not taught / AC state / Toggle / Raw), calling `setDemoRemote`
  - `SegmentedControl` "Next teach result" (Saved / Timeout / Rejected), calling `setDemoLearnOutcome`

- [ ] **Step 6: Verify**

Run: `npx expo start` and stop it when Metro is ready. Then run `npm run typecheck && npm run lint && node --test tests/*.test.cjs`.
Expected: no errors.

Demo check (`npm run ios`, then Explore the demo):
- Go to Settings → AC monitoring → Teach remote. Start shows "Waiting for the hub…", then the listening copy, then the saved step.
- Test it shows "Command sent" and then the question. Yes shows Success.
- With Remote set to Toggle beforehand, Success shows the hint, and Open Calibration lands on AC monitoring.
- With Timeout and with Rejected, the right message appears and Try again works.

- [ ] **Step 7: Commit**

```bash
git add src/components/teach-remote-flow.tsx src/components/hub-card.tsx "src/app/(tabs)/settings/teach.tsx" src/app/onboarding/teach.tsx "src/app/(tabs)/settings/ac.tsx" "src/app/(tabs)/settings/developer.tsx" src/state/app-provider.tsx
git commit -m "feat(hub): add Teach remote flow and hub card"
```

---

### Task 8: Honest Turn it off: re-check, result messages, offline status

**Files:**
- Create: `src/services/turn-off.ts`
- Modify: `src/services/notifications.ts`: `processNotificationResponse` stops sending, and the `sendTurnOffCommand` import goes.
- Modify: `src/state/app-provider.tsx`, `src/app/(tabs)/dashboard.tsx`, `src/app/(tabs)/settings/developer.tsx`
- Test: `tests/turn-off.test.cjs`

**Interfaces:**
- Consumes: `evaluateAcMonitoring`, `learnedThermalTimeConstant`, `getDeviceSnapshot`, `sendTurnOffCommand`, `loadSettings`, `getTurnOffAvailability`, `needsWarmingRateHint`, `TOGGLE_LEARNING_HINT`, `describeLastCommand`, `getHubConnectionStatus`, `setMockHubOnline`.
- Produces:
  ```ts
  // src/services/turn-off.ts
  export type TurnOffOutcome =
    | { sent: true; command: DeviceCommand }
    | { sent: false; availability: Exclude<TurnOffAvailability, { state: 'available' }> };
  export async function requestTurnOff(source: DeviceCommandSource): Promise<TurnOffOutcome>;
  // AppContextValue changes
  turnOff: () => Promise<TurnOffOutcome>;          // was Promise<void>; calls requestTurnOff('app')
  turnOffAvailability: TurnOffAvailability;        // useMemo from device + acMonitoring
  lastCommandLabel: string;                        // describeLastCommand(device.lastCommand, device.commandResult)
  connectionStatus: ConnectionStatus;              // live: getHubConnectionStatus(device.lastSeenAt, now); mock: device.connectionStatus
  setDemoHubOnline: (online: boolean) => void;
  ```

- [ ] **Step 1: Write the failing tests.** Load the module with these fakes:

```js
load('src/services/turn-off.ts', {
  '@/services/ac-monitoring': { evaluateAcMonitoring, learnedThermalTimeConstant: (s) => s.tau ?? null },
  '@/services/firebase': { getDeviceSnapshot, sendTurnOffCommand },
  '@/services/settings': { loadSettings },
  '@/lib/turn-off-guard': load('src/lib/turn-off-guard.ts'),
});
```

  In the `turnOff(...)` fixture:
  - The fake `evaluateAcMonitoring` counts its calls. It returns `status` (after applying `afterEvaluate`), `outdoor: { temperatureCelsius }` and `tau`.
  - The snapshot has room 22 and the given remote.

```js
const toggle = { learned: true, learnedAt: 1, kind: 'toggle', protocol: 'NEC' };
const acState = { ...toggle, kind: 'ac-state', protocol: 'DAIKIN' };
test('requestTurnOff sends for an ac-state remote even at home', async () => {
  const t = turnOff({ remote: acState, state: { status: 'home', tau: null } });
  assert.equal((await t.requestTurnOff('app')).sent, true);
  assert.deepEqual(t.sent, [['homeguard-a1b2c3', 'app']]);
});
test('requestTurnOff refuses a toggle remote without a learned warming rate', async () => {
  const t = turnOff({ remote: toggle, state: { status: 'likely-on', tau: null, outdoor: 30 } });
  const outcome = await t.requestTurnOff('notification');
  assert.equal(outcome.availability.reason, 'no-learned-tau');
  assert.equal(t.sent.length, 0);
});
test('requestTurnOff re-evaluates before deciding', async () => {
  const t = turnOff({ remote: toggle, state: { status: 'likely-on', tau: 60, outdoor: 30 }, afterEvaluate: { status: 'warming' } });
  const outcome = await t.requestTurnOff('notification');
  assert.equal(t.evaluations, 1);
  assert.equal(outcome.availability.reason, 'not-likely-on');
  assert.equal(t.sent.length, 0);
});
test('requestTurnOff points to Teach remote when the remote is not taught', async () => {
  const t = turnOff({ remote: { learned: false, learnedAt: null, kind: null, protocol: null }, state: { status: 'likely-on' } });
  assert.equal((await t.requestTurnOff('app')).availability.state, 'teach-remote');
});
```

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/turn-off.test.cjs`
Expected: FAIL (module missing).

- [ ] **Step 3: Implement `requestTurnOff`.**
  1. `loadSettings()`.
  2. `state = await evaluateAcMonitoring()`.
  3. `getDeviceSnapshot(settings.acDeviceId)`.
  4. Build the availability from `state.status`, `state.outdoor?.temperatureCelsius ?? null`, `snapshot.roomTemperatureCelsius` and `learnedThermalTimeConstant(state)`.
  5. Call `sendTurnOffCommand(settings.acDeviceId, source)` only when the availability is `available`.

- [ ] **Step 4: Notification action.**
  - `processNotificationResponse` returns `{ action: 'turn_off', deviceId, deviceType }` without sending. This avoids a `notifications → turn-off → ac-monitoring → notifications` import cycle.
  - In `app-provider.tsx`, handle `turn_off` from both `addNotificationActionListener` and `processLastNotificationResponse()`: call `requestTurnOff('notification')`. When `!outcome.sent`, call `Alert.alert('Command not sent', outcome.availability.message)`.

- [ ] **Step 5: Dashboard.**
  - **`teach-remote`:** replace Turn off AC with a **Teach remote** button (`router.push('/settings/teach')`) and show the message.
  - **`blocked`:** disable the button and show the message under it. If `needsWarmingRateHint(device.remote, learnedThermalTimeConstant(acMonitoring))`, also show `TOGGLE_LEARNING_HINT` and an **Open Calibration** button (`router.push('/settings/ac')`).
  - **Press that returns `{ sent: false }`:** show that message inline.
  - **"Last command" card:** shows `lastCommandLabel`.
  - **Sensor row:** uses the new `connectionStatus`. It is recomputed on the existing 15 s `now` tick, so live goes offline 3 minutes after the last telemetry.
  - Leave other labels unchanged.

- [ ] **Step 6: Demo lab.**
  - Add a `Switch` "Hub online" to the "Hub" block, calling `setDemoHubOnline`.
  - Make `sendTestReminder` pass `turnOff: getTurnOffAvailability({ remote: device.remote, status: 'likely-on', outdoorCelsius: acMonitoring.outdoor?.temperatureCelsius ?? null, roomCelsius: device.roomTemperatureCelsius, tauMinutes: learnedThermalTimeConstant(acMonitoring) })`.

- [ ] **Step 7: Verify**

Run: `node --test tests/*.test.cjs && npm run typecheck && npm run lint`
Expected: all pass.

Demo check:
- **AC state:** Turn off AC shows "Waiting for the hub", then "Command sent". The AC likelihood card never turns to "Off".
- **Not taught:** the Teach remote button appears, and a result of "Teach your remote first" is possible.
- **Toggle:** the button is disabled, with the not-likely-on message, the hint and Open Calibration. "Send AC alert now" arrives with no action buttons and the no-learned-tau text.
- **Hub online off:** the row says offline, and a command stays at "Waiting for the hub".

- [ ] **Step 8: Commit**

```bash
git add src/services/turn-off.ts src/services/notifications.ts src/state/app-provider.tsx "src/app/(tabs)/dashboard.tsx" "src/app/(tabs)/settings/developer.tsx" tests/turn-off.test.cjs
git commit -m "feat(ac): re-check toggle guard on Turn it off and show honest command results"
```

---

### Task 9: Bluetooth provisioning library behind one interface

**Choice:** `@orbital-systems/react-native-esp-idf-provisioning@0.7.0`. It was published 2026-10-27, is MIT-licensed, and is maintained by Orbital Systems.
- It is a codegen TurboModule (`codegenConfig`, `TurboModuleRegistry.getEnforcing`), so it works with the new architecture. Its example app builds on RN 0.84.
- It ships an Expo config plugin (`app.plugin.js`).
- On iOS it depends on Espressif's `ESPProvision ~> 3.2`.
- It supports BLE with `ESPSecurity.secure` and a proof-of-possession.

It is not in Expo's bundled module list, so Step 4's build gate proves it works with SDK 55. If the gate fails, do Task 9b instead of Step 5.

**Files:**
- Modify: `package.json`, `package-lock.json`, `app.json`
- Create: `src/services/hub-provisioning/types.ts`, `src/services/hub-provisioning/esp-provisioner.ts`, `src/services/hub-provisioning/mock-provisioner.ts`, `src/services/hub-provisioning/index.ts`

**Interfaces:**
- Consumes: from Task 1, `HUB_NAME_PREFIX`, `HUB_PROOF_OF_POSSESSION`, `deviceIdFromHubName`, `classifyProvisionError`, `tidyWifiNetworks` and `ProvisionFailure`; from Tasks 3 and 4, `markMockHubProvisioned` and `getFirebaseMode`.
- Produces:
  ```ts
  // types.ts
  export interface HubProvisioner {
    scanForHubs(): Promise<HubCandidate[]>;
    connect(hub: HubCandidate): Promise<void>;
    listWifiNetworks(): Promise<WifiNetwork[]>;          // already tidied
    sendWifiCredentials(ssid: string, password: string): Promise<void>; // throws HubProvisioningError
    disconnect(): void;
  }
  export class HubProvisioningError extends Error { constructor(readonly reason: ProvisionFailure, message?: string) }
  // index.ts
  export function getHubProvisioner(): HubProvisioner; // mock when getFirebaseMode() === 'mock' or Platform.OS === 'web'
  ```

- [ ] **Step 1: Install**

```bash
npm install --save-exact @orbital-systems/react-native-esp-idf-provisioning@0.7.0
```

- [ ] **Step 2: Config plugin.** Add this to the `plugins` array in `app.json`:

```json
["@orbital-systems/react-native-esp-idf-provisioning", {
  "transport": "ble",
  "bluetoothAlwaysPermission": "Home Guard uses Bluetooth to set up your HomeGuard hub."
}]
```

- `transport: "ble"` is required. The default, `"both"`, overwrites `NSLocationWhenInUseUsageDescription` with the library's own text.
- Do **not** set `neverForLocation`. It would add `maxSdkVersion="30"` to the `ACCESS_FINE_LOCATION` permission that `expo-location` needs.

- [ ] **Step 3: Implement the provisioners.**
  - **`esp-provisioner.ts`** keeps the current `ESPDevice` in a closure.
    - `scanForHubs`: `ESPProvisionManager.searchESPDevices(HUB_NAME_PREFIX, ESPTransport.ble, ESPSecurity.secure)`, mapped to `{ name, deviceId: deviceIdFromHubName(name) }` with nulls dropped. An error whose message matches `/not found/i` returns `[]`.
    - `connect`: `device.connect(HUB_PROOF_OF_POSSESSION, null, null)`.
    - `listWifiNetworks`: `tidyWifiNetworks(list.map(n => ({ ssid: n.ssid, rssi: n.rssi, secured: n.auth !== ESPWifiAuthMode.open })))`.
    - `sendWifiCredentials`: `device.provision(ssid, password)`. On rejection, throw `new HubProvisioningError(classifyProvisionError(e))`.
  - **`mock-provisioner.ts`:**
    - Scan waits 1 200 ms, then returns `[{ name: 'HomeGuard-A1B2C3', deviceId: 'homeguard-a1b2c3' }]`.
    - Connect takes 800 ms.
    - Wi-Fi list: `HomeGuard Demo Wi-Fi` (−48, secured), `Neighbour 2.4G` (−71, secured), `Cafe Guest` (−80, open).
    - Password `wrong` throws `HubProvisioningError('wrong-password')`. Any other password waits 1 500 ms, then calls `markMockHubProvisioned(deviceId)`.

- [ ] **Step 4: Build gate**

```bash
npx expo prebuild --platform ios
plutil -p ios/*/Info.plist | grep -E "NSBluetoothAlwaysUsageDescription|NSLocationWhenInUseUsageDescription"
npm run ios
npx expo prebuild --platform android && (cd android && ./gradlew assembleDebug)
grep -n "ACCESS_FINE_LOCATION" android/app/src/main/AndroidManifest.xml
```

Expected:
- The Bluetooth string is present, and the location string still reads "Home Guard uses your location to set your home radius."
- `pod install` pulls `ESPProvision (3.2.x)`, and the app builds and launches in the simulator.
- Gradle reports `BUILD SUCCESSFUL`.
- `ACCESS_FINE_LOCATION` has no `maxSdkVersion`.

If `ANDROID_HOME` is unset, report the Android build as unverified rather than skipping it silently. `ios/` and `android/` are gitignored; don't commit them.

- [ ] **Step 5: Verify and commit** (gate passed)

Run: `npm run typecheck && npm run lint && node --test tests/*.test.cjs`

```bash
git add package.json package-lock.json app.json src/services/hub-provisioning
git commit -m "feat(hub): add BLE provisioning behind a HubProvisioner interface"
```

### Task 9b (only if Task 9's gate fails): local Expo module fallback

Use this when the wrapper fails to build or link on SDK 55 / RN 0.83. If the failure is inside the `ESPProvision` pod itself, stop and report: this fallback uses the same pod.

1. Run `npm uninstall @orbital-systems/react-native-esp-idf-provisioning` and remove its `app.json` entry.
2. Run `npx create-expo-module@latest --local homeguard-provisioning` to create `modules/homeguard-provisioning/`. In its `expo-module.config.json`, set `"platforms": ["apple"]` so Android builds without native code.
3. In the module podspec, add `s.dependency 'ESPProvision', '~> 3.2'`.
4. Write the Swift module with the Expo Modules API, as `AsyncFunction`s:
   - `searchHubs(prefix) -> [String]`
   - `connect(name, pop)`
   - `scanWifi() -> [[String: Any]]`, returning ssid, rssi and auth
   - `provision(ssid, passphrase)`
   - `disconnect()`

   Reject with `ESPProvisionError.description` as the message, so `classifyProvisionError` still works. Use `ESPProvisionManager.shared.searchESPDevices(devicePrefix:transport: .ble, security: .secure)`, and `ESPDevice.connect` with a delegate that returns the PoP.
5. Create the config plugin `plugins/with-hub-provisioning.js`. It sets `NSBluetoothAlwaysUsageDescription` to the same string as Task 9. List it in `app.json` `plugins`.
6. Rewrite `esp-provisioner.ts` against `requireOptionalNativeModule('HomeguardProvisioning')`. When that returns `null` (Android), each method throws `HubProvisioningError('unknown', 'Adding a hub is only available on iPhone for now.')`.
7. Rerun the Step 4 gate, then commit as `feat(hub): add local ESPProvision module for hub setup`.

---

### Task 10: Add hub flow

**Files:**
- Create: `src/components/add-hub-flow.tsx`
- Create: `src/app/(tabs)/settings/hub.tsx`, `src/app/onboarding/hub.tsx`
- Modify: `src/app/onboarding/stove.tsx:127` to `router.push('/onboarding/hub')`
- Modify: `src/components/hub-card.tsx` to add **Add hub** (`router.push('/settings/hub')`)

**Interfaces:**
- Consumes:
  - From Task 9: `getHubProvisioner`, `HubProvisioningError`.
  - From Task 1: `getSetupWaitOutcome`, `PROVISION_FAILURE_MESSAGES`.
  - From `@/services/firebase`: `getDeviceSnapshot`.
  - From context: `acDevice`, `patchSettings`.
- Produces: `export function AddHubFlow(props: { onConnected: (deviceId: string) => void; onSkip?: () => void }): JSX.Element`.

- [ ] **Step 1: `AddHubFlow`.** Hold one provisioner per mount (`useRef(getHubProvisioner())`) and call `disconnect()` on unmount. Follow spec "1. First-time setup":
  1. **Intro:** "Plug in your hub. Its light blinks slowly when it's ready." Button **Find my hub**.
  2. **Scan:** call `scanForHubs()`.
     - Nothing found: "No hub found. Keep the phone near the hub and try again." Button **Scan again**.
     - Otherwise, list the hubs by `name`.
  3. **Pick:** call `connect(hub)`, then `listWifiNetworks()`. Show the list, plus **Enter network name** for hidden networks and the hint "The hub only works with 2.4 GHz Wi-Fi."
  4. **Password:** a secure text field for secured networks; open networks skip it. Then **Connect**.
  5. **Send:**
     - First, `lastSeenAtBefore = (await getDeviceSnapshot(hub.deviceId)).lastSeenAt || null`. If that read throws, show "Check your phone's internet connection, then try again." and don't send. Then record `waitStartedAt = Date.now()` and call `sendWifiCredentials`.
     - On `HubProvisioningError`, show `PROVISION_FAILURE_MESSAGES[reason]`.
       - `wrong-password` and `network-not-found` go back to the password or network step on the same connection.
       - `disconnected` and `unknown` go back to Scan.
     - On success, call `disconnect()`, then `await patchSettings({ acDeviceId: hub.deviceId })`, which also resets AC monitoring for the new device. Then go to Wait.
  6. **Wait:** tick `now` every second and compute `getSetupWaitOutcome({ lastSeenAtBefore, lastSeenAt: acDevice.deviceId === hub.deviceId ? acDevice.lastSeenAt : null, waitStartedAt, now })`.
     - `connected`: show "Hub connected", then call `onConnected(hub.deviceId)`.
     - `timed-out`: "The hub joined Wi-Fi but hasn't checked in yet." Two buttons:
       - **Keep waiting** sets `waitStartedAt = Date.now()`.
       - **Start over** shows "Hold the hub's button for 10 seconds to reset it, then add it again."
  7. Show **Skip for now** only when `onSkip` is given.

- [ ] **Step 2: Routes.**
  - **`settings/hub.tsx`:** `SettingsPage` titled "Add hub", with `onConnected` → `router.replace('/settings/teach')`.
  - **`onboarding/hub.tsx`:** onboarding shell, with `onConnected` → `router.push('/onboarding/teach')` and `onSkip` → `router.push('/onboarding/reminder')`.
  - **`stove.tsx`:** now pushes `/onboarding/hub`.

- [ ] **Step 3: Verify**

Run: `npx expo start` and stop it when Metro is ready. Then run `npm run typecheck && npm run lint && node --test tests/*.test.cjs`.
Expected: no errors.

Demo check, in Settings → AC monitoring → Add hub:
- The scan lists `HomeGuard-A1B2C3`, then the Wi-Fi list appears.
- Password `wrong` shows the wrong-password message and stays on the password step.
- A correct password reaches "Hub connected" about 3 s later and lands on Teach remote.
- HubCard now shows `homeguard-a1b2c3` and "Remote not taught".

- [ ] **Step 4: Commit**

```bash
git add src/components/add-hub-flow.tsx src/components/hub-card.tsx "src/app/(tabs)/settings/hub.tsx" src/app/onboarding/hub.tsx src/app/onboarding/stove.tsx
git commit -m "feat(hub): add Bluetooth Add hub flow to onboarding and settings"
```

---

### Task 11: Docs, full verification and demo walkthrough

**Files:**
- Modify: `docs/RUN.md`

- [ ] **Step 1: RUN.md.**
  - Add a "HomeGuard hub" section that:
    - links to the contract by its repo path
    - lists the four new paths
    - says the hub never writes `powerState`
    - says the AC is read only from `telemetry`, `remote`, `commands/*` and `learn/status`, so top-level fields such as `devices/{id}/temperatureCelsius` are no longer read
  - Say that Add hub needs a development build on a physical iPhone, because the simulator has no Bluetooth.
  - Document the Demo lab "Hub" controls and the `wrong` demo password.
  - Under "Checks", replace `node --test tests/stove-detection.test.cjs` with `node --test tests/*.test.cjs`.

- [ ] **Step 2: Full checks**

Run: `node --test tests/*.test.cjs && npm run typecheck && npm run lint && npx expo export --platform ios`
Expected: all pass, and the export completes.

- [ ] **Step 3: Demo-mode walkthrough** (`npm run ios`, no `.env.local`). Tick each item:
  - [ ] Onboarding runs location → stove → **Add hub** (Skip for now works) → Teach remote (skip works) → reminder → dashboard.
  - [ ] Add hub: password `wrong` shows the wrong-password message; a correct password shows "Hub connected" and moves on to Teach remote.
  - [ ] Teach remote, saved path: "Command sent", then "Did your AC turn off?". **No** returns to the start; **Yes** shows Success.
  - [ ] Teach remote with Remote set to Toggle: Success shows the hint, and Open Calibration works.
  - [ ] Teach remote with Timeout and with Rejected shows the right message, and Try again works.
  - [ ] Dashboard with an AC state remote: "Waiting for the hub", then "Command sent". Nothing says the AC is off.
  - [ ] Dashboard with Not taught: a Teach remote button appears and opens Teach remote.
  - [ ] Dashboard with a Toggle remote at home: the button is disabled, with the message, the hint and Open Calibration.
  - [ ] "Send AC alert now" with Toggle: no buttons, and an explanation. With AC state: Turn It Off sends ("Command sent" on the dashboard).
  - [ ] Hub online off: the sensor row says offline, and a command stays at "Waiting for the hub".
  - [ ] Simulate leaving home still works, and the stove flows are unchanged.

- [ ] **Step 4: Live checks, once the Firebase project and hub exist.** Record these in the PR; don't block on them.
  - [ ] With `enableLogging(true)` from `firebase/database` temporarily on, the app listens only on the six AC child paths and the `temperatureReadings` query, never on `devices/{id}` itself.
  - [ ] Fresh hub on a physical iPhone: Add hub reaches "Hub connected" within 90 s, and `settings.acDeviceId` is `homeguard-xxxxxx`.
  - [ ] A wrong password and then the right one works without resetting the hub.
  - [ ] Teach remote writes `commands/learnRequest`, `learn/status` goes from listening to saved, and `remote` appears.
  - [ ] Leave the geofence → notification → Turn It Off → `commands/result.status = sent` → the app says "Command sent" → the AC turns off. A shut-off calibration starts.
  - [ ] With the hub unplugged, the app shows offline within about 3 min 15 s.

- [ ] **Step 5: Commit**

```bash
git add docs/RUN.md
git commit -m "docs(run): document hub setup, Teach remote and demo hub controls"
```
