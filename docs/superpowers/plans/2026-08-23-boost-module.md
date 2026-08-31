# Boost Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A first-class Boost backend feature: "charge NOW until a target is reached, then hand control back", with `/boost` REST + MQTT + event surface and simulator coverage.

**Architecture:** A `Boost` MicroTasks task (mirroring `Limit`) owns the `EvseClient_OpenEVSE_Boost` Active claim at priority 200 and releases it when a threshold is reached. Threshold compares live in a new pure `ChargeThreshold` unit shared with `Limit`; wall-clock deadline math lives in a pure `deadline_timer.h` lifted from the claim-duration branch. The scenario-driven simulator (`divert_sim/sim/*`) gains per-peer Boost instances, scenario `boost`/`manual` events, and a `boost` CSV column.

**Tech Stack:** C++ (Arduino/ESP32 + native host), PlatformIO, doctest (`native_test`), MicroTasks, ArduinoJson 6, Mongoose HTTP, pytest (simulator + integration suites).

**Spec:** `docs/superpowers/specs/2026-08-23-boost-module-design.md` (committed on this branch — read it first).

## Global Constraints

- Branch: `feature/boost`, cut off upstream master `b7a606b1`. Work in an isolated worktree/checkout of that branch; all paths below are relative to the repository root. All commits authored as the default git identity (Andrew Rankin); no AI attribution anywhere.
- Claim identity: `EvseClient_OpenEVSE_Boost` / `EvseManager_Priority_Boost` (200) — **already defined** in `src/evse_man.h:30,48`; define nothing new there.
- Boost time values are **seconds** (Limit's are minutes). Energy Wh **delta since activation**. SoC/Range **absolute** targets.
- `BOOST_MAX_TIME_S` = `(7 * 24 * 3600)`; deadline compares must be rollover-safe (`(int32_t)(now - deadline) >= 0`).
- HTTP codes: POST 201 armed / 400 malformed / 422 SoC-Range without vehicle data; GET idle = **200 + `{}`**; DELETE active = 200, idle = **404**.
- `/status` gains `"boost"` (bool) and `"boost_version"` (uint8, bumps on every arm/re-arm/cancel/expiry).
- Events carry `"boost"` (bool) + `"boost_version"`, and `"boost_reason"` (`"reached"|"cancelled"|"replaced"`) on end. (Spec's `{"boost": {props}}` sketch is resolved to a boolean for `/status`-model consistency — full props live on GET `/boost` and the MQTT `<base>/boost` topic.)
- MQTT: subscribe `<base>/boost/set` (same JSON as POST; `"off"`, `"clear"` or empty = cancel); publish serialized boost (or `{}`) retained on version change, exactly like `limit`.
- No persistence; no config options; 1 Hz tick while active, `MicroTask.Infinate` when idle.
- Limit's observable behavior must not change: `pio test -e native_test`, the simulator pytest suite, and the integration suite are the proof after the refactor task.
- Existing simulator CSV columns keep their names and order; the new `boost` column is appended **last** in the per-peer group.
- Run all commands from the worktree root unless a `cd` is shown.

---

### Task 1: Pure deadline math (`deadline_timer.h`) + host tests

Lifted from `origin/feature/claim-duration:src/claim_timer.h` (verified present in that branch), renamed since it no longer times claims.

**Files:**
- Create: `src/deadline_timer.h`
- Test: `test/test_deadline_timer/test_deadline_timer.cpp`

**Interfaces:**
- Produces: `deadline_timer_expired(uint32_t release_at_ms, uint32_t now_ms) -> bool`, `deadline_timer_remaining_s(uint32_t release_at_ms, uint32_t now_ms) -> uint32_t` (ceil), `DEADLINE_TIMER_MAX_DURATION_S` (= 7*24*3600). Task 4 consumes all three.

- [ ] **Step 1: Write the failing test**

Create `test/test_deadline_timer/test_deadline_timer.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "deadline_timer.h"

TEST_CASE("deadline_timer_expired: no deadline never expires") {
  CHECK_FALSE(deadline_timer_expired(0, 0));
  CHECK_FALSE(deadline_timer_expired(0, 1000000));
}

TEST_CASE("deadline_timer_expired: before/at/after deadline") {
  CHECK_FALSE(deadline_timer_expired(1000, 999));
  CHECK(deadline_timer_expired(1000, 1000));
  CHECK(deadline_timer_expired(1000, 1001));
}

TEST_CASE("deadline_timer_expired: millis() wrap is rollover-safe") {
  // deadline just after the wrap, now just before it: not yet expired.
  CHECK_FALSE(deadline_timer_expired(100, 0xFFFFFF00));
  // now == deadline: expired.
  CHECK(deadline_timer_expired(100, 100));
  // now has wrapped past the deadline: expired.
  CHECK(deadline_timer_expired(100, 200));
}

TEST_CASE("deadline_timer_remaining_s: no deadline is 0") {
  CHECK(deadline_timer_remaining_s(0, 0) == 0);
  CHECK(deadline_timer_remaining_s(0, 5000) == 0);
}

TEST_CASE("deadline_timer_remaining_s: 0 once past the deadline") {
  CHECK(deadline_timer_remaining_s(1000, 1000) == 0);
  CHECK(deadline_timer_remaining_s(1000, 1001) == 0);
}

TEST_CASE("deadline_timer_remaining_s: ceil rounding") {
  CHECK(deadline_timer_remaining_s(2000, 0) == 2);     // exact 2000ms -> 2s
  CHECK(deadline_timer_remaining_s(2000, 1) == 2);     // 1999ms -> ceil 2s
  CHECK(deadline_timer_remaining_s(2000, 1001) == 1);  // 999ms -> ceil 1s
  CHECK(deadline_timer_remaining_s(2000, 1000) == 1);  // 1000ms -> 1s
}

TEST_CASE("deadline_timer_remaining_s: millis() wrap") {
  // deadline 100ms after the wrap, now 0x100ms before it -> 200ms -> 1s.
  CHECK(deadline_timer_remaining_s(100, 0xFFFFFF9C) == 1);
}

TEST_CASE("DEADLINE_TIMER_MAX_DURATION_S survives the deadline math") {
  uint32_t now_ms = 0;
  uint32_t release_at_ms = now_ms + (uint32_t)DEADLINE_TIMER_MAX_DURATION_S * 1000UL;

  CHECK_FALSE(deadline_timer_expired(release_at_ms, now_ms));
  CHECK(deadline_timer_remaining_s(release_at_ms, now_ms) == (uint32_t)DEADLINE_TIMER_MAX_DURATION_S);
  CHECK_FALSE(deadline_timer_expired(release_at_ms, now_ms + 1000));
  CHECK(deadline_timer_remaining_s(release_at_ms, now_ms + 1000) == (uint32_t)DEADLINE_TIMER_MAX_DURATION_S - 1);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native_test -f test_deadline_timer`
Expected: build FAILURE — `deadline_timer.h: No such file or directory`

- [ ] **Step 3: Write the header**

Create `src/deadline_timer.h`:

```cpp
#ifndef DEADLINE_TIMER_H
#define DEADLINE_TIMER_H

// Pure wall-clock deadline math, host-testable (no Arduino includes).
// All times are millis()-domain uint32_t; comparisons use rollover-safe
// signed subtraction so this remains correct across the ~49.7-day millis()
// wrap.

#include <stdint.h>

// Maximum allowed duration, in seconds. Durations must stay far below the
// ~24.8-day (2^31 ms) signed-compare horizon used by deadline_timer_expired():
// a deadline computed past that horizon reads as already expired the instant
// it is set, and a duration approaching 2^32/1000 s (~49.7 days) wraps the
// `* 1000UL` millis multiply used to compute it. 7 days is far above any real
// boost duration and comfortably inside that horizon.
#define DEADLINE_TIMER_MAX_DURATION_S (7 * 24 * 3600)

static inline bool deadline_timer_expired(uint32_t release_at_ms, uint32_t now_ms)
{
  return release_at_ms != 0 && (int32_t)(now_ms - release_at_ms) >= 0;
}

static inline uint32_t deadline_timer_remaining_s(uint32_t release_at_ms, uint32_t now_ms)
{
  if(release_at_ms == 0 || (int32_t)(now_ms - release_at_ms) >= 0) {
    return 0;
  }
  return (release_at_ms - now_ms + 999) / 1000;  // ceil
}

#endif // DEADLINE_TIMER_H
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native_test -f test_deadline_timer`
Expected: PASS (9 test cases). Header-only, so no `build_src_filter` change is needed.

- [ ] **Step 5: Commit**

```bash
git add src/deadline_timer.h test/test_deadline_timer/test_deadline_timer.cpp
git commit -m "feat: add pure rollover-safe deadline math + native tests"
```

---

### Task 2: `ChargeThreshold` shared threshold unit + host tests

**Files:**
- Create: `src/charge_threshold.h`, `src/charge_threshold.cpp`
- Modify: `platformio.ini` (`[env:native_test]` `build_src_filter`, currently line ~581)
- Test: `test/test_charge_threshold/test_charge_threshold.cpp`

**Interfaces:**
- Consumes: `LimitType` from `src/limit.h` (enum values `None,Time,Energy,Soc,Range`).
- Produces: `ChargeThreshold::reached(LimitType type, uint32_t target, uint32_t basis, uint32_t current) -> bool` and `ChargeThreshold::remaining(LimitType, uint32_t target, uint32_t basis, uint32_t current) -> uint32_t`. Semantics: reached when `target > 0 && current >= basis + target` (basis 0 = absolute target; non-zero basis = delta-from-activation). `remaining` = `basis + target - current`, floored at 0; `None` type is never reached, remaining 0. Tasks 3 and 4 consume both.

**Header-dependency note:** `limit.h` includes `evse_man.h` (heavy, Arduino-typed). To keep this unit host-pure, `charge_threshold.h` does NOT include `limit.h`; it takes the type as `uint8_t` matching `LimitType::Value`'s underlying type, and `limit.h`-including callers pass the enum (implicit conversion via `LimitType::operator Value()`).

- [ ] **Step 1: Write the failing test**

Create `test/test_charge_threshold/test_charge_threshold.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "charge_threshold.h"

// Mirror of LimitType::Value (limit.h) — kept in sync by the static_asserts
// in charge_threshold.cpp's includes being compiled firmware-side.
enum { T_NONE = 0, T_TIME = 1, T_ENERGY = 2, T_SOC = 3, T_RANGE = 4 };

TEST_CASE("absolute target (basis 0): reached at/above target") {
  CHECK_FALSE(ChargeThreshold::reached(T_SOC, 80, 0, 79));
  CHECK(ChargeThreshold::reached(T_SOC, 80, 0, 80));
  CHECK(ChargeThreshold::reached(T_SOC, 80, 0, 81));
}

TEST_CASE("zero target is never reached (matches Limit's val > 0 guard)") {
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 0, 0, 100000));
  CHECK_FALSE(ChargeThreshold::reached(T_SOC, 0, 0, 100));
  CHECK(ChargeThreshold::remaining(T_ENERGY, 0, 0, 100000) == 0);
}

TEST_CASE("None type is never reached") {
  CHECK_FALSE(ChargeThreshold::reached(T_NONE, 100, 0, 200));
  CHECK(ChargeThreshold::remaining(T_NONE, 100, 0, 200) == 0);
}

TEST_CASE("delta basis: reached measures growth since activation") {
  // 5000 Wh already in the session at arm; boost target = 2000 Wh more.
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 5000));
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 6999));
  CHECK(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 7000));
  CHECK(ChargeThreshold::reached(T_ENERGY, 2000, 5000, 9000));
}

TEST_CASE("remaining counts down to 0 and never underflows") {
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 5000) == 2000);
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 6500) == 500);
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 7000) == 0);
  CHECK(ChargeThreshold::remaining(T_ENERGY, 2000, 5000, 9999) == 0);
  CHECK(ChargeThreshold::remaining(T_SOC, 80, 0, 50) == 30);
}

TEST_CASE("basis + target near uint32 max does not overflow") {
  // 64-bit internal sum: a huge basis must not wrap into "already reached".
  CHECK_FALSE(ChargeThreshold::reached(T_ENERGY, 10, 0xFFFFFFF0u, 0xFFFFFFF5u));
  CHECK(ChargeThreshold::remaining(T_ENERGY, 10, 0xFFFFFFF0u, 0xFFFFFFF5u) == 5);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native_test -f test_charge_threshold`
Expected: build FAILURE — `charge_threshold.h: No such file or directory`

- [ ] **Step 3: Write the implementation**

Create `src/charge_threshold.h`:

```cpp
#ifndef _OPENEVSE_CHARGE_THRESHOLD_H
#define _OPENEVSE_CHARGE_THRESHOLD_H

// Shared threshold evaluation for Limit and Boost.
//
// Pure integer math over values the caller reads, so it is host-testable with
// no firmware dependencies. `type` is LimitType::Value's underlying uint8_t
// (limit.h is deliberately not included here — it drags in evse_man.h);
// callers pass a LimitType and the enum converts implicitly.
//
//   basis = the dimension's value at activation. 0 for absolute targets
//           (Limit's session totals, Boost's SoC/Range); the activation
//           snapshot for delta targets (Boost's energy).
//   reached: target > 0 && current >= basis + target
//   remaining: (basis + target) - current, floored at 0.
//
// The Time dimension for Boost is wall-clock and handled by deadline_timer.h,
// not here; Limit's session-minutes Time check does flow through here.

#include <stdint.h>

class ChargeThreshold
{
  public:
    static bool reached(uint8_t type, uint32_t target, uint32_t basis, uint32_t current);
    static uint32_t remaining(uint8_t type, uint32_t target, uint32_t basis, uint32_t current);
};

#endif // _OPENEVSE_CHARGE_THRESHOLD_H
```

Create `src/charge_threshold.cpp`:

```cpp
#include "charge_threshold.h"

// LimitType::Value::None == 0; every other type compares the same way, so the
// only type-dependent behavior is "None never fires".
static const uint8_t TYPE_NONE = 0;

bool ChargeThreshold::reached(uint8_t type, uint32_t target, uint32_t basis, uint32_t current)
{
  if(type == TYPE_NONE || target == 0) {
    return false;
  }
  return (uint64_t)current >= (uint64_t)basis + (uint64_t)target;
}

uint32_t ChargeThreshold::remaining(uint8_t type, uint32_t target, uint32_t basis, uint32_t current)
{
  if(type == TYPE_NONE || target == 0) {
    return 0;
  }
  uint64_t goal = (uint64_t)basis + (uint64_t)target;
  return (uint64_t)current >= goal ? 0 : (uint32_t)(goal - current);
}
```

Modify `platformio.ini` `[env:native_test]` — extend `build_src_filter` (one line, currently ends `+<http_etag.cpp>`):

```ini
build_src_filter = -<*> +<tsdb_sample.cpp> +<home_battery.cpp> +<lvgl_tft/backlight.cpp> +<crypto/sha256.c> +<crypto/hmac_sha256.cpp> +<web_auth.cpp> +<ota_url_allow.cpp> +<http_etag.cpp> +<charge_threshold.cpp>
```

(If the m6/fork-specific entries like `tsdb_sample.cpp` are absent on this upstream-master branch, keep whatever the existing line is and only append `+<charge_threshold.cpp>` — the append is the change, not the rest of the line.)

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native_test -f test_charge_threshold`
Expected: PASS (6 test cases)

- [ ] **Step 5: Run the whole native suite (no regressions)**

Run: `pio test -e native_test`
Expected: all suites PASS (including test_deadline_timer from Task 1)

- [ ] **Step 6: Commit**

```bash
git add src/charge_threshold.h src/charge_threshold.cpp test/test_charge_threshold/test_charge_threshold.cpp platformio.ini
git commit -m "feat: shared ChargeThreshold unit for Limit/Boost threshold math"
```

---

### Task 3: Refactor Limit onto ChargeThreshold (behavior unchanged)

**Files:**
- Modify: `src/limit.cpp` (the four `limitTime/limitEnergy/limitSoc/limitRange` bodies, lines ~243-300)

**Interfaces:**
- Consumes: `ChargeThreshold::reached` (Task 2).
- Produces: nothing new — `Limit`'s public API and behavior are unchanged. The proof is the existing suites staying green.

- [ ] **Step 1: Refactor the four compare methods**

In `src/limit.cpp`, add the include after `#include "limit.h"`:

```cpp
#include "charge_threshold.h"
```

Replace the four method bodies (keep signatures and the DBUG lines' spirit; exact result):

```cpp
bool Limit::limitTime(uint32_t val) {
  // Limit's time base is session-elapsed charging minutes (NOT wall-clock —
  // Boost handles wall-clock via deadline_timer.h).
  uint32_t elapsed = (uint32_t)_evse->getSessionElapsed()/60;
  bool reached = ChargeThreshold::reached(LimitType::Time, val, 0, elapsed);
  if (reached) {
    DBUGLN("Time limit reached");
    DBUGVAR(val);
    DBUGVAR(elapsed);
  }
  return reached;
};

bool Limit::limitEnergy(uint32_t val) {
  uint32_t elapsed = _evse->getSessionEnergy();
  bool reached = ChargeThreshold::reached(LimitType::Energy, val, 0, elapsed);
  if (reached) {
    DBUGLN("Energy limit reached");
    DBUGVAR(val);
    DBUGVAR(elapsed);
  }
  return reached;
};

bool Limit::limitSoc(uint32_t val) {
  uint32_t soc = _evse->getVehicleStateOfCharge();
  bool reached = ChargeThreshold::reached(LimitType::Soc, val, 0, soc);
  if (reached) {
    DBUGLN("SOC limit reached");
    DBUGVAR(val);
    DBUGVAR(soc);
  }
  return reached;
};

bool Limit::limitRange(uint32_t val) {
  uint32_t rng = _evse->getVehicleRange();
  bool reached = ChargeThreshold::reached(LimitType::Range, val, 0, rng);
  if (reached) {
    DBUGLN("Range limit reached");
    DBUGVAR(val);
    DBUGVAR(rng);
  }
  return reached;
};
```

- [ ] **Step 2: Firmware compiles**

Run: `pio run -e openevse_wifi_tft_v1 2>&1 | tail -3`
Expected: `SUCCESS`

- [ ] **Step 3: Native + simulator suites green (the behavior-unchanged proof)**

```bash
pio test -e native_test
pio run -e native_simulator
cd divert_sim && python3 -m pytest -x -q; cd ..
```
Expected: all PASS. (The simulator does not currently compile `limit.cpp`, so its pass here proves the shared-unit change didn't disturb the sim build; the unit-level proof is Task 2's tests plus compile-time use.)

- [ ] **Step 4: Commit**

```bash
git add src/limit.cpp
git commit -m "refactor: Limit threshold compares via shared ChargeThreshold"
```

---

### Task 4: Boost module core

**Files:**
- Create: `src/boost.h`, `src/boost.cpp`
- Modify: `src/main.cpp` (after `limit.begin(evse);`, line ~187)

**Interfaces:**
- Consumes: `deadline_timer.h` (Task 1), `ChargeThreshold` (Task 2), `LimitType` (from `limit.h`), `EvseManager` claim API (`claim(EvseClient, int, EvseProperties&)`, `release(EvseClient)`, `clientHasClaim`, `getSessionEnergy()`, `getVehicleStateOfCharge()`, `getVehicleRange()`, `isVehicleStateOfChargeValid()`, `isVehicleRangeValid()`, `onSessionComplete(EventListener*)`).
- Produces (consumed by Tasks 5-7):
  - `class Boost : public MicroTasks::Task`, global `extern Boost boost;`
  - `void begin(EvseManager &evse)`
  - `enum BoostArmResult : int { Boost_Armed = 0, Boost_BadRequest = -1, Boost_Unsupported = -2 }`
  - `int arm(LimitType type, uint32_t value)`, `int arm(const char *json)` (parses `{"type","value"}`)
  - `bool cancel()` — releases + clears, reason `cancelled`; returns false when idle
  - `bool isActive()`, `uint8_t getVersion()`, `LimitType getType()`, `uint32_t getValue()`
  - `uint32_t getRemaining()` — seconds (Time, ceil) / Wh / SoC-% gap / range gap
  - `time_t getStarted()`
  - `void serialize(JsonDocument &doc)` — fills `type`, `value`, `remaining`, `started` (ISO8601 UTC)
  - `#define BOOST_MAX_TIME_S DEADLINE_TIMER_MAX_DURATION_S`

- [ ] **Step 1: Write `src/boost.h`**

```cpp
#ifndef _OPENEVSE_BOOST_H
#define _OPENEVSE_BOOST_H

// Boost: "charge NOW until a target is reached, then hand control back."
//
// The inverse of Limit: while armed it holds an Active claim at
// EvseManager_Priority_Boost (200) — overriding Divert (50) and the
// Scheduler (100), but losing to Manual (1000), Limit (1100) and Safety —
// and when the target is reached it RELEASES the claim, so whatever was in
// control resumes. Limit, by contrast, claims Disabled when its target hits.
//
// Dimensions share Limit's type/value vocabulary but differ in basis:
//   time   — wall-clock seconds from activation (Limit: session minutes)
//   energy — Wh added since activation             (Limit: session total)
//   soc    — absolute %  (already met => releases on the first tick)
//   range  — absolute distance (same)
//
// One boost at a time; re-arming replaces (fresh activation snapshot).
// Not persisted: a reboot clears any active boost.

#ifndef EVSE_BOOST_LOOP_TIME
#define EVSE_BOOST_LOOP_TIME 1000
#endif

#include <Arduino.h>
#include <ArduinoJson.h>
#include <MicroTasks.h>

#include "evse_man.h"
#include "limit.h"            // LimitType: shared type/value vocabulary
#include "deadline_timer.h"

#define BOOST_MAX_TIME_S DEADLINE_TIMER_MAX_DURATION_S

enum BoostArmResult : int {
  Boost_Armed      = 0,
  Boost_BadRequest = -1,   // unknown/none type, zero value, bad JSON
  Boost_Unsupported = -2,  // soc/range without a vehicle data source
};

class Boost : public MicroTasks::Task
{
  private:
    EvseManager *_evse = nullptr;
    LimitType _type = LimitType::None;
    uint32_t _value = 0;
    uint32_t _deadline_ms = 0;     // Time dimension only
    uint32_t _energy_basis_wh = 0; // Energy dimension only
    time_t   _started = 0;
    uint8_t  _version = 0;
    MicroTasks::EventListener _sessionCompleteListener;

    void endBoost(const char *reason);
    void sendEvent(bool active, const char *reason);

  protected:
    void setup();
    unsigned long loop(MicroTasks::WakeReason reason);

  public:
    Boost();
    ~Boost();
    void begin(EvseManager &evse);

    int arm(LimitType type, uint32_t value);
    int arm(const char *json);
    bool cancel();

    bool isActive() { return _type != LimitType::None; }
    uint8_t getVersion() { return _version; }
    LimitType getType() { return _type; }
    uint32_t getValue() { return _value; }
    time_t getStarted() { return _started; }
    uint32_t getRemaining();
    void serialize(JsonDocument &doc);
};

extern Boost boost;

#endif // _OPENEVSE_BOOST_H
```

- [ ] **Step 2: Write `src/boost.cpp`**

```cpp
#if defined(ENABLE_DEBUG) && !defined(ENABLE_DEBUG_BOOST)
#undef ENABLE_DEBUG
#endif

#include <time.h>

#include "boost.h"
#include "charge_threshold.h"
#include "debug.h"
#include "event.h"

Boost boost;

Boost::Boost() :
  MicroTasks::Task(),
  _sessionCompleteListener(this)
{
}

Boost::~Boost()
{
  // The global (and the simulator's per-peer instances) may be destroyed
  // without begin() ever running.
  if(_evse && _evse->clientHasClaim(EvseClient_OpenEVSE_Boost)) {
    _evse->release(EvseClient_OpenEVSE_Boost);
  }
}

void Boost::setup()
{
}

void Boost::begin(EvseManager &evse)
{
  DBUGLN("Starting Boost task");
  _evse = &evse;
  MicroTask.startTask(this);
  _evse->onSessionComplete(&_sessionCompleteListener);
}

void Boost::sendEvent(bool active, const char *reason)
{
  StaticJsonDocument<96> doc;
  doc["boost"] = active;
  doc["boost_version"] = _version;
  if(reason) {
    doc["boost_reason"] = reason;
  }
  event_send(doc);
}

int Boost::arm(LimitType type, uint32_t value)
{
  if(LimitType::None == type || 0 == value) {
    return Boost_BadRequest;
  }
  if((LimitType::Soc == type && !_evse->isVehicleStateOfChargeValid()) ||
     (LimitType::Range == type && !_evse->isVehicleRangeValid()))
  {
    return Boost_Unsupported;
  }

  bool replaced = isActive();

  _type = type;
  _value = value;
  _started = time(NULL);
  _deadline_ms = 0;
  _energy_basis_wh = 0;

  if(LimitType::Time == type) {
    if(_value > BOOST_MAX_TIME_S) {
      _value = BOOST_MAX_TIME_S;
    }
    _deadline_ms = millis() + _value * 1000UL;
    if(0 == _deadline_ms) {
      _deadline_ms = 1; // 0 means "no deadline" to the timer math
    }
  } else if(LimitType::Energy == type) {
    _energy_basis_wh = (uint32_t)_evse->getSessionEnergy();
  }

  EvseProperties props(EvseState::Active);
  props.setAutoRelease(true);
  _evse->claim(EvseClient_OpenEVSE_Boost, EvseManager_Priority_Boost, props);

  _version++;
  sendEvent(true, replaced ? "replaced" : NULL);

  // Wake the task so an already-met absolute target releases immediately.
  MicroTask.wakeTask(this);

  DBUGF("Boost armed: %s %u", _type.toString(), _value);
  return Boost_Armed;
}

int Boost::arm(const char *json)
{
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, json);
  if(err || !doc.containsKey("type") || !doc.containsKey("value")) {
    return Boost_BadRequest;
  }
  LimitType type;
  type.fromString(doc["type"].as<const char *>());
  return arm(type, doc["value"].as<uint32_t>());
}

void Boost::endBoost(const char *reason)
{
  if(_evse->clientHasClaim(EvseClient_OpenEVSE_Boost)) {
    _evse->release(EvseClient_OpenEVSE_Boost);
  }
  _type = LimitType::None;
  _value = 0;
  _deadline_ms = 0;
  _energy_basis_wh = 0;
  _started = 0;
  _version++;
  sendEvent(false, reason);
}

bool Boost::cancel()
{
  if(!isActive()) {
    return false;
  }
  DBUGLN("Boost cancelled");
  endBoost("cancelled");
  return true;
}

uint32_t Boost::getRemaining()
{
  switch(_type) {
    case LimitType::Time:
      return deadline_timer_remaining_s(_deadline_ms, millis());
    case LimitType::Energy:
      return ChargeThreshold::remaining(_type, _value, _energy_basis_wh,
                                        (uint32_t)_evse->getSessionEnergy());
    case LimitType::Soc:
      return ChargeThreshold::remaining(_type, _value, 0,
                                        (uint32_t)_evse->getVehicleStateOfCharge());
    case LimitType::Range:
      return ChargeThreshold::remaining(_type, _value, 0,
                                        (uint32_t)_evse->getVehicleRange());
    default:
      return 0;
  }
}

void Boost::serialize(JsonDocument &doc)
{
  doc["type"] = _type.toString();
  doc["value"] = _value;
  doc["remaining"] = getRemaining();
  char buf[24];
  struct tm tm;
  gmtime_r(&_started, &tm);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  doc["started"] = buf;
}

unsigned long Boost::loop(MicroTasks::WakeReason reason)
{
  if(_sessionCompleteListener.IsTriggered() && isActive()) {
    // Vehicle gone: the boost's session is over. The Active claim has
    // auto-release set so EvseManager already dropped it; clear our state.
    DBUGLN("Session complete, ending boost");
    endBoost("cancelled");
  }

  if(!isActive()) {
    return MicroTask.Infinate;
  }

  bool reached = false;
  switch(_type) {
    case LimitType::Time:
      reached = deadline_timer_expired(_deadline_ms, millis());
      break;
    case LimitType::Energy:
      reached = ChargeThreshold::reached(_type, _value, _energy_basis_wh,
                                         (uint32_t)_evse->getSessionEnergy());
      break;
    case LimitType::Soc:
      reached = ChargeThreshold::reached(_type, _value, 0,
                                         (uint32_t)_evse->getVehicleStateOfCharge());
      break;
    case LimitType::Range:
      reached = ChargeThreshold::reached(_type, _value, 0,
                                         (uint32_t)_evse->getVehicleRange());
      break;
    default:
      break;
  }

  if(reached) {
    DBUGLN("Boost target reached, releasing");
    endBoost("reached");
    return MicroTask.Infinate;
  }

  return EVSE_BOOST_LOOP_TIME;
}
```

- [ ] **Step 3: Start the task in `src/main.cpp`**

Add the include near the other feature includes (next to `#include "limit.h"`):

```cpp
#include "boost.h"
```

After `limit.begin(evse);` (line ~187) add:

```cpp
  boost.begin(evse);
  DBUGF("After boost.begin: %d", ESPAL.getFreeHeap());
```

- [ ] **Step 4: Firmware compiles**

Run: `pio run -e openevse_wifi_tft_v1 2>&1 | tail -3`
Expected: `SUCCESS`. (Behavioral verification comes from Task 5's REST tests and Task 7's simulator scenarios — the pure decision math is already unit-tested by Tasks 1-2.)

- [ ] **Step 5: Native suites still green**

Run: `pio test -e native_test`
Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/boost.h src/boost.cpp src/main.cpp
git commit -m "feat: Boost module — priority-200 Active claim released on target"
```

---

### Task 5: HTTP API — `POST/GET/DELETE /boost`, `/status` fields, integration test

**Files:**
- Modify: `src/web_server.cpp` — new handlers after the Limit block (ends ~line 1131), registration next to `server.on("/limit", handleLimit)` (~line 1717), `/status` fields next to `doc["limit"]` (~line 618) and `doc["limit_version"]` (~line 627)
- Create: `tests/integration/test_boost.py`, `test/boost.http`

**Interfaces:**
- Consumes: `boost.arm(const char*)` / `cancel()` / `isActive()` / `serialize()` / `getVersion()` (Task 4); `requestPreProcess` + handler pattern from `handleLimit`.
- Produces: REST contract per Global Constraints (201/400/422; GET idle 200+`{}`; DELETE idle 404).

- [ ] **Step 1: Write the failing integration test**

Create `tests/integration/test_boost.py`:

```python
"""Integration tests for the /boost REST contract.

Runs against the paired emulator + native firmware fixtures from conftest.py
(same harness as test_charging.py).
"""

import requests


class TestBoostRest:
    def test_get_idle_returns_empty_object(self, base_url):
        """GET /boost with no boost active: 200 + {} (capability probe)."""
        r = requests.get(f"{base_url}/boost", timeout=10)
        assert r.status_code == 200
        assert r.json() == {}

    def test_delete_idle_is_404(self, base_url):
        r = requests.delete(f"{base_url}/boost", timeout=10)
        assert r.status_code == 404

    def test_post_malformed_is_400(self, base_url):
        r = requests.post(f"{base_url}/boost", data="not json", timeout=10)
        assert r.status_code == 400
        r = requests.post(f"{base_url}/boost", json={"type": "time"}, timeout=10)
        assert r.status_code == 400
        r = requests.post(f"{base_url}/boost", json={"type": "nope", "value": 10}, timeout=10)
        assert r.status_code == 400
        r = requests.post(f"{base_url}/boost", json={"type": "time", "value": 0}, timeout=10)
        assert r.status_code == 400

    def test_post_soc_without_vehicle_data_is_422(self, base_url):
        r = requests.post(f"{base_url}/boost", json={"type": "soc", "value": 80}, timeout=10)
        assert r.status_code == 422

    def test_time_boost_lifecycle(self, base_url):
        # Arm
        r = requests.post(f"{base_url}/boost", json={"type": "time", "value": 3600}, timeout=10)
        assert r.status_code == 201

        # Reported active with a counting-down remaining
        r = requests.get(f"{base_url}/boost", timeout=10)
        assert r.status_code == 200
        body = r.json()
        assert body["type"] == "time"
        assert body["value"] == 3600
        assert 0 < body["remaining"] <= 3600
        assert body["started"].endswith("Z")

        # /status reflects it
        status = requests.get(f"{base_url}/status", timeout=10).json()
        assert status["boost"] is True
        first_version = status["boost_version"]

        # Claim exists at the Boost priority
        claims = requests.get(f"{base_url}/claims", timeout=10).json()
        boost_claims = [c for c in claims if c.get("priority") == 200]
        assert len(boost_claims) == 1
        assert boost_claims[0]["properties"]["state"] == "active"

        # Re-POST replaces (version bumps)
        r = requests.post(f"{base_url}/boost", json={"type": "time", "value": 60}, timeout=10)
        assert r.status_code == 201
        status = requests.get(f"{base_url}/status", timeout=10).json()
        assert status["boost_version"] != first_version

        # Cancel
        r = requests.delete(f"{base_url}/boost", timeout=10)
        assert r.status_code == 200
        r = requests.get(f"{base_url}/boost", timeout=10)
        assert r.json() == {}
        status = requests.get(f"{base_url}/status", timeout=10).json()
        assert status["boost"] is False
        claims = requests.get(f"{base_url}/claims", timeout=10).json()
        assert not [c for c in claims if c.get("priority") == 200]

    def test_time_value_clamped_to_seven_days(self, base_url):
        r = requests.post(f"{base_url}/boost", json={"type": "time", "value": 100 * 24 * 3600}, timeout=10)
        assert r.status_code == 201
        body = requests.get(f"{base_url}/boost", timeout=10).json()
        assert body["value"] == 7 * 24 * 3600
        assert requests.delete(f"{base_url}/boost", timeout=10).status_code == 200
```

Before writing, check `tests/integration/conftest.py` for the actual fixture name serving the firmware base URL (`base_url` assumed above — if the existing `test_charging.py` uses a different fixture name, e.g. `firmware_url` or a class-level helper, use that name in this file instead; copy the import/usage pattern from `test_charging.py` exactly). Also confirm `/claims` response shape from `test_charging.py` or `src/web_server.cpp handleEvseClaims`; adjust the two claim asserts to the real field names if they differ.

- [ ] **Step 2: Run it to verify it fails**

```bash
cd tests/integration && python3 -m pytest test_boost.py -x -q; cd ../..
```
Expected: FAIL — `GET /boost` returns 404 (route not registered). (Requires Docker for the emulator fixture; if Docker is unavailable locally, note it and rely on the Step 6 curl checks + CI.)

- [ ] **Step 3: Add the handlers to `src/web_server.cpp`**

Add the include next to `#include "limit.h"` (line ~50):

```cpp
#include "boost.h"
```

Insert after the Limit section (after `handleLimit`'s closing brace, ~line 1131):

```cpp
//----------------------------------------------------------
//
//            Boost
//
//----------------------------------------------------------

void handleBoostGet(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  if(boost.isActive())
  {
    StaticJsonDocument<192> doc;
    boost.serialize(doc);
    response->setCode(200);
    serializeJson(doc, *response);
  } else {
    // 200 + {} doubles as the capability probe: old firmware 404s /boost.
    response->setCode(200);
    response->print("{}");
  }
}

void handleBoostPost(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  String body = request->body().toString();
  int rc = boost.arm(body.c_str());

  if(Boost_Armed == rc) {
    response->setCode(201);
    response->print("{\"msg\":\"done\"}");
  } else if(Boost_Unsupported == rc) {
    response->setCode(422);
    response->print("{\"msg\":\"no vehicle data source for this boost type\"}");
  } else {
    response->setCode(400);
    response->print("{\"msg\":\"failed to parse JSON\"}");
  }
}

void handleBoostDelete(MongooseHttpServerRequest *request, MongooseHttpServerResponseStream *response)
{
  if(boost.cancel()) {
    response->setCode(200);
    response->print("{\"msg\":\"done\"}");
  } else {
    response->setCode(404);
    response->print("{\"msg\":\"no boost\"}");
  }
}

void handleBoost(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }

  if(HTTP_GET == request->method()) {
    handleBoostGet(request, response);
  } else if(HTTP_POST == request->method()) {
    handleBoostPost(request, response);
  } else if(HTTP_DELETE == request->method()) {
    handleBoostDelete(request, response);
  } else {
    response->setCode(405);
    response->print("{\"msg\":\"Method not allowed\"}");
  }

  request->send(response);
}
```

Register next to `/limit` (~line 1717):

```cpp
  server.on("/boost", handleBoost);
```

Add the `/status` fields — next to `doc["limit"] = limit.hasLimit();` (~line 618):

```cpp
  doc["boost"] = boost.isActive();
```

and next to `doc["limit_version"] = limit.getVersion();` (~line 627):

```cpp
  doc["boost_version"] = boost.getVersion();
```

- [ ] **Step 4: Create `test/boost.http`** (manual REST recipes, pattern from `test/limit.http`)

```
### Get current boost (idle: 200 + {})
GET http://{{host}}/boost

### Arm a 1-hour time boost
POST http://{{host}}/boost
Content-Type: application/json

{"type": "time", "value": 3600}

### Arm an energy boost: 5 kWh more from now
POST http://{{host}}/boost
Content-Type: application/json

{"type": "energy", "value": 5000}

### Boost to 80% SoC (422 without a vehicle data source)
POST http://{{host}}/boost
Content-Type: application/json

{"type": "soc", "value": 80}

### Cancel (404 when idle)
DELETE http://{{host}}/boost
```

- [ ] **Step 5: Build**

Run: `pio run -e openevse_wifi_tft_v1 2>&1 | tail -3`
Expected: `SUCCESS`

- [ ] **Step 6: Run the integration test to verify it passes**

```bash
cd tests/integration && python3 -m pytest test_boost.py -x -q; cd ../..
```
Expected: PASS. Without Docker locally, substitute a native run + curl:

```bash
pio run -e native_openevse
# in another shell: .pio/build/native_openevse/program
curl -s http://localhost:8080/boost                                     # {}
curl -s -X POST -d '{"type":"time","value":3600}' http://localhost:8080/boost   # 201 done
curl -s http://localhost:8080/boost                                     # type/value/remaining/started
curl -s -X DELETE http://localhost:8080/boost                           # done
curl -s -o /dev/null -w '%{http_code}' -X DELETE http://localhost:8080/boost    # 404
```

- [ ] **Step 7: Commit**

```bash
git add src/web_server.cpp tests/integration/test_boost.py test/boost.http
git commit -m "feat(web): POST/GET/DELETE /boost + status boost fields"
```

---

### Task 6: MQTT + event parity

**Files:**
- Modify: `src/mqtt.h` (member + method decls beside the limit ones, ~lines 67, 117), `src/mqtt.cpp` (init ~line 63, subscribe ~line 341, resubscribe-reset ~line 358, version poll ~line 384, dispatcher ~line 518, publish/set methods ~line 709)

**Interfaces:**
- Consumes: `boost.arm(const char*)`, `cancel()`, `isActive()`, `serialize()`, `getVersion()` (Task 4).
- Produces: `<base>/boost/set` command topic; retained `<base>/boost` state topic republished on version change.

- [ ] **Step 1: Declarations in `src/mqtt.h`**

Add the include is NOT needed (`mqtt.h` already includes `limit.h`; add `#include "boost.h"` next to it). Beside `uint8_t _limitVersion;`-style members (find `_limit_props` at ~line 67) add:

```cpp
    uint8_t _boostVersion;
```

Beside `void setLimit(LimitProperties &limitProps);` (~line 117) add:

```cpp
    void publishBoost();
```

(No `setBoost` needed: `Boost::arm(const char*)` already parses JSON, unlike Limit which needs a properties object marshalled.)

- [ ] **Step 2: Wire `src/mqtt.cpp`**

Initialisation — beside `_limitVersion = limit.getVersion() == 0 ? 1 : limit.getVersion() -1;` (line ~63, and again at the reconnect reset ~line 358), add the same pattern so the first connect publishes current state:

```cpp
  _boostVersion = boost.getVersion() == 0 ? 1 : boost.getVersion() - 1;
```

Subscribe — beside `_mqttclient.subscribe(mqtt_topic + "/limit/set"); yield();` (~line 341):

```cpp
  _mqttclient.subscribe(mqtt_topic + "/boost/set"); yield();
```

Version poll — beside the `_limitVersion != limit.getVersion()` block (~line 384):

```cpp
  if (_boostVersion != boost.getVersion()) {
    publishBoost();
    DBUGLN("Boost has changed, publishing to MQTT");
    _boostVersion = boost.getVersion();
  }
```

Dispatcher — beside the `/limit/set` branch (~line 518):

```cpp
  else if (topic_string == mqtt_topic + "/boost/set") {
    if (payload_str.equals("off") || payload_str.equals("clear") || payload_str.length() == 0) {
      boost.cancel();  // version bump makes the poll republish
    } else if (Boost_Armed != boost.arm(payload_str.c_str())) {
      DBUGLN("MQTT boost/set rejected");
    }
  }
```

Publish — beside `Mqtt::publishLimit()` (~line 714):

```cpp
void Mqtt::publishBoost() {
  StaticJsonDocument<192> boost_data;
  if (boost.isActive()) {
    boost.serialize(boost_data);
  }
  // else: publish "{}" so a retained stale boost is cleared for HA
  String fulltopic = mqtt_topic + "/boost";
  String payload;
  serializeJson(boost_data, payload);
  _mqttclient.publish(fulltopic, payload, true);
}
```

(Serializing an empty StaticJsonDocument yields `null`, not `{}` — guard it: if `!boost.isActive()`, set `payload = "{}"` directly instead of serializing. Implement as:)

```cpp
void Mqtt::publishBoost() {
  String payload;
  if (boost.isActive()) {
    StaticJsonDocument<192> boost_data;
    boost.serialize(boost_data);
    serializeJson(boost_data, payload);
  } else {
    payload = "{}";
  }
  String fulltopic = mqtt_topic + "/boost";
  _mqttclient.publish(fulltopic, payload, true);
}
```

Use the second form only.

- [ ] **Step 3: Build**

Run: `pio run -e openevse_wifi_tft_v1 2>&1 | tail -3`
Expected: `SUCCESS`

- [ ] **Step 4: Full local suites**

```bash
pio test -e native_test
```
Expected: PASS (MQTT isn't covered by host tests; the build + Task 7's suites + bench HW cover it).

- [ ] **Step 5: Commit**

```bash
git add src/mqtt.h src/mqtt.cpp
git commit -m "feat(mqtt): boost/set command topic + retained boost state"
```

---

### Task 7: Simulator — build, scenario events, boost column, pytest scenarios

**Files:**
- Modify: `platformio.ini` (`[env:native_simulator]` `build_src_filter`, ~line 676)
- Modify: `divert_sim/RapiSender.cpp` (`$GG` handler, case `'G'`/`'G'`, ~line 135)
- Modify: `divert_sim/sim/sim_evse.h` (no change needed — `actualCurrent()` exists)
- Modify: `divert_sim/sim/scenario.h` (PeerEvent fields), `divert_sim/sim/scenario.cpp` (event parsing, ~line 163)
- Modify: `divert_sim/sim/peer.h` (own a `Boost`), `divert_sim/sim/peer.cpp` (begin + applyEvents)
- Modify: `divert_sim/sim/csv_writer.h` (append `boost` column), `divert_sim/sim/runner.cpp` (emit it, ~line 150)
- Create: `divert_sim/data/scenarios/boost_time_over_divert.json`, `divert_sim/data/scenarios/boost_energy_delta.json`, `divert_sim/data/scenarios/boost_manual_wins.json`
- Test: `divert_sim/test_boost.py`

**Interfaces:**
- Consumes: `Boost` class (Task 4) — per-peer instances like the existing `ManualOverride _manual`.
- Produces: scenario JSON events `{"time": N, "boost": {"type": "...", "value": N}}`, `{"time": N, "boost": "cancel"}`, `{"time": N, "manual": "disabled"|"active"|"release"}`; per-peer CSV column `boost` (0/1, appended last).

- [ ] **Step 1: Add the firmware objects to the sim build**

In `platformio.ini` `[env:native_simulator]` `build_src_filter`, after `+<manual.cpp>` add:

```ini
  +<limit.cpp>
  +<charge_threshold.cpp>
  +<boost.cpp>
```

`limit.cpp` comes along because `boost.cpp` uses `LimitType::fromString/toString` (defined there). Its global `Limit limit` is never `begin()`-ed in the sim and is inert; the process exits via `std::_Exit` so its destructor (which would dereference an un-begun `_evse`) never runs — `Boost`'s destructor is null-guarded (Task 4) because per-peer instances DO destruct.

Build check: `pio run -e native_simulator 2>&1 | tail -3` → expected `SUCCESS`. If `limit.cpp` drags in an unresolved config symbol, it will be `limit_default_type`/`limit_default_value` — both already defined in `app_config.cpp`, which the sim compiles, so this should link as-is; if anything else surfaces, stub it in `divert_sim/divert_sim.cpp` beside the existing `event_send` stubs.

- [ ] **Step 2: Make the sim report real charge current on `$GG`**

Energy boosts integrate `EvseManager::getSessionEnergy()`, which accumulates `getAmps() * getVoltage()` while charging (`src/energy_meter.cpp:218-250`) from the `$GG` poll. The sim's `$GG` currently returns zeros, so session energy never moves. In `divert_sim/RapiSender.cpp`, replace the `case 'G':` (inner, the `$GG` reply that answers `_tokens[1] = zero;`) with milliamps/millivolts derived from the model:

```cpp
        case 'G':
        {
          // Report the EV's actual draw so the firmware's energy meter
          // integrates a real session energy (energy boosts depend on it).
          static char buf_ma[16];
          static char buf_mv[16];
          sprintf(buf_ma, "%ld", (long)(sim->actualCurrent() * 1000.0));
          sprintf(buf_mv, "%ld", (long)(sim->voltage * 1000.0));
          _tokens[0] = ok;
          _tokens[1] = buf_ma;
          _tokens[2] = buf_mv;
          _tokenCnt = 3;
        } break;
```

Then run the existing sim suites unchanged:

```bash
pio run -e native_simulator
cd divert_sim && python3 -m pytest test_divert.py test_shaper.py test_config.py -q; cd ..
```
Expected: PASS — the existing assertions read SimEvse-derived CSV columns (`actual_charge_w`, `state`), not the firmware energy meter. A failure here means an existing scenario actually depends on zero amps: STOP and investigate before proceeding (do not tweak expected values to pass).

- [ ] **Step 3: Scenario events for boost + manual**

`divert_sim/sim/scenario.h` — extend `PeerEvent`:

```cpp
struct PeerEvent
{
  long t_sec;
  bool set_online = false;
  bool online = false;
  bool set_vehicle = false;
  bool vehicle = false;

  // {"boost": {"type": "time", "value": 900}} arms; {"boost": "cancel"} cancels.
  bool set_boost = false;
  bool boost_cancel = false;
  std::string boost_type;
  uint32_t boost_value = 0;

  // {"manual": "disabled"|"active"|"release"} drives the peer's ManualOverride
  // (priority 1000), which outranks Boost (200).
  bool set_manual = false;
  std::string manual_state;
};
```

`divert_sim/sim/scenario.cpp` — in the events loop (~line 163), after the `vehicle` branch:

```cpp
        if (ej.containsKey("boost")) {
          e.set_boost = true;
          if (ej["boost"].is<const char *>()) {
            e.boost_cancel = (std::string(ej["boost"].as<const char *>()) == "cancel");
          } else {
            JsonObjectConst bj = ej["boost"].as<JsonObjectConst>();
            e.boost_type = bj["type"] | "";
            e.boost_value = bj["value"] | 0;
          }
        }
        if (ej.containsKey("manual")) {
          e.set_manual = true;
          e.manual_state = ej["manual"].as<const char *>();
        }
```

- [ ] **Step 4: Per-peer Boost instance + event application**

`divert_sim/sim/peer.h`:
- add `#include "boost.h"` beside `#include "manual.h"`
- add accessor beside `shaper()`: `Boost &boost() { return _boost; }`
- add member beside `ManualOverride _manual;`: `Boost _boost;`

`divert_sim/sim/peer.cpp`:
- in `Peer::begin()`, after `_shaper.begin(_evse);`:

```cpp
  _boost.begin(_evse);
```

- in `Peer::applyEvents(long t_sec)` where events fire (follow the existing `set_online`/`set_vehicle` handling; add after them):

```cpp
    if (ev.set_boost) {
      if (ev.boost_cancel) {
        _boost.cancel();
      } else {
        LimitType type;
        type.fromString(ev.boost_type.c_str());
        _boost.arm(type, ev.boost_value);
      }
    }
    if (ev.set_manual) {
      if (ev.manual_state == "release") {
        _manual.release();
      } else {
        EvseProperties props(ev.manual_state == "disabled" ? EvseState::Disabled
                                                           : EvseState::Active);
        _manual.claim(props);
      }
    }
```

(Adapt member access to the real loop variable name in `applyEvents` — read the function first; it iterates `_scenario.events` from `_next_event_idx`.)

- [ ] **Step 5: CSV `boost` column**

`divert_sim/sim/csv_writer.h` — append to `columns::peerColumns()` (LAST entry, after `"soc"`):

```cpp
      "boost",
```

`divert_sim/sim/runner.cpp` — in the row-emit block, after `writer.addDouble(s.soc, 2);`:

```cpp
      writer.addBool(p->boost().isActive());
```

- [ ] **Step 6: Scenario files**

Look at an existing `divert_sim/data/scenarios/divert_*.json` first and copy its input-CSV reference style. Create `divert_sim/data/scenarios/boost_time_over_divert.json` — eco divert on a low-solar day, 15-min time boost armed mid-morning:

```json
{
  "simulation": { "duration": 7200, "tick_interval": 5, "start_time": "2020-03-22T08:00:00" },
  "config": { "divert_enabled": true, "charge_mode": 1, "divert_min_charge_time": 600 },
  "peers": [
    {
      "id": "evse-001",
      "divert_mode": "eco",
      "ev": { "battery_capacity_kwh": 75.0, "initial_soc": 20.0, "max_charge_rate_kw": 7.2 },
      "inputs": { "solar": { "constant": 300 } },
      "events": [
        { "time": 1800, "boost": { "type": "time", "value": 900 } }
      ]
    }
  ]
}
```

(`"constant"` input style: check `sim/time_series.cpp loadFromJson` for the accepted forms — if constants aren't supported, reference a low-solar CSV from `data/` instead, e.g. `{"csv": "../CloudyMorning.csv", "column": 1}` in whatever form the existing scenarios use. Resolve by copying a working scenario's `inputs` block. 300 W is below any 6 A/240 V minimum, so divert alone never charges.)

Create `divert_sim/data/scenarios/boost_energy_delta.json` — charging normally from t=0 (no divert), 500 Wh energy boost armed after energy has already accumulated:

```json
{
  "simulation": { "duration": 5400, "tick_interval": 5, "start_time": "2020-03-22T08:00:00" },
  "config": { "divert_enabled": false },
  "peers": [
    {
      "id": "evse-001",
      "ev": { "battery_capacity_kwh": 75.0, "initial_soc": 20.0, "max_charge_rate_kw": 7.2 },
      "events": [
        { "time": 1800, "boost": { "type": "energy", "value": 500 } }
      ]
    }
  ]
}
```

Create `divert_sim/data/scenarios/boost_manual_wins.json` — boost armed, then a Manual Disabled claim lands on top:

```json
{
  "simulation": { "duration": 3600, "tick_interval": 5, "start_time": "2020-03-22T08:00:00" },
  "config": { "divert_enabled": true, "charge_mode": 1, "divert_min_charge_time": 600 },
  "peers": [
    {
      "id": "evse-001",
      "divert_mode": "eco",
      "ev": { "battery_capacity_kwh": 75.0, "initial_soc": 20.0, "max_charge_rate_kw": 7.2 },
      "inputs": { "solar": { "constant": 300 } },
      "events": [
        { "time": 600,  "boost": { "type": "time", "value": 2400 } },
        { "time": 1200, "manual": "disabled" },
        { "time": 1800, "manual": "release" }
      ]
    }
  ]
}
```

- [ ] **Step 7: Write `divert_sim/test_boost.py`**

```python
#!/usr/bin/env python3
"""Boost module simulator tests: boost overrides divert, releases on target,
and loses to a Manual claim."""

from datetime import datetime

from run_simulations import run_scenario


def _t(row):
    return datetime.fromisoformat(row["time"].replace("Z", "+00:00"))


def _rows_between(rows, start_s, end_s):
    t0 = _t(rows[0])
    return [r for r in rows if start_s <= (_t(r) - t0).total_seconds() < end_s]


def _boost_active(row):
    return str(row.get("evse-001_boost", "0")).strip() in ("1", "true", "True")


def test_time_boost_overrides_divert_then_releases():
    rows = run_scenario("data/scenarios/boost_time_over_divert.json", "boost_time_over_divert")

    # Before the boost: solar (300 W) is far below the 6 A minimum, so eco
    # divert never starts a charge.
    before = _rows_between(rows, 300, 1800)
    assert before and all(r["evse-001_state"] != "charging" for r in before)
    assert all(not _boost_active(r) for r in before)

    # During the boost window (1800..2700): charging at full current.
    during = _rows_between(rows, 1810, 2690)
    assert during and all(_boost_active(r) for r in during)
    assert all(r["evse-001_state"] == "charging" for r in during)
    assert all(float(r["evse-001_actual_charge_w"]) > 1000 for r in during)

    # After the deadline: boost released, divert back in control, no charging.
    after = _rows_between(rows, 2760, 5400)
    assert after and all(not _boost_active(r) for r in after)
    assert all(r["evse-001_state"] != "charging" for r in after)


def test_energy_boost_releases_on_delta_not_session_total():
    rows = run_scenario("data/scenarios/boost_energy_delta.json", "boost_energy_delta")

    # The session had already delivered well over 500 Wh before t=1800
    # (7.2 kW for 30 min = 3.6 kWh). A session-total interpretation would
    # release instantly; the delta interpretation runs until 500 Wh MORE.
    armed = [r for r in rows if _boost_active(r)]
    assert armed, "boost never armed"
    t0 = _t(rows[0])
    first = (_t(armed[0]) - t0).total_seconds()
    last = (_t(armed[-1]) - t0).total_seconds()
    assert first >= 1800
    # 500 Wh at ~7.2 kW is ~250 s. Instant release (< 3 ticks) means the
    # delta basis is broken; hours means release never fired.
    assert 100 <= (last - first) <= 600


def test_manual_disabled_outranks_active_boost():
    rows = run_scenario("data/scenarios/boost_manual_wins.json", "boost_manual_wins")

    # Boost alone (600..1200): charging.
    during_boost = _rows_between(rows, 650, 1150)
    assert during_boost and all(r["evse-001_state"] == "charging" for r in during_boost)

    # Manual Disabled on top (1200..1800): boost still armed, but NOT charging.
    manual_window = _rows_between(rows, 1260, 1740)
    assert manual_window and all(_boost_active(r) for r in manual_window)
    assert all(r["evse-001_state"] != "charging" for r in manual_window)

    # Manual released (1800..3000): boost (still inside its 2400 s window)
    # resumes charging.
    resumed = _rows_between(rows, 1900, 2900)
    assert resumed and any(r["evse-001_state"] == "charging" for r in resumed)
```

Timing windows are deliberately slack (±60 s) around tick boundaries and the 1 Hz boost loop; if an assert trips on an off-by-one-tick edge, widen the slack — do not weaken the shape of the assertion (charging vs not, armed vs not).

- [ ] **Step 8: Run the boost sim tests**

```bash
pio run -e native_simulator
cd divert_sim && python3 -m pytest test_boost.py -x -q; cd ..
```
Expected: 3 PASS. Debug loop: run the binary directly for a CSV eyeball —
`.pio/build/native_simulator/program --scenario divert_sim/data/scenarios/boost_time_over_divert.json | head -30`.

- [ ] **Step 9: Full sim suite green**

```bash
cd divert_sim && python3 -m pytest -q; cd ..
```
Expected: all PASS (existing divert/shaper/config suites unaffected).

- [ ] **Step 10: Commit**

```bash
git add platformio.ini divert_sim/RapiSender.cpp divert_sim/sim/scenario.h divert_sim/sim/scenario.cpp divert_sim/sim/peer.h divert_sim/sim/peer.cpp divert_sim/sim/csv_writer.h divert_sim/sim/runner.cpp divert_sim/data/scenarios/boost_time_over_divert.json divert_sim/data/scenarios/boost_energy_delta.json divert_sim/data/scenarios/boost_manual_wins.json divert_sim/test_boost.py
git commit -m "feat(sim): boost scenarios — per-peer Boost, boost/manual events, boost CSV column"
```

---

### Task 8: API docs + final full validation

**Files:**
- Modify: `api.yml` (add `/boost` path beside the existing `/limit` path entry)

- [ ] **Step 1: Document `/boost` in `api.yml`**

Find the `/limit:` path entry and add a sibling (match the file's existing style/indentation and reuse its response-component patterns; adjust `$ref`s to whatever `/limit` uses):

```yaml
  /boost:
    get:
      summary: Get the active boost
      description: >
        Returns the active boost (type, value, remaining, started) or an empty
        object when no boost is active. An empty 200 doubles as the capability
        probe: firmware without boost support returns 404.
      tags: [Boost]
      responses:
        '200':
          description: Active boost, or {} when idle
          content:
            application/json:
              schema:
                type: object
                properties:
                  type: { type: string, enum: [time, energy, soc, range] }
                  value: { type: integer, description: "time: seconds (NOT minutes, unlike /limit); energy: Wh added since activation; soc: absolute %; range: absolute distance" }
                  remaining: { type: integer, description: Remaining amount in the dimension's unit (ceil) }
                  started: { type: string, format: date-time }
    post:
      summary: Arm (or replace) a boost
      description: >
        Boost charges NOW - an Active claim at priority 200 (above Divert and
        the Scheduler, below Manual and Limit) - until the target is reached,
        then releases so the previous controller resumes. Re-POST replaces the
        active boost with a fresh activation snapshot. Time values clamp to 7
        days.
      tags: [Boost]
      requestBody:
        required: true
        content:
          application/json:
            schema:
              type: object
              required: [type, value]
              properties:
                type: { type: string, enum: [time, energy, soc, range] }
                value: { type: integer, minimum: 1 }
      responses:
        '201': { description: Boost armed }
        '400': { description: Malformed body, unknown type, or zero value }
        '422': { description: soc/range boost without a vehicle data source }
    delete:
      summary: Cancel the active boost
      tags: [Boost]
      responses:
        '200': { description: Boost cancelled }
        '404': { description: No boost active }
```

- [ ] **Step 2: Full firmware build**

```bash
pio run -e openevse_wifi_tft_v1 2>&1 | tail -3
pio run -e openevse_wifi_v1 2>&1 | tail -3
```
Expected: both `SUCCESS` (the non-TFT env guards against a TFT-only include sneaking in; watch the 4 MB flash figure on `openevse_wifi_v1` and report it).

- [ ] **Step 3: Every suite, one pass**

```bash
pio test -e native_test
pio run -e native_simulator && cd divert_sim && python3 -m pytest -q && cd ..
cd tests/integration && python3 -m pytest -q; cd ../..
```
Expected: all PASS (integration needs Docker; if unavailable, state so — CI runs it on push).

- [ ] **Step 4: Commit**

```bash
git add api.yml
git commit -m "docs: document POST/GET/DELETE /boost in the API spec"
```

- [ ] **Step 5: Hand back for HW validation + PR**

Do NOT push or open the PR from this plan. Remaining after code-complete (per spec "Branch / PR / rollout"): bench HW validation (REST contract on real hardware, a real timed + energy boost, manual-beats-boost), then push `feature/boost`, open the upstream PR, and close #1195 with a pointer — all user-gated steps.

---

## Self-Review (completed)

- **Spec coverage:** semantics table → Task 4; shared layer + Limit refactor → Tasks 2-3; HTTP API incl. 201/400/422/404 + capability probe → Task 5; `/status`/events/MQTT parity → Tasks 4 (events) / 5 (status) / 6 (MQTT); simulator (build, events, column, 4 assertions — full-current during boost, time release + divert resumes, energy delta, manual outranks) → Task 7 (scenarios 1-2 are combined in `test_time_boost_overrides_divert_then_releases`); host tests for threshold + deadline math → Tasks 1-2; 7-day clamp → Tasks 4/5; no persistence/config → nothing added anywhere. SoC/Range sim scenarios intentionally absent: the sim has no vehicle-data feed; the threshold math is host-tested and the 422 path integration-tested.
- **Placeholder scan:** none. Two deliberately-conditional instructions remain (integration fixture name, scenario `inputs` constant-vs-CSV form) — each names the exact file to read and the exact fallback, which is an instruction, not a placeholder.
- **Type consistency:** `arm(LimitType, uint32_t)`/`arm(const char*)`/`Boost_Armed/-BadRequest/-Unsupported` used identically in Tasks 4-7; `ChargeThreshold::reached/remaining(uint8_t,uint32_t,uint32_t,uint32_t)` consistent across 2-4; `boost()` accessor name consistent between peer.h and runner.cpp; CSV column `boost` matches `evse-001_boost` in pytest.
