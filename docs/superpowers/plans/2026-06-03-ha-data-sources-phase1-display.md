# HA Data Sources — Phase 1 (Display-Only) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Poll two home-battery entities and two vehicle-extra entities from Home Assistant and surface them as display-only `/status` fields, by generalizing the existing vehicle poll chain into a reusable poll table.

**Architecture:** Replace the hardcoded `switch(field)` in `HomeAssistantClient::pollVehicleField` with a static poll table (`{entity, gate, type, sinkId}`) that the existing single-in-flight chain iterates. Value coercion is centralized by type (numeric / bool / string); numeric vehicle values keep flowing through `evse.setVehicle*()` unchanged, while the four new display-only values are held in the client and merged into `/status` via a new `addStatusFields()`. The new fields are **omitted when absent** (never emitted as `0`).

**Tech Stack:** C++ (Arduino-ESP32 core 2.x and ESP-IDF 5.x core 3.x), MicroTasks cooperative tasks, ArduinoJson 6, ArduinoMongoose HTTP client, doctest native unit tests.

**Spec:** `docs/superpowers/specs/2026-06-03-ha-data-sources-firmware-design.md`

**Working directory / branch:** `/home/rar/oevse/openevse_esp32_firmware`, branch `feature/ha-data-sources` (already created; the design doc is committed there as `934d807`).

**Scope of THIS plan (Phase 1 — display-only):** home-battery SoC/power + vehicle plugged/charging-state. The poll-table refactor lands here. Phase 2 (solar/grid/shaper control-path + MQTT arbitration + `divert_data_src`/`shaper_data_src`) is a separate plan.

**Key conventions to respect:**
- Pure, host-testable helpers live in `src/ha_oauth.cpp` — the `[env:native]` test build compiles **only** that file (`build_src_filter = -<*> +<ha_oauth.cpp>`), so anything tested by `pio test -e native` must be declared in `src/ha_oauth.h` and defined in `src/ha_oauth.cpp`.
- Config keys starting with `ha_` already auto-dispatch to `homeAssistant.notifyConfigChanged()` (`src/app_config.cpp:440`) — Phase 1 adds only `ha_*` keys, so no dispatch change is needed.
- `ha_parse_entity_state()` already rejects `""` / `unavailable` / `unknown` / non-string / bad-JSON (verified in `test/test_ha_oauth/test_ha_oauth.cpp`).
- Display-only fields are emitted into `/status` **only when a valid reading exists** — `0`/`false` are legitimate values, so use an explicit per-field "valid" flag (the same lesson as the energy-logger SoC `-1` sentinel), not a magic number.
- NVS short keys must be unique. Phase 1 uses `hbs`, `hbp`, `hvp`, `hcs` (all verified free).

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/ha_oauth.h` / `src/ha_oauth.cpp` | New pure helper `ha_parse_bool()` | 1 |
| `test/test_ha_oauth/test_ha_oauth.cpp` | Native tests for `ha_parse_bool` | 1 |
| `src/app_config.h` / `src/app_config.cpp` | 4 new `ha_*` String config keys | 2 |
| `src/home_assistant.h` / `src/home_assistant.cpp` | Poll-table refactor (Task 3); display-only members, new sinks, table rows, clear-on-disconnect (Task 4) | 3, 4 |
| `src/web_server.cpp` | `homeAssistant.addStatusFields(doc)` in `buildStatus()` | 5 |

---

## Task 1: `ha_parse_bool` pure helper + native tests

**Files:**
- Modify: `src/ha_oauth.h`
- Modify: `src/ha_oauth.cpp`
- Test: `test/test_ha_oauth/test_ha_oauth.cpp`

Maps a Home Assistant entity *state string* (already extracted and validated by `ha_parse_entity_state`) to a bool. Recognized truthy/falsey tokens set `out` and return `true`; anything unrecognized returns `false` (caller skips, keeping last good).

- [ ] **Step 1: Write the failing tests**

Append to `test/test_ha_oauth/test_ha_oauth.cpp`:
```cpp
TEST_CASE("ha_parse_bool recognizes truthy and falsey states") {
  bool b = false;
  CHECK(ha_parse_bool("on", b));        CHECK(b == true);
  CHECK(ha_parse_bool("ON", b));        CHECK(b == true);
  CHECK(ha_parse_bool("true", b));      CHECK(b == true);
  CHECK(ha_parse_bool("1", b));         CHECK(b == true);
  CHECK(ha_parse_bool("home", b));      CHECK(b == true);
  CHECK(ha_parse_bool("off", b));       CHECK(b == false);
  CHECK(ha_parse_bool("OFF", b));       CHECK(b == false);
  CHECK(ha_parse_bool("false", b));     CHECK(b == false);
  CHECK(ha_parse_bool("0", b));         CHECK(b == false);
  CHECK(ha_parse_bool("not_home", b));  CHECK(b == false);
  CHECK(ha_parse_bool("unplugged", b)); CHECK(b == false);
}

