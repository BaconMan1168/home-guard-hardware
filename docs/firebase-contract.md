# HomeGuard hub ↔ app: Firebase contract

The single place that defines what the hub and the ac-guard app write to Firebase Realtime Database, and where. Both sides build against this file. If either side needs a change, change it here first.

- App: `~/repos/ac-guard` (reads/writes in `src/services/firebase.ts`, types in `src/types/home-guard.ts`)
- Hub: this repo
- All timestamps are epoch milliseconds (UTC).
- Rows marked **existing** are already implemented in ac-guard (documented in `ac-guard/docs/RUN.md`). Rows marked **new** are added by this project.

## Device ID

`deviceId` = the hub's Bluetooth setup name, lowercased: `homeguard-` + the last 3 bytes of the ESP32's Wi-Fi MAC in lowercase hex (e.g. `homeguard-a1b2c3`). The app learns it from the Bluetooth name during setup and stores it as `settings.acDeviceId`.

## Tree

```
devices/{deviceId}/
  telemetry/                      hub writes     (existing)
  temperatureReadings/{pushId}    hub writes     (existing)
  remote/                         hub writes     (new)
  commands/latest                 app writes     (existing)
  commands/result                 hub writes     (new)
  commands/learnRequest           app writes     (new)
  learn/status                    hub writes     (new)
```

The hub holds **one** live stream, on `devices/{deviceId}/commands`, and reacts only to changes under `latest` and `learnRequest` (it ignores the echo of its own `result` writes). Each extra stream costs the ESP32 another TLS connection (~40 KB of RAM), which is why the learn request lives under `commands`.

### `telemetry` (hub → app, existing)

Written every 60 s, overwriting.

```json
{ "temperatureCelsius": 22.4, "lastSeenAt": 1789262500000 }
```

The hub never writes `powerState` or `powerStateUpdatedAt`. It cannot observe whether the AC is on.

### `temperatureReadings/{pushId}` (hub → app, existing)

One new child every 60 s (Firebase push ID). The app queries this by `timestamp` and needs `".indexOn": ["timestamp"]` in the rules.

```json
{ "timestamp": 1789262500000, "temperatureCelsius": 22.4 }
```

### `remote` (hub → app, new)

Describes the saved OFF code. Written at boot (once online), after every learn attempt, and after a factory reset clears the code.

```json
{ "learned": true, "learnedAt": 1789262500000, "kind": "ac-state", "protocol": "DAIKIN" }
```

| Field | Meaning |
|---|---|
| `learned` | `false` until an OFF code is saved. The app shows "Teach your remote" while this is `false`. |
| `kind` | `"ac-state"`: the library recognised a stateful AC protocol (safe to repeat). `"toggle"`: a code that may flip power (short codes, and AC protocols with a power-toggle bit: DAIKIN128, HITACHI_AC1, WHIRLPOOL_AC, YORK). `"raw"`: unrecognised, stored as raw timings. |
| `protocol` | Library protocol name, or `"UNKNOWN"` for raw. Display/debug only. |

### `commands/latest` (app → hub, existing)

```json
{ "value": "OFF", "requestedAt": 1789262500000, "requestedBy": "<uid>", "source": "notification" }
```

`requestedAt` is a server timestamp. The hub acts only on `value: "OFF"`.

### `commands/result` (hub → app, new)

Written once per command the hub handles.

```json
{ "requestedAt": 1789262500000, "status": "sent", "handledAt": 1789262501200 }
```

| `status` | Meaning | App shows |
|---|---|---|
| `sent` | OFF code fired | "Command sent" (never "AC off") |
| `failed` | The hub could not send the saved code | "Hub couldn't send the OFF signal. Teach your remote again" |
| `no-code` | No OFF code saved | "Teach your remote first" |
| `unsupported` | Value was not `OFF` | "The hub can only turn the AC off" |
| `stale` | Command older than 2 minutes when the hub saw it | "Hub was offline; command not sent" |

`requestedAt` echoes the command it answers, so the app can match a result to its request.

When its stream (re)connects, the hub skips a `latest` command that `result` already answers, so a reboot or factory reset never rewrites an old result.

### `commands/learnRequest` (app → hub, new)

```json
{ "requestedAt": 1789262500000, "requestedBy": "<uid>" }
```

`requestedAt` is a server timestamp. Each new value starts a learn attempt, if it is under 2 minutes old (same freshness rule as commands).

### `learn/status` (hub → app, new)

```json
{ "requestedAt": 1789262500000, "state": "listening", "updatedAt": 1789262500400 }
```

`state` moves `listening` → one of `saved` | `timeout` | `rejected`. `rejected` means a signal arrived but was too short or noisy to trust. `requestedAt` echoes the request being answered.

## Rules (prototype)

The existing prototype rules from `ac-guard/docs/RUN.md` stay as they are (`auth != null` read/write under `devices/$deviceId`, plus the `temperatureReadings` index). The hub signs in anonymously, like the app. Per-owner rules are a known gap (see the design doc).
