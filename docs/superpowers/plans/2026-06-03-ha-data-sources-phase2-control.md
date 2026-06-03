# HA Data Sources — Phase 2 (Control-Path) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the eco-divert solar/grid feeds and the shaper whole-home live-power feed be sourced from Home Assistant entities (the GUI already offers this; the firmware config keys don't exist yet, so the GUI's writes silently don't persist).

**Architecture:** Add the control-path config keys (`divert_data_src`, `shaper_data_src`, `ha_solar`, `ha_grid_ie`, `ha_live_pwr`) and three more rows to the existing HA poll table whose sinks write the SAME globals/objects the MQTT path writes (`solar`/`grid_ie` globals + `shaper.setLivePwr()`), triggering the same divert/shaper recalcs. MQTT⇄HA arbitration mirrors the existing vehicle pattern — the MQTT message handlers are guarded by a per-feature data-source check, so when HA is the source the MQTT message is ignored (no source thrash).

**Tech Stack:** C++ (Arduino-ESP32 core 2.x + ESP-IDF 5.x core 3.x), MicroTasks, ArduinoJson 6, ArduinoMongoose.

**Spec:** `docs/superpowers/specs/2026-06-03-ha-data-sources-firmware-design.md` (Phase 2 sections). **Phase 1 (display-only) is already merged** to `feature/esp32-modernization` — the poll table, `pollNext`, `applyEntity`, gates (`gateVehicleHA`/`gateAlways`), and `HaSink` enum already exist in `src/home_assistant.cpp`.

**Working directory / branch:** `/home/rar/oevse/openevse_esp32_firmware`, branch `feature/ha-data-sources` (currently == `feature/esp32-modernization` at `671f32a`).

**Key conventions:**
- `0` = MQTT (default), `1` = Home Assistant for both `*_data_src` keys. A new `enum data_src { DATA_SRC_MQTT = 0, DATA_SRC_HOMEASSISTANT = 1 };` makes this readable.
- The `*_data_src` keys do NOT start with `ha_`, so they need an explicit line in the config-change dispatcher (`app_config.cpp`).
- **Arbitration = handler guard, NOT subscription teardown.** The vehicle MQTT handlers already use `&& vehicle_data_src == VEHICLE_DATA_SRC_MQTT` (`mqtt.cpp`); mirror that exactly. This needs no MQTT reconnect.
- Short keys (verified free): `divert_data_src`=`dvs` (NB `dds` is taken by `divert_decay_smoothing_time`), `shaper_data_src`=`shs`, `ha_solar`=`hso`, `ha_grid_ie`=`hgi`, `ha_live_pwr`=`hlp`.
- No new pure helpers → no new native tests; the control rows reuse the proven `HA_NUMERIC` path. Verification is build + HW (Task 4), consistent with Phase 1's integration tasks.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `src/app_config.h` / `src/app_config.cpp` | 5 new config keys + `data_src` enum + dispatch line | 1 |
| `src/home_assistant.cpp` | 3 new sinks + gates + table rows + applyEntity cases (+ divert/shaper includes) | 2 |
| `src/mqtt.cpp` | guard solar/grid/live-pwr handlers (+ subscriptions) on `*_data_src == MQTT` | 3 |
| — | full build (core-2 + P4) + HW validation | 4 |

(`home_assistant.h` needs no signature changes — the new sinks reuse `applyEntity(int,int,String)`.)

---

## Task 1: Control-path config keys + `data_src` enum + dispatch

**Files:**
- Modify: `src/app_config.h`
- Modify: `src/app_config.cpp`

- [ ] **Step 1: Add the `data_src` enum + extern declarations**

In `src/app_config.h`, immediately AFTER the existing `enum vehicle_data_src { ... };` block (ends ~line 132), add:
```cpp
// Per-feature data source for divert/shaper feeds (solar, grid, live power).
enum data_src {
  DATA_SRC_MQTT = 0,          // default: MQTT topic (existing behavior)
  DATA_SRC_HOMEASSISTANT = 1, // poll a Home Assistant entity instead
};

extern uint8_t divert_data_src;
extern uint8_t shaper_data_src;
extern String ha_solar;
extern String ha_grid_ie;
extern String ha_live_pwr;
```

- [ ] **Step 2: Define the globals**

