# App Hub Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let a user set up the HomeGuard hub and teach it their AC remote from the ac-guard app alone. Turn it off must report honestly ("Command sent"), refuse power-toggle codes without a strong sign the AC is on, and learn the room's warming rate automatically. Every flow keeps working in demo mode.

**Architecture:** Pure logic (hub data parsing, online/offline, setup wait, learn phases, result messages, toggle guard) lives in two dependency-free modules under `src/lib/`, tested with `node --test` the way the existing tests work. `src/services/firebase.ts` gains the new contract paths plus a mock hub. `src/services/ac-monitoring.ts` gains the notification guard and automatic calibration. A new `src/services/turn-off.ts` re-checks the guard at tap time. Bluetooth provisioning sits behind one `HubProvisioner` interface, with a native implementation (`@orbital-systems/react-native-esp-idf-provisioning`) and a mock. Shared `TeachRemoteFlow` and `AddHubFlow` components are mounted by thin onboarding and settings routes.

**Tech Stack:** Expo SDK 55, React Native 0.83 (new architecture only), Expo Router (typed routes), NativeWind 4, Firebase JS SDK v12 Realtime Database, `expo-notifications`, `@orbital-systems/react-native-esp-idf-provisioning@0.7.0` (wraps Espressif `ESPProvision ~> 3.2` on iOS), Node 24 `node:test` + `typescript.transpileModule` for tests.

**Spec:** `~/repos/home-guard-hardware/docs/superpowers/specs/2026-11-06-homeguard-hub-design.md`: sections "App changes (ac-guard)", "1. First-time setup", "2. Teach remote", "Power-toggle remotes", "Learning the room's warming rate as it's used", "Testing without the hardware". **Contract:** `~/repos/home-guard-hardware/docs/firebase-contract.md`. Read both before starting.

## Global Constraints

