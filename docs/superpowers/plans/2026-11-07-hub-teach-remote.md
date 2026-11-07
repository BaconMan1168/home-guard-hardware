# Hub Teach Remote Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When the app writes `commands/learnRequest`, the hub listens on the IR receiver for 30 s, saves the first real frame from the remote's OFF button to flash, classifies it (`ac-state` / `toggle` / `raw`), and reports progress in `learn/status` and the result in `remote`.

**Architecture:** The learn state machine, the request gate and the classification are pure functions in `lib/hub_core`, tested on the laptop. A small `src/ir_learner` adapter turns IRremoteESP8266's `decode_results` into a frame summary plus a `StoredCode`. `main.cpp` connects the stream callback, the adapter and the store.

**Tech Stack:** Same as the Hub Basics plan (PlatformIO, Arduino-ESP32 3.x, IRremoteESP8266, ArduinoJson v7, Unity).

**Spec:** `docs/superpowers/specs/2026-11-06-homeguard-hub-design.md` (section "2. Teach remote") and `docs/firebase-contract.md` (`commands/learnRequest`, `learn/status`, `remote`).

**Requires:** `docs/superpowers/plans/2026-11-07-hub-basics.md` fully done.

## Global Constraints

- Everything in Hub Basics' Global Constraints still applies.
- Learn window: 30 000 ms. Request freshness: same `isFresh` rule as commands (±120 000 ms). A request is new only if `requestedAt` > the persisted `lastLearnAt`.
- IR receiver: `IRrecv(kPinIrReceive /*14*/, 1024, 50, true)` with `setUnknownThreshold(12)` (the PDF's IRrecvDumpV2 settings). It is enabled **only** while listening.
- Frame rules: `rawlen < 8` is noise, so ignore it and keep listening. `overflow`, or `8 ≤ rawlen < 24`, is Rejected. `rawlen ≥ 24` is Saved.
- `learn/status.state` values: `"listening"`, `"saved"`, `"timeout"`, `"rejected"`.
- Classification: decoded and the protocol has AC state (`hasACState`) → `ac-state`. Decoded without AC state → `toggle`. Not decoded (`UNKNOWN`) → `raw`.
- A new code replaces the old one only on `saved`. `timeout` and `rejected` keep the old code.

## Review Focus

1. **Leftover request after reboot.** The stream's first event re-delivers `learnRequest`. It must not start listening if it isn't newer than the persisted `lastLearnAt` or isn't fresh. Pinned in Task 1 (`test_repeat_request_does_not_start`, `test_stale_request_does_not_start`).
2. **Short IR blips** from sunlight, LED lighting, or the NEC repeat burst sent while a button is held. These must not end the session. Pinned in Task 1 (`test_noise_frames_are_ignored`).
3. **A remote frame longer than the 1024-entry buffer.** Report `rejected`, not a truncated code. Pinned in Task 1 (`test_overflow_is_rejected`).
4. **A new request while already listening** restarts the window with the new `requestedAt`, so the app's status matches its latest request. Pinned in Task 1 (`test_new_request_restarts_session`).
5. **Flash write fails on save.** Report `rejected` and keep the old code and the old `remote` entry. Handled in Task 3, and checked on the board in Task 3, step 4.

---

### Task 1: Learn logic in `hub_core`

**Files:**
- Create: `lib/hub_core/src/learn_session.{h,cpp}`
- Modify: `lib/hub_core/src/hub_config.h` (add `kLearnWindowMs = 30000`, `kNoiseMaxRawLen = 7`, `kMinFrameRawLen = 24`)
- Modify: `lib/hub_core/src/payloads.{h,cpp}` (add `learnStatusJson`)
- Test: `test/test_learn_session/test_main.cpp`, extend `test/test_payloads/test_main.cpp`

**Interfaces:**
- Consumes: `isFresh`, `CodeKind` (Hub Basics Task 2).
- Produces:
  ```cpp
  enum class LearnState { Idle, Listening, Saved, Timeout, Rejected };
  const char* learnStateName(LearnState);   // "listening","saved","timeout","rejected" ("idle" for Idle)
  struct FrameInfo { uint16_t rawLen; bool overflow; };
  CodeKind classifyCode(bool decoded, bool hasAcState);
  bool shouldStartLearn(int64_t requestedAt, int64_t lastLearnAt, int64_t nowMs);
  class LearnSession {
   public:
    void start(int64_t requestedAt, uint32_t nowMs);      // → Listening (restarts if already listening)
    LearnState onFrame(const FrameInfo&, uint32_t nowMs); // only acts while Listening
    LearnState tick(uint32_t nowMs);                      // Listening → Timeout after kLearnWindowMs
    LearnState state() const;
    int64_t requestedAt() const;
    void reset();                                          // → Idle
  };
  std::string learnStatusJson(int64_t requestedAt, LearnState, int64_t updatedAt);
  // {"requestedAt":..,"state":"listening","updatedAt":..}
  ```

- [ ] **Step 1: Write the failing tests**

`test_learn_session` (now = 1789262500000; ms values are from `millis()`):
- `test_fresh_new_request_starts`: `shouldStartLearn(now-1000, 0, now)` → true.
- `test_repeat_request_does_not_start`: requestedAt == lastLearnAt → false; also requestedAt < lastLearnAt → false.
- `test_stale_request_does_not_start`: `now-120001` → false.
- `test_long_frame_is_saved`: start at 0, `onFrame({200,false}, 5000)` → Saved.
- `test_noise_frames_are_ignored`: rawLen 4, then 7 → still Listening. Then rawLen 100 → Saved.
- `test_mid_length_frame_is_rejected`: rawLen 8 → Rejected. A separate session with rawLen 23 → Rejected.
- `test_overflow_is_rejected`: `{1024, true}` → Rejected.
- `test_timeout_after_window`: `tick(29999)` → Listening, `tick(30000)` → Timeout.
- `test_frames_after_finish_are_ignored`: after Saved, `onFrame` returns Saved, and `tick(99999)` returns Saved.
- `test_new_request_restarts_session`: start(rA, 0), `start(rB, 20000)`, `tick(45000)` → Listening, `requestedAt() == rB`, `tick(50000)` → Timeout.
- `test_classify`: (true,true) → AcState; (true,false) → Toggle; (false,false) → Raw; (false,true) → Raw.

`test_payloads` additions:
- `test_learn_status_json`: (r, Listening, u) → `requestedAt` r, `state` "listening", `updatedAt` u. Also Timeout → "timeout".

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e native -f test_learn_session -f test_payloads`
Expected: FAIL (types not declared).

- [ ] **Step 3: Implement `learn_session.{h,cpp}`, the `hub_config` constants and `learnStatusJson`**

- [ ] **Step 4: Run to verify pass**

Run: `pio test -e native`
Expected: all tests (old and new) PASS.

- [ ] **Step 5: Commit**

```bash
git add lib/hub_core test/test_learn_session test/test_payloads
git commit -m "feat(hub): add teach remote state machine and status payload"
```

---

### Task 2: IR learner adapter

**Files:**
- Create: `src/ir_learner.{h,cpp}`

**Interfaces:**
- Consumes: `StoredCode`, `classifyCode`, `FrameInfo`, `kPinIrReceive`.
- Produces:
  ```cpp
  void irLearnerBegin();                // constructs IRrecv with the Global Constraints settings
  void irLearnerEnable(bool on);        // enableIRIn() / disableIRIn()
  bool irLearnerPoll(FrameInfo& frame, StoredCode& code);   // true when a frame was captured
  ```

- [ ] **Step 1: Implement**

On `irrecv.decode(&results)`:
- `frame = {results.rawlen, results.overflow}`.
- `decoded = results.decode_type != UNKNOWN`, then `code.kind = classifyCode(decoded, hasACState(results.decode_type))`.
- `code.protocol = results.decode_type` and `code.protocolName = typeToString(results.decode_type)`.
- AcState: copy `results.bits / 8` bytes of `results.state`.
- Toggle: `value = results.value`, `bits = results.bits`.
- Raw: fill `code.raw` from `resultToRawArray(&results)` with length `getCorrectedRawLength(&results)`, then free the array.

Leave `learnedAt` and `present` to the caller. Call `irrecv.resume()` after every frame.

- [ ] **Step 2: Build for the board**

Run: `pio run -e esp32dev`
Expected: success.

- [ ] **Step 3: Commit**

```bash
git add src/ir_learner.*
git commit -m "feat(hub): add ir learner adapter"
```

---

### Task 3: Wire Teach remote into `main.cpp`, with board checks

**Files:**
- Modify: `src/main.cpp`, `docs/board-checklist.md`

**Interfaces:**
- Consumes: Task 1, Task 2, plus Hub Basics' `code_store`, `cloud`, `status_led`.

- [ ] **Step 1: Implement**

- `setup()`: `irLearnerBegin()` (receiver stays disabled), and `lastLearnAt = loadLastLearnAt()`.
- `onLearnRequest(r)`: if `shouldStartLearn(r, lastLearnAt, nowMs())`, then:
  1. `saveLastLearnAt(r)` and update `lastLearnAt`.
  2. `session.start(r, millis())`, `irLearnerEnable(true)`, LED mode Learning.
  3. `cloudSet("learn/status", learnStatusJson(r, Listening, nowMs()))`.
- In `loop()`, while `session.state() == Listening`:
  - If `irLearnerPoll(frame, candidate)`, pass `session.onFrame(frame, millis())`. Then call `session.tick(millis())`.
  - On reaching **Saved**: set `candidate.present = true` and `candidate.learnedAt = nowMs()`. If `saveCode(candidate)` succeeds, set `code = candidate`, then `cloudSet("remote", remoteJson(code))` and status `saved`. If the save fails, report status `rejected` and keep the old `code`.
  - On reaching **Timeout / Rejected**: write that status.
  - In every finished case: `irLearnerEnable(false)`, `session.reset()`, and the LED goes back to the normal Online/Connecting mode.
- OFF commands keep working while listening, using the current `code`.

- [ ] **Step 2: Run all host tests and the board build**

Run: `pio test -e native && pio run -e esp32dev`
Expected: all PASS, and the build succeeds.

- [ ] **Step 3: Add the "Teach remote" section to `docs/board-checklist.md`**

1. In the console, set `commands/learnRequest` to `{"requestedAt":{".sv":"timestamp"}}`: `learn/status.state` becomes `listening` and the LED fast-blinks.
2. Point the AC remote at the hub and press OFF: the state becomes `saved`, and `remote` shows `learned:true` with a `kind` and `protocol`. Note the kind for the demo AC.
3. Repeat the request and wait 30 s without pressing anything: `timeout`, and `remote` is unchanged.
4. Reboot: no new `listening` (the leftover request is ignored), and `remote` still shows the saved code.
5. With the AC on, set `commands/latest` to OFF (as in Hub Basics step 4): the AC turns off and `commands/result.status` is `"sent"`. A phone camera shows the IR LEDs flickering 3 times for `ac-state` and once otherwise.

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp docs/board-checklist.md
git commit -m "feat(hub): teach remote over firebase"
```