In `src/app_config.cpp`, immediately after the `String ha_vehicle_charging_state;` definition (added in Phase 1, ~line 146), add:
```cpp
uint8_t divert_data_src;
uint8_t shaper_data_src;
String ha_solar;
String ha_grid_ie;
String ha_live_pwr;
```

- [ ] **Step 3: Register the config options**

In `src/app_config.cpp`, immediately after the `ha_vehicle_charging_state` `ConfigOptDefinition` line (the `"hvg"` entry added in Phase 1), add:
```cpp
  new ConfigOptDefinition<uint8_t>(divert_data_src, 0, "divert_data_src", "dvs"),
  new ConfigOptDefinition<uint8_t>(shaper_data_src, 0, "shaper_data_src", "shs"),
  new ConfigOptDefinition<String>(ha_solar, "", "ha_solar", "hso"),
  new ConfigOptDefinition<String>(ha_grid_ie, "", "ha_grid_ie", "hgi"),
  new ConfigOptDefinition<String>(ha_live_pwr, "", "ha_live_pwr", "hlp"),
```

- [ ] **Step 4: Notify HA when a source flips**

In `src/app_config.cpp`, find the dispatch branch (~line 448):
```cpp
  } else if(name.startsWith("ha_") || name == "home_assistant_enabled" || name == "vehicle_data_src") {
    homeAssistant.notifyConfigChanged();
```
and replace its condition with:
```cpp
  } else if(name.startsWith("ha_") || name == "home_assistant_enabled" || name == "vehicle_data_src"
            || name == "divert_data_src" || name == "shaper_data_src") {
    homeAssistant.notifyConfigChanged();
```
(The MQTT side reads `divert_data_src`/`shaper_data_src` live on each message, so it needs no notification; only the HA poll task must re-arm.)

- [ ] **Step 5: Verify short-key uniqueness + build**

Run: `grep -oE '"[a-z0-9]{1,3}"\)' src/app_config.cpp | sort | uniq -d` — must print NOTHING (no duplicate short keys).
Run: `pio run -e openevse_wifi_v1` — expect SUCCESS. (Transient `FRAMEWORK_DIR is None` → re-run once.)

- [ ] **Step 6: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/app_config.h src/app_config.cpp
git commit -m "feat(ha): control-path config keys (divert/shaper data source + entities)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: HA poll sinks for solar / grid / shaper-live-power

**Files:**
- Modify: `src/home_assistant.cpp`

Adds three rows to the existing poll table and the matching `applyEntity` sinks, writing the SAME globals/objects the MQTT handlers write and triggering the identical divert/shaper recalcs.

- [ ] **Step 1: Add the divert + shaper includes**

In `src/home_assistant.cpp`, after the existing `#include "input.h"` line (~line 15), add:
```cpp
#include "divert.h"          // global `solar`/`grid_ie` ints + `divert`; DIVERT_TYPE_*
#include "current_shaper.h"  // global `shaper`
```

- [ ] **Step 2: Extend the `HaSink` enum**

In `src/home_assistant.cpp`, add three values to the end of the `HaSink` enum (before the closing `};`):
```cpp
  SINK_SOLAR,
  SINK_GRID_IE,
  SINK_SHAPER_LIVE_PWR,
```

- [ ] **Step 3: Add the three gate functions**

In `src/home_assistant.cpp`, immediately after the existing `gateAlways()` definition, add:
```cpp
// Control-path feeds mirror the MQTT subscription conditions: only active when the
// feature is enabled, the source is HA, and (for divert) the divert type matches.
static bool gateSolarHA() {
  return config_divert_enabled() && divert_data_src == DATA_SRC_HOMEASSISTANT
         && divert_type == DIVERT_TYPE_SOLAR;
}
static bool gateGridHA() {
  return config_divert_enabled() && divert_data_src == DATA_SRC_HOMEASSISTANT
         && divert_type == DIVERT_TYPE_GRID;
}
static bool gateShaperHA() {
  return config_current_shaper_enabled() && shaper_data_src == DATA_SRC_HOMEASSISTANT;
}
```

- [ ] **Step 4: Add the three table rows**

In `src/home_assistant.cpp`, add these rows to `HA_POLL_TABLE` after the existing `ha_battery_power` row (before the closing `};`):
```cpp
  { &ha_solar,    gateSolarHA,  HA_NUMERIC, SINK_SOLAR },
  { &ha_grid_ie,  gateGridHA,   HA_NUMERIC, SINK_GRID_IE },
  { &ha_live_pwr, gateShaperHA, HA_NUMERIC, SINK_SHAPER_LIVE_PWR },
```