- Work in `~/repos/ac-guard` on a new branch `feat/hub-integration` cut from `main`. Commit after every task with a conventional commit message. **No `Co-Authored-By` trailer, no "Generated with" line.**
- `ac-guard/AGENTS.md`: read the Expo v55 docs (https://docs.expo.dev/versions/v55.0.0/) before writing code. Use Context7 for current docs on Expo, expo-notifications, expo-router and Firebase v12.
- Firebase paths, exactly as in the contract: `devices/{deviceId}/telemetry`, `remote`, `commands/latest`, `commands/result`, `commands/learnRequest` (app writes `{ requestedAt: serverTimestamp(), requestedBy: uid }`), `learn/status`. The app never writes `remote`, `commands/result` or `learn/status`.
- Bluetooth name `HomeGuard-XXXXXX` (6 uppercase hex). deviceId = name lowercased (`homeguard-a1b2c3`). Proof-of-possession `homeguard-pop-v1`. Transport BLE, security 1 (`ESPSecurity.secure`).
- Hub offline when `telemetry.lastSeenAt` is more than 3 minutes (180 000 ms) old. "Hub connected" needs `lastSeenAt` > the moment setup started, waiting up to 90 s (90 000 ms).
- Toggle guard: applies when `remote.kind` is `toggle` or `raw`. Turn it off is allowed only if status is `likely-on` AND `(outdoor − room) × (1 − e^(−15/τ)) ≥ 0.5 °C`, with τ from `learnedThermalTimeConstant`. No learned τ: no Turn it off for those codes, with an explanation. The guard is checked when deciding to notify and again on every Turn it off tap (dashboard and notification action).
- Copy: "Command sent", never "AC off". Result messages exactly: `sent` "Command sent", `no-code` "Teach your remote first", `unsupported` "The hub can only turn the AC off", `stale` "Hub was offline; command not sent". The feature is called **Teach remote** everywhere (never "calibrate"). Listening copy: "Point your remote at the hub and press OFF."
- Demo mode (no Firebase env values, `getFirebaseMode() === 'mock'`) must run every new flow without hardware.
- Modules in `src/lib/` use `import type` only, so tests can transpile them without dependencies. Tests copy the `load()` helper from `tests/stove-detection.test.cjs` lines 1–23 verbatim (it injects `require` fakes and a fixed `Date`).
- Add hub has to work on iOS. Android must still build. The only new dependency is the provisioning library. Don't touch unrelated code, labels or styling.
- Checks: `node --test tests/*.test.cjs`, `npm run typecheck`, `npm run lint`. After adding route files, run `npx expo start` once (stop it after Metro is ready) so `.expo/types/router.d.ts` picks up the new routes before `npm run typecheck`.

## Review Focus

1. **A result from an earlier attempt shown as this attempt's result.** Example: Try again after a `saved`, and the old `learn/status` still reads `saved`. The app must wait for the status that answers the new request. Pinned in Task 1 (`getLearnPhase` "previous attempt" assertions).
2. **Malformed or unknown hub data.** Examples: a `kind` the app doesn't know, a `status` string outside the contract, string timestamps. Unknown kinds must be treated as `raw` (so the guard applies) and malformed results ignored. Pinned in Task 1 (parser test).
3. **A hub that goes silent.** No Firebase event arrives when telemetry stops, so offline must come from the clock, not from a new snapshot. Boundary pinned in Task 1. The 15 s `now` tick recomputes it in Task 7, checked in the walkthrough.
4. **Problems during setup.** A wrong Wi-Fi password, a 5 GHz-only network, or Bluetooth dropping mid-setup must each show a specific message, and the user can retry because the hub is still in Bluetooth mode. Pinned in Task 1 (`classifyProvisionError`), wired in Task 9.
5. **Turn it off tapped on an old notification after conditions changed** (back home, room now warming). The toggle re-check must block it. Pinned in Task 7 (`requestTurnOff re-evaluates before deciding`).

## Known issues, not in scope

- `subscribeToDevice` and `getDeviceSnapshot` read the whole `devices/{deviceId}` node, which includes `temperatureReadings` (about 1,440 new entries a day from the hub). Every telemetry update re-downloads the full history. Leave it as is. Flag it to Daniel as the first follow-up: listen to `telemetry`, `remote`, `commands` and `learn` separately.
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
  export type CommandResultStatus = 'sent' | 'no-code' | 'unsupported' | 'stale';
  export type CommandResult = { requestedAt: number; status: CommandResultStatus; handledAt: number };
  export type LearnState = 'listening' | 'saved' | 'timeout' | 'rejected';
  export type LearnStatus = { requestedAt: number; state: LearnState; updatedAt: number };
  export type HubCandidate = { name: string; deviceId: string };
  export type WifiNetwork = { ssid: string; rssi: number; secured: boolean };
  // AcDeviceSnapshot gains (AC only, not BaseDeviceSnapshot):
  //   remote: HubRemote | null; commandResult: CommandResult | null;
  //   learnStatus: LearnStatus | null; learnRequestedAt: number | null;
  // AcCalibration gains: source?: 'manual' | 'shut-off' | 'teach-remote';
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
  export function getSetupWaitOutcome(input: { lastSeenAt: number | null; setupStartedAt: number; waitStartedAt: number; now: number }): SetupWaitOutcome;
  export function parseHubRemote(value: unknown): HubRemote | null;
  export function parseCommandResult(value: unknown): CommandResult | null;
  export function parseLearnStatus(value: unknown): LearnStatus | null;
  export const COMMAND_RESULT_MESSAGES: Record<CommandResultStatus, string>;
  export function describeLastCommand(command: DeviceCommand | null, result: CommandResult | null): string;
  export type LearnPhase = 'waiting' | 'listening' | 'saved' | 'timeout' | 'rejected' | 'no-response';
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

- [ ] **Step 2: Write the failing tests** in `tests/hub.test.cjs` (`const hub = load('src/lib/hub.ts');`, `NOW = 1_800_000_000_000`, `MINUTE = 60_000`)

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

test('setup waits for a lastSeenAt newer than setup start, up to 90 s', () => {
  const start = NOW;
  const wait = (lastSeenAt, now, waitStartedAt = start) => hub.getSetupWaitOutcome({ lastSeenAt, setupStartedAt: start, waitStartedAt, now });
  assert.equal(wait(start, start + 5_000), 'waiting');
  assert.equal(wait(start - 3_600_000, start + 5_000), 'waiting'); // re-setup hub's old lastSeenAt
  assert.equal(wait(start + 1, start + 5_000), 'connected');
  assert.equal(wait(null, start + 90_000), 'waiting');
  assert.equal(wait(null, start + 90_001), 'timed-out');
  assert.equal(wait(start + 1, start + 200_000), 'connected');
  assert.equal(wait(null, start + 120_000, start + 100_000), 'waiting'); // "Keep waiting"
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
  assert.deepEqual({ ...hub.COMMAND_RESULT_MESSAGES }, { sent: 'Command sent', 'no-code': 'Teach your remote first', unsupported: 'The hub can only turn the AC off', stale: 'Hub was offline; command not sent' });
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
  for (const state of ['saved', 'timeout', 'rejected']) assert.equal(phase({ status: s(100, state), now: NOW + 120_000 }), state);
  assert.equal(phase({ currentRequest: 50, status: s(50, 'saved') }), 'waiting'); // previous attempt, new request not seen yet
  assert.equal(phase({ status: s(50, 'saved') }), 'waiting');                     // previous attempt's status
  assert.equal(phase({ now: NOW + 15_001 }), 'no-response');
  assert.equal(phase({ status: s(100, 'listening'), now: NOW + 45_000 }), 'listening');
  assert.equal(phase({ status: s(100, 'listening'), now: NOW + 45_001 }), 'no-response');
  assert.equal(hub.LEARN_PHASE_MESSAGES.listening, 'Point your remote at the hub and press OFF.');
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

Rules the tests don't fully fix:
- `deviceIdFromHubName`: `/^HomeGuard-[0-9A-F]{6}$/`, then lowercase.
- `getHubConnectionStatus`: online iff `lastSeenAt > 0 && now - lastSeenAt <= HUB_OFFLINE_AFTER_MS`.
- `getSetupWaitOutcome`: `connected` if `lastSeenAt !== null && lastSeenAt > setupStartedAt` (checked first). Otherwise `timed-out` if `now - waitStartedAt > HUB_SETUP_WAIT_MS`, else `waiting`.
- Parsers need finite numbers for every timestamp field (`learnedAt` may be missing, which gives `null`). `parseHubRemote` with `learned !== true` returns the all-null shape.
- `getLearnPhase`: a status counts only if `currentRequest !== null && currentRequest !== requestBefore && status.requestedAt === currentRequest`. Final states win at any time. A matched `listening` turns into `no-response` after `LEARN_LISTEN_TIMEOUT_MS`. With no matched status it is `waiting`, then `no-response` after `LEARN_ACK_TIMEOUT_MS` (both measured from `attemptStartedAt`).
- `classifyProvisionError`: read `code` and `message` from an object error. `/authentication error|AUTH_FAILED/i` gives wrong-password, `/network not found|NETWORK_NOT_FOUND/i` gives network-not-found, and `code === 'E_DEVICE_DISCONNECTED'` or `/disconnected/i` gives disconnected.
- Copy:
  - `LEARN_PHASE_MESSAGES`:
    - waiting: "Waiting for the hub…"
    - listening: as tested
    - saved: "Remote saved."
    - timeout: "We didn't hear your remote. Try again."
    - rejected: "The signal was too short or unclear. Try again."
    - no-response: "The hub didn't answer. Check it's plugged in and online, then try again."
  - `PROVISION_FAILURE_MESSAGES`:
    - wrong-password: "That Wi-Fi password didn't work. Check it and try again."
    - network-not-found: "The hub couldn't find that network. It only works with 2.4 GHz Wi-Fi."
    - disconnected: "Lost the Bluetooth connection to the hub. Move closer and try again."
    - unknown: "Setup didn't finish. Try again."

Add the new snapshot and state fields to the types now. `npm run typecheck` will fail where `AcDeviceSnapshot` literals lack them (`src/constants/demo.ts`, `src/services/firebase.ts`). Task 3 fixes those, so only `node --test` gates this task.

- [ ] **Step 5: Run to verify pass**

Run: `node --test tests/*.test.cjs`
Expected: all tests pass, including the existing 21.

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
  export function predictedOffRoomRise(outdoorCelsius: number, roomCelsius: number, tauMinutes: number): number;
  export function getTurnOffAvailability(input: TurnOffInput): TurnOffAvailability;
  ```

- [ ] **Step 1: Write the failing tests** (`const guard = load('src/lib/turn-off-guard.ts');`)

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
```

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/turn-off-guard.test.cjs`
Expected: FAIL (module missing).

- [ ] **Step 3: Implement `src/lib/turn-off-guard.ts`**

Check order: teach-remote (remote null or not learned), then `ac-state` is available, then not-likely-on, then no-learned-tau (τ not finite or ≤ 0), then missing-readings, then gap-too-small (`predictedOffRoomRise < 0.5`). The rise is `(outdoor − room) × (1 − Math.exp(−15 / τ))`.

Copy:
- `TEACH_REMOTE_MESSAGE`: "Teach your remote first so the hub can turn the AC off."
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

### Task 3: Firebase contract paths and mock hub

**Files:**
- Modify: `src/services/firebase.ts`
- Modify: `src/constants/demo.ts`

**Interfaces:**
- Consumes: Task 1 parsers and `getHubConnectionStatus`.
- Produces (`src/services/firebase.ts`):
  ```ts
  export async function requestRemoteLearning(deviceId: string): Promise<void>;
  export function setMockRemote(kind: RemoteKind | null): void;            // null = not taught
  export function setMockLearnOutcome(outcome: Exclude<LearnState, 'listening'>): void;
  export function setMockHubOnline(online: boolean): void;
  export function markMockHubProvisioned(deviceId: string): void;
  // unchanged signatures: sendDeviceCommand, sendTurnOffCommand, getDeviceSnapshot, subscribeToDevice
  ```

- [ ] **Step 1: Live parsing.** Extend `FirebaseDeviceValue` with `remote?: unknown`, `commands?: { latest?: Partial<DeviceCommand>; result?: unknown; learnRequest?: { requestedAt?: unknown } }`, `learn?: { status?: unknown }`. In `parseAcSnapshot` set:
  - `remote: parseHubRemote(value?.remote)`
  - `commandResult: parseCommandResult(value?.commands?.result)`
  - `learnStatus: parseLearnStatus(value?.learn?.status)`
  - `learnRequestedAt`: finite number or `null`
  - `connectionStatus`: `'offline'` when `!value || value.connectionStatus === 'offline' || value.connected === false`, else `getHubConnectionStatus(lastSeenAtParsed, Date.now())`

- [ ] **Step 2: Live learn request.** `requestRemoteLearning(deviceId)` does `set(ref(db, \`devices/${deviceId}/commands/learnRequest\`), { requestedAt: serverTimestamp(), requestedBy: uid })`. Don't read the value back: the screen matches `learn/status.requestedAt` against the snapshot's `learnRequestedAt`.

- [ ] **Step 3: Mock hub.** Module constants: `MOCK_RESULT_DELAY_MS = 800`, `MOCK_LISTEN_DELAY_MS = 600`, `MOCK_LEARN_DELAY_MS = 4_000`, `MOCK_HUB_BOOT_DELAY_MS = 3_000`. Mock state: `mockLearnOutcome = 'saved'`, `mockTeachKind: RemoteKind = 'ac-state'`. Every change notifies `mockAcListeners`, as `setMockTemperature` already does.
  - `sendDeviceCommand` (mock branch): **stop setting `powerState`/`powerStateUpdatedAt`**, since a command never proves the AC is off. Set `lastCommand`. If the mock hub is online, after `MOCK_RESULT_DELAY_MS` set `commandResult = { requestedAt: command.requestedAt, status, handledAt: Date.now() }`. `status` is `'unsupported'` for `ON`, `'no-code'` if `remote?.learned !== true`, else `'sent'`. If the mock hub is offline, write no result.
  - `requestRemoteLearning` (mock): `learnRequestedAt = Date.now()`. After `MOCK_LISTEN_DELAY_MS`, set `learnStatus` to `listening`. After `MOCK_LEARN_DELAY_MS`, set it to `mockLearnOutcome`. On `saved`, also set `remote = { learned: true, learnedAt: Date.now(), kind: mockTeachKind, protocol: mockTeachKind === 'ac-state' ? 'DAIKIN' : 'UNKNOWN' }`.
  - `setMockRemote(kind)`: `null` sets `{ learned: false, learnedAt: null, kind: null, protocol: null }`. Otherwise set a learned remote of that kind and remember `mockTeachKind = kind`.
  - `setMockHubOnline(online)`: sets `connectionStatus` and `lastSeenAt = online ? Date.now() : Date.now() - 4 * 60_000`.
  - `markMockHubProvisioned(deviceId)`: after `MOCK_HUB_BOOT_DELAY_MS`, sets `deviceId`, `lastSeenAt = Date.now()`, `connectionStatus = 'online'` and an untaught remote.

- [ ] **Step 4: Demo constants.** In `DEMO_AC_SNAPSHOT` add `remote: { learned: true, learnedAt: Date.now() - 7 * 24 * 60 * 60_000, kind: 'ac-state', protocol: 'DAIKIN' }`, `commandResult: null`, `learnStatus: null`, `learnRequestedAt: null`. Set `lastCommand: null`: the old demo `ON` command has no result and would show "Waiting for the hub". In `emptyAcMonitoringState()` (`src/services/ac-monitoring.ts`) add `lastCalibratedCommandAt: null`.

- [ ] **Step 5: Verify**

Run: `npm run typecheck && npm run lint && node --test tests/*.test.cjs`
Expected: no errors, all tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/services/firebase.ts src/constants/demo.ts src/services/ac-monitoring.ts
git commit -m "feat(firebase): read hub remote, results and learn status; add mock hub"
```

---

### Task 4: Guard the AC notification

**Files:**
- Modify: `src/services/ac-monitoring.ts:287-295` (the `likely-on` notification block)
- Modify: `src/services/notifications.ts` (`ScheduleAcReminderOptions`, `scheduleAcReminder`)
- Test: `tests/ac-monitoring-hub.test.cjs`

**Interfaces:**
- Consumes: `getTurnOffAvailability`, `TurnOffAvailability` (Task 2); snapshot `remote` (Task 3).
- Produces: `ScheduleAcReminderOptions` gains `turnOff?: TurnOffAvailability` (default `{ state: 'available' }`). `sendAcReminderNow` passes it through.

- [ ] **Step 1: Write the failing tests.** Fixture `monitoring({ remote, tau, outdoorCelsius, state, settingsPatch, snapshotPatch })` loads `src/services/ac-monitoring.ts` with these fakes:
  - `@react-native-async-storage/async-storage`: an in-memory `{ getItem, setItem }` seeded under `'home_guard.ac_monitoring.v1'`.
  - `@/services/firebase`: `{ getDeviceSnapshot, getRoomTemperatureReadings: async () => [] }`.
  - `@/services/notifications`: `{ cancelAcReminders: async () => {}, sendAcReminderNow: async (o) => { reminders.push(o); return 'id'; } }`.
  - `@/services/outdoor-temperature`: `{ getNearbyOutdoorTemperature: async () => outdoor }`.
  - `@/services/settings`: `{ loadSettings: async () => settings }`.
  - `@/lib/turn-off-guard`: `load('src/lib/turn-off-guard.ts')`.

  Defaults:
  - settings: `{ ...DEFAULT_SETTINGS, setupComplete: true, acDeviceId: 'homeguard-a1b2c3', phoneDepartedAt: NOW - 25 * MINUTE }`
  - snapshot: room 22, `lastSeenAt: NOW - MINUTE`, `connectionStatus: 'online'`, `powerState: 'ON'`, `powerStateUpdatedAt: NOW - MINUTE` (fresh power ON is the shortest path to `likely-on`)
  - stored state: a departure at `NOW - 25 * MINUTE` (`alertSent: false`, `temperatureAtDeparture: 22`, threshold 26), `thermalDeviceId` set, `thermalTimeConstantsMinutes: tau`
  - outdoor: `{ temperatureCelsius: outdoorCelsius ?? 30, timestamp: NOW - 5 * MINUTE, stationName: 'Test' }`

```js
const toggle = { learned: true, learnedAt: 1, kind: 'toggle', protocol: 'NEC' };
test('toggle remote without a learned warming rate notifies without Turn it off', async () => {
  const m = monitoring({ remote: toggle, tau: [] });
  await m.evaluateAcMonitoring();
  assert.equal(m.reminders.length, 1);
  assert.equal(m.reminders[0].turnOff.state, 'blocked');
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

- [ ] **Step 3: Implement.** In the notification block, compute `getTurnOffAvailability({ remote: snapshot.remote, status: state.status, outdoorCelsius: outdoor?.temperatureCelsius ?? null, roomCelsius: snapshot.roomTemperatureCelsius, tauMinutes: learnedThermalTimeConstant(state) })` and pass it as `turnOff` to `sendAcReminderNow`.

  In `scheduleAcReminder`, when `turnOff.state !== 'available'`, leave out `categoryIdentifier` (so the notification has no Turn It Off / Keep It On buttons) and append `' ' + turnOff.message` to the existing body. Keep the titles and the `data` payload, so `cancelAcReminders` still finds it.

- [ ] **Step 4: Run to verify pass**

Run: `node --test tests/*.test.cjs && npm run typecheck`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/services/ac-monitoring.ts src/services/notifications.ts tests/ac-monitoring-hub.test.cjs
git commit -m "feat(ac): apply power-toggle guard to the AC notification"
```

---

### Task 5: Automatic calibration after shut-off and Teach remote

**Files:**
- Modify: `src/services/ac-monitoring.ts` (`observeCalibration`, `beginAcDeparture`, `startAcCalibration`, new exports)
- Modify: `src/state/app-provider.tsx` (device subscription effect)
- Test: `tests/ac-monitoring-hub.test.cjs` (append)

**Interfaces:**
- Consumes: snapshot `lastCommand`, `commandResult` (Task 3).
- Produces (`src/services/ac-monitoring.ts`):
  ```ts
  export const AUTO_CALIBRATION_MAX_RESULT_AGE_MS = 10 * 60_000;
  export function startAutomaticCalibration(source: 'teach-remote'): Promise<AcMonitoringState>;
  export function calibrateAfterCommandResult(snapshot: AcDeviceSnapshot): Promise<AcMonitoringState>;
  ```

- [ ] **Step 1: Write the failing tests** (same fixture; `departure` optional; `lastCommand`/`commandResult` set through `snapshotPatch`). Use whole-degree temperatures at the 1 °C boundary, because `23.4 - 22.4` in floating point is below 1.

```js
const off = (requestedAt, status = 'sent', handledAt = requestedAt + 1_000) => ({
  lastCommand: { value: 'OFF', requestedAt, source: 'notification' },
  commandResult: { requestedAt, status, handledAt },
});
test('a matching sent result starts a silent shut-off calibration without wiping readings', async () => {
  const m = monitoring({ remote: toggle, outdoorCelsius: 30, snapshotPatch: { ...off(NOW - 30_000), powerState: null }, stateReadings: 3 });
  const state = await m.calibrateAfterCommandResult(m.snapshot);
  assert.equal(state.calibration.source, 'shut-off');
  assert.equal(state.calibration.startedAt, NOW - 29_000);   // handledAt
  assert.equal(state.calibration.temperatureAtStart, 22);
  assert.equal(state.calibration.outdoorAtStart.temperatureCelsius, 30);
  assert.ok(state.readings.length >= 3);
  assert.equal(state.lastCalibratedCommandAt, NOW - 30_000);
  assert.equal(state.calibrationMessage, '');
});
test('each result is handled once, even when it could not start', async () => {
  const m = monitoring({ outdoorCelsius: 22.5, snapshotPatch: { ...off(NOW - 30_000), powerState: null } });
  await m.calibrateAfterCommandResult(m.snapshot);
  m.setOutdoor(30);
  assert.equal((await m.calibrateAfterCommandResult(m.snapshot)).calibration, null);
});
test('non-sent, unmatched or old results start nothing', async () => {
  for (const patch of [off(NOW - 30_000, 'no-code'), { ...off(NOW - 30_000), commandResult: { requestedAt: NOW - 90_000, status: 'sent', handledAt: NOW - 89_000 } }, off(NOW - 11 * MINUTE)]) {
    const m = monitoring({ snapshotPatch: { ...patch, powerState: null } });
    assert.equal((await m.calibrateAfterCommandResult(m.snapshot)).calibration, null);
  }
});
test('a running calibration is never replaced', async () => {
  const m = monitoring({ calibration: { source: 'manual', startedAt: NOW - 5 * MINUTE }, snapshotPatch: { ...off(NOW - 30_000), powerState: null } });
  assert.equal((await m.calibrateAfterCommandResult(m.snapshot)).calibration.source, 'manual');
});
test('teach-remote calibration needs outdoor at least 1 °C warmer than the room', async () => {
  assert.equal((await monitoring({ departure: false, outdoorCelsius: 22.9, snapshotPatch: { powerState: null } }).startAutomaticCalibration('teach-remote')).calibration, null);
  assert.equal((await monitoring({ departure: false, outdoorCelsius: 23, snapshotPatch: { powerState: null } }).startAutomaticCalibration('teach-remote')).calibration.source, 'teach-remote');
});
test('automatic calibrations give up silently; manual ones keep their message', async () => {
  const auto = monitoring({ departure: false, calibration: { source: 'shut-off', startedAt: NOW - 121 * MINUTE }, settingsPatch: { phoneDepartedAt: null } });
  assert.equal((await auto.evaluateAcMonitoring()).calibrationMessage, '');
  const manual = monitoring({ departure: false, calibration: { source: 'manual', startedAt: NOW - 121 * MINUTE }, settingsPatch: { phoneDepartedAt: null } });
  assert.match((await manual.evaluateAcMonitoring()).calibrationMessage, /^Calibration stopped/);
});
test('leaving home keeps an automatic calibration but stops a manual one', async () => {
  const auto = monitoring({ departure: false, calibration: { source: 'teach-remote', startedAt: NOW - 5 * MINUTE }, settingsPatch: { phoneDepartedAt: NOW } });
  assert.equal((await auto.beginAcDeparture(auto.settings)).calibration.source, 'teach-remote');
  const manual = monitoring({ departure: false, calibration: { source: 'manual', startedAt: NOW - 5 * MINUTE }, settingsPatch: { phoneDepartedAt: NOW } });
  assert.equal((await manual.beginAcDeparture(manual.settings)).calibration, null);
});
```

  Fixture helpers this needs: `stateReadings` (n readings one minute apart before NOW), `calibration` (partial, completed with `deviceId`, `temperatureAtStart: 22`, `outdoorAtStart`), `m.setOutdoor(c)`, `m.snapshot`, `m.settings`.

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/ac-monitoring-hub.test.cjs`
Expected: the new tests FAIL.

- [ ] **Step 3: Implement.**
  - Write one **non-serialized** helper, `beginAutomaticCalibration(state, settings, snapshot, outdoor, source, startedAt): void`, and call it from both serialized exports. Calling `serialize` inside `serialize` deadlocks the queue.
  - The helper does nothing if any of these hold: `state.calibration` is set, `snapshot.deviceId !== settings.acDeviceId`, `!freshTemperature(snapshot, now)`, `freshPowerState(snapshot, now) === 'ON'`, or `!outdoor || outdoor.temperatureCelsius - snapshot.roomTemperatureCelsius < 1`.
  - Otherwise it applies the same `thermalDeviceId` reset as `startAcCalibration` and **merges** the reading with `mergeReadings` (no reset). It sets `state.outdoor` and `state.calibration = { …, source }` and leaves `calibrationMessage` untouched.
  - `calibrateAfterCommandResult`: proceed only if `commandResult.status === 'sent'`, `commandResult.requestedAt === lastCommand?.requestedAt`, and that value `!== state.lastCalibratedCommandAt`. Set `lastCalibratedCommandAt` whenever those match. Start with `startedAt = commandResult.handledAt`, but only if `Date.now() - handledAt <= AUTO_CALIBRATION_MAX_RESULT_AGE_MS`. Fetch outdoor with `getNearbyOutdoorTemperature(settings.homeLocation)` and save.
  - `startAutomaticCalibration('teach-remote')`: load settings, snapshot and outdoor as `startAcCalibration` does, with `startedAt = Date.now()`. Don't throw; return the state unchanged when it can't start.
  - `startAcCalibration` sets `source: 'manual'`.
  - `observeCalibration`: for `shut-off`/`teach-remote`, the stop branch clears `calibration` without touching `calibrationMessage`. On success with `shut-off`, prefix the existing success message with "The room is warming, so the AC is likely off now. ".
  - `beginAcDeparture`: stop the calibration (with its existing message) only when `source` is `'manual'` or missing.
  - In `app-provider.tsx`'s `subscribeToDevice` effect, next to `recordAcReading`, call `void calibrateAfterCommandResult(snapshot).then((state) => { if (state.calibration) void startAcBackgroundMonitoring().catch(() => undefined); })`.

- [ ] **Step 4: Run to verify pass**

Run: `node --test tests/*.test.cjs && npm run typecheck && npm run lint`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add src/services/ac-monitoring.ts src/state/app-provider.tsx tests/ac-monitoring-hub.test.cjs
git commit -m "feat(ac): learn room warming automatically after shut-off and Teach remote"
```

---

### Task 6: Teach remote screen

**Files:**
- Create: `src/components/teach-remote-flow.tsx`
- Create: `src/components/hub-card.tsx`
- Create: `src/app/(tabs)/settings/teach.tsx`, `src/app/onboarding/teach.tsx`
- Modify: `src/app/(tabs)/settings/ac.tsx` (render `<HubCard />` above `<AcSettings />`)
- Modify: `src/state/app-provider.tsx`, `src/app/(tabs)/settings/developer.tsx`

**Interfaces:**
- Consumes: `requestRemoteLearning`, `sendTurnOffCommand`, `setMockLearnOutcome` (Task 3); `getLearnPhase`, `LEARN_PHASE_MESSAGES`, `describeLastCommand` (Task 1); `startAutomaticCalibration` (Task 5).
- Produces (context additions in `AppContextValue`):
  ```ts
  acDevice: AcDeviceSnapshot;
  startRemoteLearning: () => Promise<void>;   // requestRemoteLearning(settings.acDeviceId)
  testRemote: () => Promise<void>;            // sendTurnOffCommand(settings.acDeviceId, 'app'); no guard (user just turned the AC on)
  finishRemoteTest: () => Promise<void>;      // startAutomaticCalibration('teach-remote'), then startAcBackgroundMonitoring() if calibration started
  setDemoLearnOutcome: (outcome: 'saved' | 'timeout' | 'rejected') => void;
  ```
  `export function TeachRemoteFlow(props: { onDone: () => void; onSkip?: () => void }): JSX.Element`, `export function HubCard(): JSX.Element`.

- [ ] **Step 1: Context.** Add the members above to `app-provider.tsx`, with `acDevice: device`.

- [ ] **Step 2: `TeachRemoteFlow`.** These steps follow spec "2. Teach remote" (layout and other copy are free, within the existing design system):
  1. **Prepare:** "Turn your AC on with its remote, then tap Start." Button **Start**.
  2. **Start:** record `requestBefore = acDevice.learnRequestedAt` and `attemptStartedAt = Date.now()`, then call `startRemoteLearning()`. Tick `now` every 1 s and render `LEARN_PHASE_MESSAGES[getLearnPhase(...)]` with `currentRequest = acDevice.learnRequestedAt` and `status = acDevice.learnStatus`. While listening, also show "Your AC will probably turn off too. That's expected."
  3. **`timeout` / `rejected` / `no-response`:** show **Try again**, which goes back to Prepare.
  4. **`saved`:** "Turn your AC back on with its remote, then test it." Buttons **Test it** and **Skip test** (Skip calls `onDone`).
  5. **Test it:** record `commandBefore = acDevice.lastCommand?.requestedAt ?? null`, then `await testRemote()`. Show `describeLastCommand(...)` once `acDevice.lastCommand.requestedAt !== commandBefore`. On a matched `sent`, ask "Did your AC turn off?" **Yes** runs `await finishRemoteTest()` then `onDone()`. **No** goes back to Prepare. On any other matched status, show its message and **Try again**.
  6. Disable Start when `connectionStatus === 'offline'` and say "The hub is offline."

- [ ] **Step 3: Routes.**
  - `settings/teach.tsx`: wrap the flow in `SettingsPage` (title "Teach remote"), with `onDone={() => router.back()}`.
  - `onboarding/teach.tsx`: an onboarding shell like `onboarding/reminder.tsx`'s header, with `onDone` and `onSkip` both `router.push('/onboarding/reminder')`.

- [ ] **Step 4: `HubCard`** (Settings → AC). It shows:
  - `settings.acDeviceId` and the online/offline status
  - the remote state: "Remote taught · {protocol}" or "Remote not taught"
  - a button **Teach remote** that runs `router.push('/settings/teach')`

  Task 9 adds Add hub here.

- [ ] **Step 5: Demo lab.** In `developer.tsx`, add a "Hub" block with a `SegmentedControl` "Next teach result" (Saved / Timeout / Rejected) that calls `setDemoLearnOutcome`.

- [ ] **Step 6: Verify**

Run: `npx expo start` (stop when Metro is ready), then `npm run typecheck && npm run lint && node --test tests/*.test.cjs`
Expected: no errors.

Demo check (`npm run ios`, Explore the demo): Settings → AC monitoring → Teach remote. Start shows "Waiting for the hub…", then the listening copy, then the saved step. Test it shows "Command sent", then the question. Yes returns to Settings. Repeat with Timeout and Rejected in Demo lab, and check that Try again works.

- [ ] **Step 7: Commit**

```bash
git add src/components/teach-remote-flow.tsx src/components/hub-card.tsx "src/app/(tabs)/settings/teach.tsx" src/app/onboarding/teach.tsx "src/app/(tabs)/settings/ac.tsx" "src/app/(tabs)/settings/developer.tsx" src/state/app-provider.tsx
git commit -m "feat(hub): add Teach remote flow and hub card"
```

---

### Task 7: Honest Turn it off: re-check, result messages, offline status

**Files:**
- Create: `src/services/turn-off.ts`
- Modify: `src/services/notifications.ts` (`processNotificationResponse`: stop sending; drop the `sendTurnOffCommand` import)
- Modify: `src/state/app-provider.tsx`, `src/app/(tabs)/dashboard.tsx`, `src/app/(tabs)/settings/developer.tsx`
- Test: `tests/turn-off.test.cjs`

**Interfaces:**
- Consumes: `evaluateAcMonitoring`, `learnedThermalTimeConstant`, `getDeviceSnapshot`, `sendTurnOffCommand`, `loadSettings`, `getTurnOffAvailability`, `describeLastCommand`, `getHubConnectionStatus`, `setMockRemote`, `setMockHubOnline`.
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
  connectionStatus: ConnectionStatus;              // live: device.connectionStatus === 'offline' ? 'offline' : getHubConnectionStatus(device.lastSeenAt, now); mock: device.connectionStatus
  setDemoRemote: (kind: RemoteKind | null) => void;
  setDemoHubOnline: (online: boolean) => void;
  ```

- [ ] **Step 1: Write the failing tests** (`load('src/services/turn-off.ts', { '@/services/ac-monitoring': { evaluateAcMonitoring, learnedThermalTimeConstant: (s) => s.tau ?? null }, '@/services/firebase': { getDeviceSnapshot, sendTurnOffCommand }, '@/services/settings': { loadSettings }, '@/lib/turn-off-guard': load('src/lib/turn-off-guard.ts') })`)

```js
const toggle = { learned: true, learnedAt: 1, kind: 'toggle', protocol: 'NEC' };
const acState = { ...toggle, kind: 'ac-state', protocol: 'DAIKIN' };
test('requestTurnOff sends for an ac-state remote even at home', async () => {
  const t = turnOff({ remote: acState, state: { status: 'home', tau: null } });
  const outcome = await t.requestTurnOff('app');
  assert.equal(outcome.sent, true);
  assert.deepEqual(t.sent, [['homeguard-a1b2c3', 'app']]);
});
test('requestTurnOff refuses a toggle remote without a learned warming rate', async () => {
  const t = turnOff({ remote: toggle, state: { status: 'likely-on', tau: null, outdoor: 30 } });
  const outcome = await t.requestTurnOff('notification');
  assert.equal(outcome.sent, false);
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

  The `turnOff(...)` fixture's fake `evaluateAcMonitoring` counts calls and returns a state with `status` (after `afterEvaluate`), `outdoor: { temperatureCelsius }`, and `tau`. The fake snapshot has room 22 and the given remote.

- [ ] **Step 2: Run to verify failure**

Run: `node --test tests/turn-off.test.cjs`
Expected: FAIL (module missing).

- [ ] **Step 3: Implement `requestTurnOff`.** It runs `loadSettings()`, then `state = await evaluateAcMonitoring()`, then `getDeviceSnapshot(settings.acDeviceId)`. It builds the availability from `state.status`, `state.outdoor?.temperatureCelsius ?? null`, `snapshot.roomTemperatureCelsius` and `learnedThermalTimeConstant(state)`. It calls `sendTurnOffCommand(settings.acDeviceId, source)` only when the result is `available`.

- [ ] **Step 4: Notification action.** `processNotificationResponse` returns `{ action: 'turn_off', deviceId, deviceType }` without sending anything (this avoids a `notifications → turn-off → ac-monitoring → notifications` import cycle). In `app-provider.tsx`, handle `turn_off` from both `addNotificationActionListener` and `processLastNotificationResponse()` the same way. Call `requestTurnOff('notification')`, and when `!outcome.sent`, run `Alert.alert('Command not sent', outcome.availability.message)`.

- [ ] **Step 5: Dashboard.**
  - When `turnOffAvailability.state === 'teach-remote'`, replace the Turn off AC button with a **Teach remote** button (`router.push('/settings/teach')`) and show `turnOffAvailability.message`.
  - When `blocked`, disable the button and show the message under it.
  - After a press that returns `{ sent: false }`, show that message inline.
  - The "Last command" card shows `lastCommandLabel`.
  - The sensor row uses the new `connectionStatus`, so in live mode it goes offline 3 minutes after the last telemetry, recomputed on the existing 15 s `now` tick.
  - Leave the other labels unchanged.

- [ ] **Step 6: Demo lab.** In the "Hub" block, add a `SegmentedControl` "Remote" (Not taught / AC state / Toggle / Raw) that calls `setDemoRemote`, and a `Switch` "Hub online" that calls `setDemoHubOnline`. Make `sendTestReminder` pass `turnOff: getTurnOffAvailability({ remote: device.remote, status: 'likely-on', outdoorCelsius: acMonitoring.outdoor?.temperatureCelsius ?? null, roomCelsius: device.roomTemperatureCelsius, tauMinutes: learnedThermalTimeConstant(acMonitoring) })`.

- [ ] **Step 7: Verify**

Run: `node --test tests/*.test.cjs && npm run typecheck && npm run lint`
Expected: all pass.

Demo check:
- AC state remote: Turn off AC shows "Waiting for the hub", then "Command sent". The AC likelihood card never changes to "Off".
- Not taught: a Teach remote button appears. A command sent anyway from the notification ends in "Teach your remote first".
- Toggle: the button is disabled with the not-likely-on message. "Send AC alert now" arrives with no action buttons and the no-learned-tau text.
- Hub online off: the row says offline, and Turn off AC stays at "Waiting for the hub".

- [ ] **Step 8: Commit**

```bash
git add src/services/turn-off.ts src/services/notifications.ts src/state/app-provider.tsx "src/app/(tabs)/dashboard.tsx" "src/app/(tabs)/settings/developer.tsx" tests/turn-off.test.cjs
git commit -m "feat(ac): re-check toggle guard on Turn it off and show honest command results"
```

---

### Task 8: Bluetooth provisioning library behind one interface

**Choice:** `@orbital-systems/react-native-esp-idf-provisioning@0.7.0` (published 2026-10-27, MIT, maintained by Orbital Systems).
- It is a codegen TurboModule (`codegenConfig` with `TurboModuleRegistry.getEnforcing`), so it works with the new architecture only. Its example app builds on RN 0.84.
- It ships an Expo config plugin (`app.plugin.js`).
- On iOS it depends on Espressif's `ESPProvision ~> 3.2`, and supports BLE with `ESPSecurity.secure` and a PoP.
- It is not in Expo's bundled module list, so Step 4's build gate proves it works on SDK 55. If the gate fails, do Task 8b instead of Step 5.

**Files:**
- Modify: `package.json`, `package-lock.json`, `app.json`
- Create: `src/services/hub-provisioning/types.ts`, `src/services/hub-provisioning/esp-provisioner.ts`, `src/services/hub-provisioning/mock-provisioner.ts`, `src/services/hub-provisioning/index.ts`

**Interfaces:**
- Consumes: `HUB_NAME_PREFIX`, `HUB_PROOF_OF_POSSESSION`, `deviceIdFromHubName`, `classifyProvisionError`, `tidyWifiNetworks`, `ProvisionFailure` (Task 1); `markMockHubProvisioned`, `getFirebaseMode` (Task 3).
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

- [ ] **Step 2: Config plugin.** In the `app.json` `plugins` array, add:

```json
["@orbital-systems/react-native-esp-idf-provisioning", {
  "transport": "ble",
  "bluetoothAlwaysPermission": "Home Guard uses Bluetooth to set up your HomeGuard hub."
}]
```

`transport: "ble"` is required. The default `"both"` overwrites `NSLocationWhenInUseUsageDescription` with the library's text. Do **not** set `neverForLocation`: it would add `maxSdkVersion="30"` to the `ACCESS_FINE_LOCATION` entry that `expo-location` needs.

- [ ] **Step 3: Implement the provisioners.**
  - `esp-provisioner.ts` keeps the current `ESPDevice` in a closure.
    - `scanForHubs`: `ESPProvisionManager.searchESPDevices(HUB_NAME_PREFIX, ESPTransport.ble, ESPSecurity.secure)`. Map each device to `{ name, deviceId: deviceIdFromHubName(name) }` and drop nulls. An error whose message matches `/not found/i` returns `[]`.
    - `connect`: `device.connect(HUB_PROOF_OF_POSSESSION, null, null)`.
    - `listWifiNetworks`: `tidyWifiNetworks(list.map(n => ({ ssid: n.ssid, rssi: n.rssi, secured: n.auth !== ESPWifiAuthMode.open })))`.
    - `sendWifiCredentials`: `device.provision(ssid, password)`. On rejection, throw `new HubProvisioningError(classifyProvisionError(e))`.
  - `mock-provisioner.ts`:
    - scan: after 1 200 ms, returns `[{ name: 'HomeGuard-A1B2C3', deviceId: 'homeguard-a1b2c3' }]`.
    - connect: 800 ms.
    - Wi-Fi list: `HomeGuard Demo Wi-Fi` (−48, secured), `Neighbour 2.4G` (−71, secured), `Cafe Guest` (−80, open).
    - Password `wrong`: throws `HubProvisioningError('wrong-password')`. Any other password: waits 1 500 ms, then calls `markMockHubProvisioned(deviceId)`.

- [ ] **Step 4: Build gate**

```bash
npx expo prebuild --platform ios
plutil -p ios/*/Info.plist | grep -E "NSBluetoothAlwaysUsageDescription|NSLocationWhenInUseUsageDescription"
npm run ios
npx expo prebuild --platform android && (cd android && ./gradlew assembleDebug)
grep -n "ACCESS_FINE_LOCATION" android/app/src/main/AndroidManifest.xml
```

Expected:
- Bluetooth string present, and the location string still reads "Home Guard uses your location to set your home radius."
- `pod install` pulls `ESPProvision (3.2.x)`, and the app builds and launches in the simulator.
- Gradle reports `BUILD SUCCESSFUL`.
- `ACCESS_FINE_LOCATION` has no `maxSdkVersion`.

If `ANDROID_HOME` is unset, report the Android build as unverified; don't skip it silently. `ios/` and `android/` are gitignored, so commit neither.

- [ ] **Step 5: Verify and commit** (gate passed)

Run: `npm run typecheck && npm run lint && node --test tests/*.test.cjs`

```bash
git add package.json package-lock.json app.json src/services/hub-provisioning
git commit -m "feat(hub): add BLE provisioning behind a HubProvisioner interface"
```

### Task 8b (only if Task 8's gate fails): local Expo module fallback

Use this if the wrapper fails to build or link on SDK 55 / RN 0.83. If the failure is inside the `ESPProvision` pod itself, stop and report instead: this fallback uses the same pod.

1. `npm uninstall @orbital-systems/react-native-esp-idf-provisioning` and remove its `app.json` entry.
2. `npx create-expo-module@latest --local homeguard-provisioning`, which creates `modules/homeguard-provisioning/`. In `expo-module.config.json`, set `"platforms": ["apple"]` so Android builds without native code.
3. In the module's podspec, add `s.dependency 'ESPProvision', '~> 3.2'`.
4. In Swift (Expo Modules API), write `AsyncFunction`s: `searchHubs(prefix) -> [String]`, `connect(name, pop)`, `scanWifi() -> [[String: Any]]` (ssid, rssi, auth) and `provision(ssid, passphrase)`. Each rejects with `ESPProvisionError.description` as the message, so `classifyProvisionError` still works. Also write `disconnect()`. Use `ESPProvisionManager.shared.searchESPDevices(devicePrefix:transport:.ble, security:.secure)` and `ESPDevice.connect` with a delegate that returns the PoP.
5. Add a config plugin, `plugins/with-hub-provisioning.js`, that sets `NSBluetoothAlwaysUsageDescription` to the same string as Task 8, and list it in `app.json` `plugins`.
6. Rewrite `esp-provisioner.ts` against `requireOptionalNativeModule('HomeguardProvisioning')`. When the module is `null` (Android), each method throws `HubProvisioningError('unknown', 'Adding a hub is only available on iPhone for now.')`.
7. Rerun the Step 4 gate, then commit as `feat(hub): add local ESPProvision module for hub setup`.

---

### Task 9: Add hub flow

**Files:**
- Create: `src/components/add-hub-flow.tsx`
- Create: `src/app/(tabs)/settings/hub.tsx`, `src/app/onboarding/hub.tsx`
- Modify: `src/app/onboarding/stove.tsx:127` (`router.push('/onboarding/hub')`)
- Modify: `src/components/hub-card.tsx` (add **Add hub**: `router.push('/settings/hub')`)

**Interfaces:**
- Consumes: `getHubProvisioner`, `HubProvisioningError` (Task 8); `getSetupWaitOutcome`, `PROVISION_FAILURE_MESSAGES` (Task 1); `acDevice`, `patchSettings` (context).
- Produces: `export function AddHubFlow(props: { onConnected: (deviceId: string) => void; onSkip?: () => void }): JSX.Element`.

- [ ] **Step 1: `AddHubFlow`.** Hold one provisioner per mount (`useRef(getHubProvisioner())`) and call `disconnect()` on unmount. These steps follow spec "1. First-time setup":
  1. **Intro:** "Plug in your hub. Its light blinks slowly when it's ready." Button **Find my hub**.
  2. **Scan:** `scanForHubs()`. With no results: "No hub found. Keep the phone near the hub and try again." Button **Scan again**. With one or more results, list them by `name`.
  3. **Pick:** `connect(hub)`, then `listWifiNetworks()`. Show the list, plus **Enter network name** for hidden networks and the hint "The hub only works with 2.4 GHz Wi-Fi."
  4. **Password:** a secure text field for secured networks; skip it for open ones. Then **Connect**.
  5. **Send:** `setupStartedAt = Date.now()` immediately before `sendWifiCredentials`.
     - On `HubProvisioningError`, show `PROVISION_FAILURE_MESSAGES[reason]`. `wrong-password` and `network-not-found` go back to the password or network step on the same connection. `disconnected` and `unknown` go back to Scan.
     - On success: `disconnect()`, `await patchSettings({ acDeviceId: hub.deviceId })` (this also resets AC monitoring for the new device), then move to Wait.
  6. **Wait:** tick `now` every 1 s. Compute `getSetupWaitOutcome({ lastSeenAt: acDevice.deviceId === hub.deviceId ? acDevice.lastSeenAt : null, setupStartedAt, waitStartedAt, now })`, with `waitStartedAt` starting equal to `setupStartedAt`.
     - `connected`: show "Hub connected", then call `onConnected(hub.deviceId)`.
     - `timed-out`: "The hub joined Wi-Fi but hasn't checked in yet." Buttons **Keep waiting** (`waitStartedAt = Date.now()`) and **Start over**, with the copy "Hold the hub's button for 10 seconds to reset it, then add it again."
  7. Show **Skip for now** only when `onSkip` is given.

- [ ] **Step 2: Routes.**
  - `settings/hub.tsx`: `SettingsPage` titled "Add hub", with `onConnected={() => router.replace('/settings/teach')}`.
  - `onboarding/hub.tsx`: onboarding shell, with `onConnected` → `router.push('/onboarding/teach')` and `onSkip` → `router.push('/onboarding/reminder')`.
  - `stove.tsx` now pushes `/onboarding/hub`.

- [ ] **Step 3: Verify**

Run: `npx expo start` (stop when Metro is ready), then `npm run typecheck && npm run lint && node --test tests/*.test.cjs`
Expected: no errors.

Demo check: Settings → AC monitoring → Add hub. The scan shows `HomeGuard-A1B2C3`, then the Wi-Fi list. Password `wrong` shows the wrong-password message and stays on the password step. A correct password reaches "Hub connected" about 3 s later and lands on Teach remote. HubCard now shows `homeguard-a1b2c3` with "Remote not taught".

- [ ] **Step 4: Commit**

```bash
git add src/components/add-hub-flow.tsx src/components/hub-card.tsx "src/app/(tabs)/settings/hub.tsx" src/app/onboarding/hub.tsx src/app/onboarding/stove.tsx
git commit -m "feat(hub): add Bluetooth Add hub flow to onboarding and settings"
```

---

### Task 10: Docs, full verification and demo walkthrough

**Files:**
- Modify: `docs/RUN.md`

- [ ] **Step 1: RUN.md.** Make these changes:
  - Add a "HomeGuard hub" section that links to the contract by its repo path, lists the four new paths, and says the hub never writes `powerState`.
  - Explain that Add hub needs a development build on a physical iPhone, because the simulator has no Bluetooth.
  - Document the Demo lab "Hub" controls and the `wrong` demo password.
  - Under "Checks", replace `node --test tests/stove-detection.test.cjs` with `node --test tests/*.test.cjs`.

- [ ] **Step 2: Full checks**

Run: `node --test tests/*.test.cjs && npm run typecheck && npm run lint && npx expo export --platform ios`
Expected: all pass; export completes.

- [ ] **Step 3: Demo-mode walkthrough** (`npm run ios`, no `.env.local`). Tick each item:
  - [ ] Onboarding: location → stove → **Add hub** (Skip for now works) → Teach remote (skip works) → reminder → dashboard.
  - [ ] Onboarding with Add hub: password `wrong` shows the wrong-password message, a correct password shows "Hub connected", and the flow continues to Teach remote.
  - [ ] Teach remote: the saved path ends with "Command sent" and "Did your AC turn off?". **No** returns to the start. **Yes** finishes.
  - [ ] Teach remote with Timeout and with Rejected (Demo lab) shows the right message and Try again.
  - [ ] Dashboard, AC state remote: Turn off AC shows "Waiting for the hub", then "Command sent". Nothing says the AC is off.
  - [ ] Dashboard, not taught: a Teach remote button appears, and it opens Teach remote.
  - [ ] Dashboard, toggle remote at home: the button is disabled with the power-button message.
  - [ ] Demo lab "Send AC alert now" with a toggle remote: the notification has no buttons and explains why. With AC state, it has Turn It Off / Keep It On. Tapping Turn It Off sends ("Command sent" on the dashboard).
  - [ ] Hub online off: the sensor row says offline, and a command stays "Waiting for the hub".
  - [ ] Simulate leaving home still works, and the existing stove flows are unchanged.

- [ ] **Step 4: Live checks, for when the Firebase project and hub exist** (record them in the PR; don't block on them)
  - [ ] A fresh hub on a physical iPhone: Add hub through to "Hub connected" within 90 s. `settings.acDeviceId` = `homeguard-xxxxxx`.
  - [ ] Wrong password, then the right one, without resetting the hub.
  - [ ] Teach remote writes `commands/learnRequest`. `learn/status` moves listening → saved, and `remote` appears.
  - [ ] Leave the geofence → notification → Turn It Off → `commands/result.status = sent` → the app says "Command sent" → the AC turns off. A shut-off calibration starts.
  - [ ] Unplug the hub: the app shows offline within about 3 min 15 s.

- [ ] **Step 5: Commit**

```bash
git add docs/RUN.md
git commit -m "docs(run): document hub setup, Teach remote and demo hub controls"
```