TEST_CASE("ha_parse_bool rejects unrecognized states") {
  bool b = true;
  CHECK_FALSE(ha_parse_bool("charging", b));
  CHECK_FALSE(ha_parse_bool("", b));
  CHECK_FALSE(ha_parse_bool("42.5", b));
  CHECK_FALSE(ha_parse_bool("maybe", b));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `pio test -e native`
Expected: FAIL to compile — `ha_parse_bool` is not declared.

- [ ] **Step 3: Declare the helper**

In `src/ha_oauth.h`, after the `ha_parse_entity_state` declaration (line ~47), add:
```cpp
// Map a Home Assistant state string (e.g. a binary_sensor's "on"/"off") to a bool.
// Recognized: on/true/1/home/open/yes -> true; off/false/0/not_home/closed/no/unplugged
// -> false (case-insensitive). Returns false (and leaves `out` untouched) for any
// other state, so callers skip it and keep the last good value.
bool ha_parse_bool(const std::string &state, bool &out);
```

- [ ] **Step 4: Implement the helper**

In `src/ha_oauth.cpp`, append:
```cpp
bool ha_parse_bool(const std::string &state, bool &out) {
  std::string s;
  s.reserve(state.size());
  for (char c : state) s += (char)tolower((unsigned char)c);

  if (s == "on" || s == "true" || s == "1" || s == "home" ||
      s == "open" || s == "yes" || s == "plugged" || s == "connected") {
    out = true;
    return true;
  }
  if (s == "off" || s == "false" || s == "0" || s == "not_home" ||
      s == "closed" || s == "no" || s == "unplugged" || s == "disconnected") {
    out = false;
    return true;
  }
  return false;
}
```
Ensure `#include <cctype>` is present near the top of `src/ha_oauth.cpp` (for `tolower`); add it if missing.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `pio test -e native`
Expected: PASS — all `ha_parse_bool` cases plus every existing `test_ha_oauth` case.

- [ ] **Step 6: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/ha_oauth.h src/ha_oauth.cpp test/test_ha_oauth/test_ha_oauth.cpp
git commit -m "feat(ha): ha_parse_bool helper for binary entity states

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Phase-1 config fields (4 `ha_*` entity strings)

**Files:**
- Modify: `src/app_config.h` (declarations, after `ha_vehicle_charge_limit` ~line 86)
- Modify: `src/app_config.cpp` (definitions ~line 142; `ConfigOptDefinition` registrations ~line 272)

No unit test — config plumbing is verified by a successful firmware build (Task 6). Dispatch is automatic because all four keys start with `ha_`.

- [ ] **Step 1: Declare the four new globals**

In `src/app_config.h`, immediately after `extern String ha_vehicle_charge_limit;` (line ~86), add:
```cpp
extern String ha_battery_soc;
extern String ha_battery_power;
extern String ha_vehicle_plugged;
extern String ha_vehicle_charging_state;
```

- [ ] **Step 2: Define the four globals**

In `src/app_config.cpp`, immediately after `String ha_vehicle_charge_limit;` (line ~142), add:
```cpp
String ha_battery_soc;
String ha_battery_power;
String ha_vehicle_plugged;
String ha_vehicle_charging_state;
```

- [ ] **Step 3: Register the four config options**

In `src/app_config.cpp`, immediately after the `ha_vehicle_charge_limit` registration line (`new ConfigOptDefinition<String>(ha_vehicle_charge_limit, "", "ha_vehicle_charge_limit", "hvc"),` ~line 272), add:
```cpp
  new ConfigOptDefinition<String>(ha_battery_soc, "", "ha_battery_soc", "hbs"),
  new ConfigOptDefinition<String>(ha_battery_power, "", "ha_battery_power", "hbp"),
  new ConfigOptDefinition<String>(ha_vehicle_plugged, "", "ha_vehicle_plugged", "hvp"),
  new ConfigOptDefinition<String>(ha_vehicle_charging_state, "", "ha_vehicle_charging_state", "hcs"),
```

- [ ] **Step 4: Verify it compiles (native config sanity)**

Run: `pio test -e native`
Expected: PASS — `app_config.cpp` isn't in the native build, but this confirms Task 1 still green and nothing in `ha_oauth` broke. (Full firmware compile of `app_config.cpp` happens in Task 6; if you want an early check, `pio run -e openevse_wifi_v1` also works here.)

- [ ] **Step 5: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/app_config.h src/app_config.cpp
git commit -m "feat(ha): config keys for home-battery + vehicle-extra entities

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Refactor the vehicle chain into a generic poll table (no behavior change)

**Files:**
- Modify: `src/home_assistant.h` (private section + add `pollNext`, remove `pollVehicleField`)
- Modify: `src/home_assistant.cpp` (`loop()` gating ~114-122; replace `pollVehicle`/`pollVehicleField` ~302-362)

This task is a pure structural refactor: it reproduces today's behavior (poll the 4 vehicle numeric entities when `vehicle_data_src == HOMEASSISTANT`) through a table-driven chain. No new feeds yet. Correctness is verified by build (Task 6) plus the reasoning that the table's vehicle rows map 1:1 to the old `switch`.

- [ ] **Step 1: Add the poll-table types and declarations to the header**

In `src/home_assistant.h`, inside `class HomeAssistantClient`, in the `private:` section, replace:
```cpp
    unsigned long _lastVehiclePoll;
    bool _vehicleInFlight;
    unsigned long _vehiclePollStart;

    void exchangeCode(const String &code);
    void refreshTokens();
    void storeTokens(const HaTokens &t);
    void pollVehicle();
    void pollVehicleField(int field); // 0=soc, 1=range, 2=eta; chains to the next
```
with:
```cpp
    unsigned long _lastPoll;
    bool _pollInFlight;
    unsigned long _pollStart;

    void exchangeCode(const String &code);
    void refreshTokens();
    void storeTokens(const HaTokens &t);

    bool anyPollActive();          // true if at least one table row is active+configured
    void pollNext(int index);      // walk the poll table from `index`
    void applyEntity(int sinkId, int type, const String &state);
```
Also rename the constructor-initialised members in `src/home_assistant.cpp` (Step 3) to match.

- [ ] **Step 2: Add the value-type and sink enums + the table (top of `home_assistant.cpp`)**

In `src/home_assistant.cpp`, after the `#include` block and before `HomeAssistantClient homeAssistant;` (~line 25), add:
```cpp
enum HaValueType { HA_NUMERIC, HA_BOOL, HA_STRING };

enum HaSink {
  SINK_VEHICLE_SOC = 0,
  SINK_VEHICLE_RANGE,
  SINK_VEHICLE_ETA,
  SINK_VEHICLE_CHARGE_LIMIT,
};

struct HaPollEntry {
  const String *entity;   // config string; empty => skip
  bool (*gate)();         // is this row active right now?
  int  type;              // HaValueType
  int  sinkId;            // HaSink
};

static bool gateVehicleHA() {
  return vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT;
}

static const HaPollEntry HA_POLL_TABLE[] = {
  { &ha_vehicle_soc,          gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_SOC },
  { &ha_vehicle_range,        gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_RANGE },
  { &ha_vehicle_eta,          gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_ETA },
  { &ha_vehicle_charge_limit, gateVehicleHA, HA_NUMERIC, SINK_VEHICLE_CHARGE_LIMIT },
};
static const int HA_POLL_TABLE_LEN = sizeof(HA_POLL_TABLE) / sizeof(HA_POLL_TABLE[0]);
```

- [ ] **Step 3: Update the constructor initialisers**

In `src/home_assistant.cpp`, in the `HomeAssistantClient::HomeAssistantClient()` initialiser list (~32-40), replace:
```cpp
  _lastVehiclePoll(0),
  _vehicleInFlight(false),
  _vehiclePollStart(0)
```
with:
```cpp
  _lastPoll(0),
  _pollInFlight(false),
  _pollStart(0)
```

- [ ] **Step 4: Rewrite the `loop()` poll-trigger block**

In `src/home_assistant.cpp` `loop()`, replace the stuck-chain recovery + vehicle-poll trigger block (~106-122):
```cpp
  // Recover a stuck vehicle-poll chain (e.g. a request that never completed and
  // no onResponse fired) -- otherwise _vehicleInFlight would block polling forever.
  if (_vehicleInFlight && _vehiclePollStart != 0 &&
      (millis() - _vehiclePollStart) > HA_VEHICLE_TIMEOUT_MS) {
    DBUGLN("[ha] vehicle poll timed out, clearing in-flight flag");
    _vehicleInFlight = false;
  }

  // Poll HA vehicle entities when HA is the selected vehicle-data source.
  if (isConnected()
      && vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT
      && !_vehicleInFlight
      && (_lastVehiclePoll == 0 || (millis() - _lastVehiclePoll) >= HA_VEHICLE_POLL_MS)) {
    _lastVehiclePoll = millis();
    if (_lastVehiclePoll == 0) _lastVehiclePoll = 1; // 0 means "never polled"
    pollVehicle();
  }
```
with:
```cpp
  // Recover a stuck poll chain (a request that never completed and no onResponse
  // fired) -- otherwise _pollInFlight would block polling forever.
  if (_pollInFlight && _pollStart != 0 &&
      (millis() - _pollStart) > HA_VEHICLE_TIMEOUT_MS) {
    DBUGLN("[ha] poll chain timed out, clearing in-flight flag");
    _pollInFlight = false;
  }

  // Poll HA entities when connected and at least one configured feed selects HA.
  if (isConnected()
      && anyPollActive()
      && !_pollInFlight
      && (_lastPoll == 0 || (millis() - _lastPoll) >= HA_VEHICLE_POLL_MS)) {
    _lastPoll = millis();
    if (_lastPoll == 0) _lastPoll = 1; // 0 means "never polled"
    _pollInFlight = true;
    _pollStart = millis();
    pollNext(0);
  }
```

- [ ] **Step 5: Replace `pollVehicle`/`pollVehicleField` with the generic chain**

In `src/home_assistant.cpp`, delete the entire `pollVehicle()` and `pollVehicleField(int field)` definitions (~302-362) and replace with:
```cpp
bool HomeAssistantClient::anyPollActive() {
  for (int i = 0; i < HA_POLL_TABLE_LEN; i++) {
    const HaPollEntry &e = HA_POLL_TABLE[i];
    if (e.entity->length() > 0 && e.gate()) return true;
  }
  return false;
}

// Apply one parsed entity state to its sink, dispatched by sinkId.
void HomeAssistantClient::applyEntity(int sinkId, int type, const String &state) {
  if (type == HA_NUMERIC) {
    char *endp = nullptr;
    double v = strtod(state.c_str(), &endp);
    if (endp == state.c_str()) {
      DBUGF("[ha] sink %d: non-numeric state, skipping", sinkId);
      return;
    }
    switch (sinkId) {
      case SINK_VEHICLE_SOC:          evse.setVehicleStateOfCharge((int)lround(v)); break;
      case SINK_VEHICLE_RANGE:        evse.setVehicleRange((int)lround(v));         break;
      case SINK_VEHICLE_ETA:          evse.setVehicleEta((int)lround(v));           break;
      case SINK_VEHICLE_CHARGE_LIMIT: evse.setVehicleChargeLimit((int)lround(v));   break;
      default: break;
    }
  }
  // HA_BOOL and HA_STRING sinks are added in Task 4.
}

// Fetch one active+configured entity, apply it, then chain to the next. Sequential
// (one in-flight request at a time) to stay safe on the shared MongooseHttpClient.
// An empty entity, inactive gate, failed request, or unparseable state simply skips
// that row (keeps the last good value).
void HomeAssistantClient::pollNext(int index) {
  for (int i = index; i < HA_POLL_TABLE_LEN; i++) {
    const HaPollEntry &e = HA_POLL_TABLE[i];
    if (e.entity->length() == 0 || !e.gate()) {
      continue; // not configured / not active -> skip
    }

    String path = "/api/states/" + *e.entity; // entity IDs are URL-safe (sensor.x_y)
    int next = i + 1;
    int sinkId = e.sinkId;
    int type = e.type;
    bool sent = get(path, [this, sinkId, type, next](MongooseHttpClientResponse *response) {
      if (response->respCode() == 200) {
        MongooseString body = response->body();
        std::string state;
        if (ha_parse_entity_state(std::string((const char *)body, body.length()), state)) {
          applyEntity(sinkId, type, String(state.c_str()));
        } else {
          DBUGF("[ha] sink %d: state unavailable/unparseable", sinkId);
        }
      } else {
        DBUGF("[ha] sink %d: HTTP %d", sinkId, response->respCode());
      }
      pollNext(next); // advance regardless of this row's outcome
    });

    if (!sent) {
      // get() declined (refresh due / not connected): onResponse won't fire, so end
      // the chain now rather than stranding _pollInFlight until the timeout.
      _pollInFlight = false;
    }
    return; // one in-flight request at a time; the callback resumes the walk
  }

  // Walked off the end with nothing left to send -> chain complete.
  _pollInFlight = false;
}
```

- [ ] **Step 6: Build to verify the refactor compiles**

Run: `pio run -e openevse_wifi_v1`
Expected: SUCCESS (links). This is the core-2.x board; it exercises the default (non-P4) code path the same as the S3/standard builds.

- [ ] **Step 7: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/home_assistant.h src/home_assistant.cpp
git commit -m "refactor(ha): generic poll table replaces hardcoded vehicle chain

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Wire the four display-only feeds (bool/string coercion + new sinks + rows)

**Files:**
- Modify: `src/home_assistant.h` (public `addStatusFields` + private display-only members)
- Modify: `src/home_assistant.cpp` (sink enum, gates, table rows, `applyEntity` bool/string branches, `addStatusFields`, clear-on-disconnect)

- [ ] **Step 1: Add the display-only members and the status method to the header**

In `src/home_assistant.h`, in the `public:` section after `void getStatus(JsonDocument &doc);` (line ~18), add:
```cpp
    void addStatusFields(JsonDocument &doc); // merge display-only HA values into /status
```
In the `private:` section, after the `_pollStart;` member, add:
```cpp
    // Display-only values polled from HA (omitted from /status until a valid read).
    int    _homeBatterySoc = 0;       bool _homeBatterySocValid = false;
    int    _homeBatteryPower = 0;     bool _homeBatteryPowerValid = false;
    bool   _vehiclePlugged = false;   bool _vehiclePluggedValid = false;
    String _vehicleChargingState;     // empty => absent
```

- [ ] **Step 2: Extend the sink enum and add gates + table rows**

In `src/home_assistant.cpp`, extend the `HaSink` enum:
```cpp
enum HaSink {
  SINK_VEHICLE_SOC = 0,
  SINK_VEHICLE_RANGE,
  SINK_VEHICLE_ETA,
  SINK_VEHICLE_CHARGE_LIMIT,
  SINK_VEHICLE_PLUGGED,
  SINK_VEHICLE_CHARGING_STATE,
  SINK_HOME_BATTERY_SOC,
  SINK_HOME_BATTERY_POWER,
};
```
Add a second gate next to `gateVehicleHA()`:
```cpp
// Home-battery feeds poll whenever the entity is configured; the loop already
// gates on isConnected(), which implies HA is enabled and authorized.
static bool gateAlways() { return true; }
```
Add the four rows to `HA_POLL_TABLE` (after the vehicle rows, before the closing `};`):
```cpp
  { &ha_vehicle_plugged,         gateVehicleHA, HA_BOOL,    SINK_VEHICLE_PLUGGED },
  { &ha_vehicle_charging_state,  gateVehicleHA, HA_STRING,  SINK_VEHICLE_CHARGING_STATE },
  { &ha_battery_soc,             gateAlways,    HA_NUMERIC, SINK_HOME_BATTERY_SOC },
  { &ha_battery_power,           gateAlways,    HA_NUMERIC, SINK_HOME_BATTERY_POWER },
```

- [ ] **Step 3: Extend `applyEntity` for the new numeric sinks + bool + string**

In `src/home_assistant.cpp` `applyEntity`, add the two home-battery cases to the existing `HA_NUMERIC` switch:
```cpp
      case SINK_HOME_BATTERY_SOC:
        _homeBatterySoc = (int)lround(v); _homeBatterySocValid = true; break;
      case SINK_HOME_BATTERY_POWER:
        _homeBatteryPower = (int)lround(v); _homeBatteryPowerValid = true; break;
```
Then replace the trailing comment `// HA_BOOL and HA_STRING sinks are added in Task 4.` with:
```cpp
  else if (type == HA_BOOL) {
    bool b;
    if (!ha_parse_bool(std::string(state.c_str()), b)) {
      DBUGF("[ha] sink %d: unrecognized bool state, skipping", sinkId);
      return;
    }
    switch (sinkId) {
      case SINK_VEHICLE_PLUGGED: _vehiclePlugged = b; _vehiclePluggedValid = true; break;
      default: break;
    }
  }
  else if (type == HA_STRING) {
    // ha_parse_entity_state already rejected ""/unavailable/unknown, so `state` is real.
    switch (sinkId) {
      case SINK_VEHICLE_CHARGING_STATE: _vehicleChargingState = state; break;
      default: break;
    }
  }
```
(The `if (type == HA_NUMERIC) { ... }` block must close before this `else if` chain; keep the `return` inside the numeric non-numeric-skip path so it doesn't fall through.)

- [ ] **Step 4: Implement `addStatusFields` (omit-when-absent)**

In `src/home_assistant.cpp`, add (e.g. after `getStatus`):
```cpp
void HomeAssistantClient::addStatusFields(JsonDocument &doc) {
  if (_homeBatterySocValid)   doc["home_battery_soc"] = _homeBatterySoc;
  if (_homeBatteryPowerValid) doc["home_battery_power"] = _homeBatteryPower;
  if (_vehiclePluggedValid)   doc["vehicle_plugged"] = _vehiclePlugged;
  if (_vehicleChargingState.length() > 0)
    doc["vehicle_charging_state"] = _vehicleChargingState;
}
```

- [ ] **Step 5: Clear display-only values on disconnect**

In `src/home_assistant.cpp` `disconnect()`, after the existing token/state clears (before the `event_send`), add:
```cpp
  _homeBatterySocValid = false;
  _homeBatteryPowerValid = false;
  _vehiclePluggedValid = false;
  _vehicleChargingState = "";
```

- [ ] **Step 6: Build to verify it compiles**

Run: `pio run -e openevse_wifi_v1`
Expected: SUCCESS (links). Confirms the bool/string branches, new members, and table rows all compile.

- [ ] **Step 7: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/home_assistant.h src/home_assistant.cpp
git commit -m "feat(ha): poll home-battery + vehicle plugged/charging-state (display-only)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Surface the display-only fields in `/status`

**Files:**
- Modify: `src/web_server.cpp` (include + `buildStatus()` ~302)

- [ ] **Step 1: Include the HA client header**

In `src/web_server.cpp`, add to the include block (near the other module includes, e.g. after `#include "current_shaper.h"` line ~44):
```cpp
#include "home_assistant.h"
```

- [ ] **Step 2: Call `addStatusFields` in `buildStatus()`**

In `src/web_server.cpp` `buildStatus()`, immediately after the vehicle `else { ... }` block closes (line ~302, right before `DBUGF("/status ArduinoJson size: %dbytes", doc.size());`), add:
```cpp
  homeAssistant.addStatusFields(doc);
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e openevse_wifi_v1`
Expected: SUCCESS (links).

- [ ] **Step 4: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/web_server.cpp
git commit -m "feat(ha): emit home-battery + vehicle-extra fields in /status

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Full build + hardware validation on the P4

**Files:** none (build/flash/validate only).

The P4 is the bench board with a working HA connection (used for VD/CL HW validation). It runs the core-3.x build and the gui-nightshift bundle.

- [ ] **Step 1: Build both target cores**

Run:
```bash
cd /home/rar/oevse/openevse_esp32_firmware
pio run -e openevse_wifi_v1            # core-2.x, default path
GUI_NAME=gui-nightshift pio run -e openevse_p4   # core-3.x, P4 bench board
```
Expected: both SUCCESS. (If the P4 build throws a transient `FRAMEWORK_DIR is None` on a fresh framework install, re-run — see the core-3 migration notes.)

- [ ] **Step 2: Flash the P4**

Run: `GUI_NAME=gui-nightshift pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5`
Expected: upload completes, board reboots.

- [ ] **Step 3: Configure the four entities (one of two ways)**

Either set them in the gui-nightshift Solar/Vehicle settings pages (once the GUI branch is flashed), or POST config directly:
```bash
curl -s -X POST http://10.75.1.143/config \
  -H 'Content-Type: application/json' \
  -d '{"ha_battery_soc":"sensor.home_battery_soc","ha_battery_power":"sensor.home_battery_power","ha_vehicle_plugged":"binary_sensor.car_plugged_in","ha_vehicle_charging_state":"sensor.car_charging_state"}'
```
Use entity IDs that actually exist on the bench HA instance (substitute real ones). `vehicle_data_src` must be `4` (HOMEASSISTANT) for the two vehicle-extra rows to be active; the home-battery rows poll regardless.

- [ ] **Step 4: Confirm the fields appear in `/status`**

Wait ~30 s for a poll cycle, then:
```bash
curl -s http://10.75.1.143/status | python3 -m json.tool | grep -E "home_battery_soc|home_battery_power|vehicle_plugged|vehicle_charging_state"
```
Expected: the configured fields appear with live values. **Crucially**, before configuring (or for an unconfigured field), confirm the field is **absent** from `/status` — not present as `0`/`false`.

- [ ] **Step 5: Confirm a bad/unavailable entity is handled**

Temporarily set one entity to a nonexistent ID (e.g. `sensor.does_not_exist`), wait a cycle, and confirm `/status` keeps the **last good** value (or stays absent if never read) rather than emitting `0` — and that the poll chain keeps running for the other fields (check the serial log via `pio device monitor` for `HTTP 404`/`unavailable` skips without a stuck chain).

- [ ] **Step 6: Record validation result**

No commit (validation only). Note the observed `/status` values and absent-when-unconfigured behavior in the task tracker / PR description.

---

## Final Verification

- [ ] `pio test -e native` — all green (`ha_parse_bool` + existing).
- [ ] `pio run -e openevse_wifi_v1` and `GUI_NAME=gui-nightshift pio run -e openevse_p4` — both build clean.
- [ ] `/status` on the P4 shows the four fields with live values when configured, and **omits** each field when its entity is unconfigured/unavailable (never `0`/`false`).
- [ ] Vehicle numeric values (`battery_level`, `battery_range`, `time_to_full_charge`, `vehicle_charge_limit`) still behave exactly as before the refactor (the poll table reproduces the old chain).
- [ ] Backward compatibility: with none of the new `ha_*` keys set (all default `""`), the new rows are skipped and `/status` is unchanged from today.