- [ ] **Step 5: Add the three sinks to `applyEntity`**

In `src/home_assistant.cpp` `applyEntity`, in the `HA_NUMERIC` `switch (sinkId)` block, add these cases (before `default: break;`). They mirror the MQTT handlers in `mqtt.cpp` (solar/grid drive divert + a shaper recompute; live-power feeds the shaper):
```cpp
      case SINK_SOLAR:
        solar = (int)lround(v);
        divert.update_state();
        if (shaper.getState()) shaper.shapeCurrent();
        break;
      case SINK_GRID_IE:
        grid_ie = (int)lround(v);
        divert.update_state();
        break;
      case SINK_SHAPER_LIVE_PWR:
        shaper.setLivePwr((int)lround(v));
        break;
```

- [ ] **Step 6: Build**

Run: `pio run -e openevse_wifi_v1` — expect SUCCESS (links). If `solar`/`grid_ie`/`divert`/`shaper`/`DIVERT_TYPE_*`/`config_divert_enabled` are unresolved, recheck the Step 1 includes. (Transient `FRAMEWORK_DIR is None` → re-run once.)

- [ ] **Step 7: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/home_assistant.cpp
git commit -m "feat(ha): poll solar/grid/live-power from HA into divert + shaper

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: MQTT⇄HA arbitration (handler guards)

**Files:**
- Modify: `src/mqtt.cpp`

When a feed's source is HA, the MQTT message for that feed must be ignored so the two sources don't fight. Mirror the existing vehicle pattern (`&& vehicle_data_src == VEHICLE_DATA_SRC_MQTT`). Also gate the matching subscriptions so HA-sourced feeds don't needlessly subscribe.

- [ ] **Step 1: Guard the message handlers**

In `src/mqtt.cpp`, in the message-handling chain (~line 299), change the three handler conditions.

Change:
```cpp
  if (topic_string == mqtt_solar){
```
to:
```cpp
  if (topic_string == mqtt_solar && divert_data_src == DATA_SRC_MQTT){
```

Change:
```cpp
  else if (topic_string == mqtt_grid_ie) {
```
to:
```cpp
  else if (topic_string == mqtt_grid_ie && divert_data_src == DATA_SRC_MQTT) {
```

Change:
```cpp
  else if (topic_string == mqtt_live_pwr) {
```
to:
```cpp
  else if (topic_string == mqtt_live_pwr && shaper_data_src == DATA_SRC_MQTT) {
```

- [ ] **Step 2: Gate the subscriptions**

In `src/mqtt.cpp`, in the subscription block (~line 207), add the source check to each condition.

Change:
```cpp
  // Divert mode related
  if (config_divert_enabled()) {
    if (divert_type == DIVERT_TYPE_SOLAR && mqtt_solar != "") {
      _mqttclient.subscribe(mqtt_solar); yield();
    }
    if (divert_type == DIVERT_TYPE_GRID && mqtt_grid_ie != "") {
      _mqttclient.subscribe(mqtt_grid_ie); yield();
    }
  }

  // Current shaper related
  if (config_current_shaper_enabled()) {
    if (mqtt_live_pwr != "" && mqtt_live_pwr != mqtt_grid_ie) {
      _mqttclient.subscribe(mqtt_live_pwr); yield();
    }
  }
```
to:
```cpp
  // Divert mode related (skip when the feed is sourced from Home Assistant)
  if (config_divert_enabled() && divert_data_src == DATA_SRC_MQTT) {
    if (divert_type == DIVERT_TYPE_SOLAR && mqtt_solar != "") {
      _mqttclient.subscribe(mqtt_solar); yield();
    }
    if (divert_type == DIVERT_TYPE_GRID && mqtt_grid_ie != "") {
      _mqttclient.subscribe(mqtt_grid_ie); yield();
    }
  }

  // Current shaper related (skip when sourced from Home Assistant)
  if (config_current_shaper_enabled() && shaper_data_src == DATA_SRC_MQTT) {
    if (mqtt_live_pwr != "" && mqtt_live_pwr != mqtt_grid_ie) {
      _mqttclient.subscribe(mqtt_live_pwr); yield();
    }
  }
```
(The handler guard in Step 1 is the correctness mechanism; this subscription gate is a traffic optimization. Like all MQTT topic-config changes in this firmware, the subscription set only re-evaluates on the next MQTT (re)connect — the handler guard covers the interim, so flipping the source takes effect immediately regardless.)

- [ ] **Step 3: Build**

Run: `pio run -e openevse_wifi_v1` — expect SUCCESS. (Transient `FRAMEWORK_DIR is None` → re-run once.)

- [ ] **Step 4: Commit**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
git add src/mqtt.cpp
git commit -m "feat(ha): arbitrate MQTT vs HA for solar/grid/live-power feeds

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Full build (core-2 + P4) + hardware validation

**Files:** none (build/flash/validate only).

- [ ] **Step 1: Build both cores**

```bash
cd /home/rar/oevse/openevse_esp32_firmware
pio run -e openevse_wifi_v1
GUI_NAME=gui-nightshift pio run -e openevse_p4
```
Expected: both SUCCESS. (P4 first build may throw the transient `FRAMEWORK_DIR is None` — re-run.)

- [ ] **Step 2: Restore web_static + flash the P4**

The P4 build regenerates `src/web_static/*.h` to the gui-nightshift bundle (build-time only — never commit). After flashing, restore the committed gui-v2 default so the tree stays clean:
```bash
cd /home/rar/oevse/openevse_esp32_firmware
GUI_NAME=gui-nightshift pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5
git checkout -- src/web_static/
```
Expected: upload completes, board reboots.

- [ ] **Step 3: Confirm the config keys now persist (the reported bug)**

After the board is back up (~30 s), set the solar feed to HA and read it back:
```bash
curl -s -X POST http://10.75.1.143/config -H 'Content-Type: application/json' \
  -d '{"divert_data_src":1,"ha_solar":"sensor.luxpower_lxp_x_2_pv_power"}'
sleep 2
curl -s http://10.75.1.143/config | python3 -c "import sys,json;c=json.load(sys.stdin);print({k:c.get(k,'ABSENT') for k in ['divert_data_src','ha_solar']})"
```
Expected: `{'divert_data_src': 1, 'ha_solar': 'sensor.luxpower_lxp_x_2_pv_power'}` — they now STICK (this is the bug fix). `divert_enabled` and `divert_type` must be set for solar to actually poll (`divert_type=0`).

- [ ] **Step 4: Confirm `/status` solar tracks the HA entity**

With divert enabled and `divert_type=0` (solar), wait ~30 s for a poll cycle:
```bash
curl -s http://10.75.1.143/status | python3 -c "import sys,json;s=json.load(sys.stdin);print('solar:', s.get('solar'), '| divert_active:', s.get('divert_active'), '| charge_rate:', s.get('charge_rate'))"
```
Expected: `solar` reflects the live value of `sensor.luxpower_lxp_x_2_pv_power` (not stuck at 0), proving the HA poll writes the `solar` global and drives divert. Cross-check against the value of that entity in Home Assistant.

- [ ] **Step 5: Confirm MQTT no longer fights (arbitration)**

If an MQTT solar topic is configured and publishing, confirm that with `divert_data_src=1` the `/status` `solar` follows the HA entity, not the MQTT topic (the MQTT message is ignored by the guarded handler). Then flip `divert_data_src` back to `0` and confirm `solar` returns to the MQTT-sourced value (or its prior behavior). If no MQTT solar source is configured on the bench, note that and skip the live comparison.

- [ ] **Step 6: Record validation result**

No commit (validation only). Note the observed persistence + `/status` tracking in the task tracker / PR description.

---

## Final Verification

- [ ] `pio run -e openevse_wifi_v1` and `GUI_NAME=gui-nightshift pio run -e openevse_p4` — both build clean.
- [ ] `grep -oE '"[a-z0-9]{1,3}"\)' src/app_config.cpp | sort | uniq -d` — no duplicate short keys.
- [ ] On HW: `divert_data_src`/`ha_solar`/`ha_grid_ie`/`ha_live_pwr`/`shaper_data_src` persist across a `/config` POST (the reported bug is fixed).
- [ ] On HW: with `divert_data_src=1` + divert enabled + `divert_type=0`, `/status.solar` tracks the configured HA entity.
- [ ] Backward compatibility: with `divert_data_src`/`shaper_data_src` absent/0 (default), MQTT solar/grid/live-power behaves exactly as before (handlers fire, subscriptions made).
- [ ] No regression to the Phase 1 display-only feeds (`home_battery_*`, `vehicle_plugged`, `vehicle_charging_state` still poll/emit).
