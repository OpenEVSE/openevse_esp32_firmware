# HA Capability Gating + Vehicle Charge-Limit — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Gate the HA web UI on firmware capability, and read the vehicle's own charge-limit % from HA → surface it (informational) in `/status` and on the TFT.

**Architecture:** Firmware adds a generic `vehicleChargeLimit` field to the EVSE manager (mirroring SoC), fed by a new `ha_vehicle_charge_limit` HA entity via the existing poll chain, exposed in `/status` and the TFT "Charge limit" info-line. The gui derives `ha_supported` from `/config` (presence of `ha_url`) and hides the HA settings page + Vehicle HA-source option when absent.

**Tech Stack:** C++ (Arduino-ESP32, PlatformIO), ArduinoMongoose, ArduinoJson; Svelte + vitest (gui-nightshift).

**Spec:** `docs/superpowers/specs/2026-06-01-ha-gating-and-vehicle-charge-limit-design.md`
**Branch:** `feature/home-assistant-oauth` (continues — depends on unmerged HA code).

**Build/test commands:** native `pio test -e native -f test_ha_oauth`; core-2 `pio run -e nodemcu-32s`; P4 `GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4`. GUI: in `gui-nightshift/`, `npm test` / `npm run build`. **Publish gui from the canonical clone `/home/rar/openevse-gui-nightshift`** (the in-tree clone bundles into the firmware build; see Task 6).

---

## File Structure

**Firmware:** `src/evse_man.{h,cpp}` (field), `src/app_config.{h,cpp}` (config), `src/home_assistant.cpp` (poll field 3), `src/web_server.cpp` (/status), `src/lcd.cpp` (display + rotation).
**GUI (`gui-nightshift/`):** `src/lib/stores/config.js` (derived flag), `src/lib/config/pages.js` (filter), `src/routes/Settings.svelte` (pass config), `src/routes/settings/Vehicle.svelte` (gate option) + their tests.

---

## Task 1: EVSE manager — `vehicleChargeLimit` field

**Files:** Modify `src/evse_man.h`, `src/evse_man.cpp`. No unit test (firmware glue; validated by build).

- [ ] **Step 1: Add the validity bit in `src/evse_man.h`**

After `#define EVSE_VEHICLE_ETA    (1 << 2)` add:

```cpp
#define EVSE_VEHICLE_CHARGE_LIMIT (1 << 3)
```

- [ ] **Step 2: Add the member in `src/evse_man.h`**

Immediately AFTER the `int _vehicleEta;` member declaration (so it's the last of the
`_vehicleStateOfCharge` / `_vehicleRange` / `_vehicleEta` group — this ordering must match
the constructor init-list order in Step 6 to avoid a `-Wreorder` warning), add:

```cpp
    int _vehicleChargeLimit;
```

- [ ] **Step 3: Add the getter + valid accessor in `src/evse_man.h`**

Next to the existing `getVehicleStateOfCharge()` / `isVehicleStateOfChargeValid()` (around lines 465–484), add:

```cpp
    int getVehicleChargeLimit() {
      return _vehicleChargeLimit;
    }
```

and, next to the `isVehicle*Valid()` group:

```cpp
    int isVehicleChargeLimitValid() {
      return 0 != (_vehicleValid & EVSE_VEHICLE_CHARGE_LIMIT);
    }
```

- [ ] **Step 4: Declare the setter in `src/evse_man.h`**

Next to `void setVehicleStateOfCharge(int vehicleStateOfCharge);` (around line 486), add:

```cpp
    void setVehicleChargeLimit(int vehicleChargeLimit);
```

- [ ] **Step 5: Implement the setter in `src/evse_man.cpp`**

After `setVehicleEta(...)` (around line 606), add (mirrors `setVehicleStateOfCharge`):

```cpp
void EvseManager::setVehicleChargeLimit(int vehicleChargeLimit)
{
  _vehicleChargeLimit = vehicleChargeLimit;
  _vehicleValid |= EVSE_VEHICLE_CHARGE_LIMIT;
  _vehicleUpdated |= EVSE_VEHICLE_CHARGE_LIMIT;
  _vehicleLastUpdated = millis();
  MicroTask.wakeTask(this);
}
```

- [ ] **Step 6: Initialize the member in the constructor**

The `EvseManager::EvseManager(...)` initializer list zero-inits the vehicle members at
`src/evse_man.cpp:134-136`:

```cpp
  _vehicleStateOfCharge(0),
  _vehicleRange(0),
  _vehicleEta(0)
```

Add `_vehicleChargeLimit(0)` as the new last vehicle initializer — append a comma to the
`_vehicleEta(0)` line and add the new line after it:

```cpp
  _vehicleStateOfCharge(0),
  _vehicleRange(0),
  _vehicleEta(0),
  _vehicleChargeLimit(0)
```

(This matches the header declaration order from Step 2 — `_vehicleEta` then
`_vehicleChargeLimit` — so no `-Wreorder`. If `_vehicleEta(0)` is followed by more
initializers in the list, insert `_vehicleChargeLimit(0)` right after `_vehicleEta(0)` with
the appropriate commas instead.)

- [ ] **Step 7: Build**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add src/evse_man.h src/evse_man.cpp
git commit -m "feat(evse): add vehicleChargeLimit field (mirrors SoC)

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Config field + HA poll (field 3)

**Files:** Modify `src/app_config.h`, `src/app_config.cpp`, `src/home_assistant.cpp`.

- [ ] **Step 1: Add the extern in `src/app_config.h`**

Next to `extern String ha_vehicle_eta;`, add:

```cpp
extern String ha_vehicle_charge_limit;
```

- [ ] **Step 2: Add the variable in `src/app_config.cpp`**

Next to `String ha_vehicle_eta;`, add:

```cpp
String ha_vehicle_charge_limit;
```

- [ ] **Step 3: Register the ConfigOpt in `src/app_config.cpp`**

After `new ConfigOptDefinition<String>(ha_vehicle_eta, "", "ha_vehicle_eta", "hve"),` add:

```cpp
  new ConfigOptDefinition<String>(ha_vehicle_charge_limit, "", "ha_vehicle_charge_limit", "hvc"),
```

(The existing `config_changed` `name.startsWith("ha_")` branch already wakes the HA task for this key.)

- [ ] **Step 4: Extend the poll chain in `src/home_assistant.cpp`**

In `pollVehicleField(int field)`, add a `case 3` to the entity switch and change the `default`:

```cpp
  switch (field) {
    case 0: entity = ha_vehicle_soc;          break;
    case 1: entity = ha_vehicle_range;        break;
    case 2: entity = ha_vehicle_eta;          break;
    case 3: entity = ha_vehicle_charge_limit; break;
    default: _vehicleInFlight = false; return; // chain complete (now at field 4)
  }
```

And in the `onResponse` numeric-parse switch (the one that calls `evse.setVehicle*`), add the field-3 case:

```cpp
          switch (field) {
            case 0: evse.setVehicleStateOfCharge((int)lround(v)); break;
            case 1: evse.setVehicleRange((int)lround(v));         break;
            case 2: evse.setVehicleEta((int)lround(v));           break;
            case 3: evse.setVehicleChargeLimit((int)lround(v));   break;
          }
```

(No other changes — the blank-skip, numeric guard, chain-advance `pollVehicleField(next)`, and in-flight/timeout logic all already handle the extra field.)

- [ ] **Step 5: Build**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 6: Native tests still pass**

Run: `pio test -e native -f test_ha_oauth`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/app_config.h src/app_config.cpp src/home_assistant.cpp
git commit -m "feat(ha): poll vehicle charge-limit entity into the EVSE field

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Surface in /status + TFT display

**Files:** Modify `src/web_server.cpp`, `src/lcd.cpp`.

- [ ] **Step 1: Expose in `/status` (`src/web_server.cpp`)**

In the vehicle block (right after the `time_to_full_charge` lines, ~line 288), add:

```cpp
    if(evse.isVehicleChargeLimitValid()) {
      doc["vehicle_charge_limit"] = evse.getVehicleChargeLimit();
    }
```

- [ ] **Step 2: Fill the TFT "Charge limit" display case (`src/lcd.cpp:565`)**

Replace the stub:

```cpp
    case LcdInfoLine::ChargeLimit:
      // Charge limit 85%
      break;
```

with:

```cpp
    case LcdInfoLine::ChargeLimit:
      displayNumberValue(1, "Charge limit", _evse->getVehicleChargeLimit(), 0, "%");
      break;
```

- [ ] **Step 3: Insert ChargeLimit into the info-line rotation (`src/lcd.cpp`, `getNextInfoLine`, CHARGING state)**

In the `OPENEVSE_STATE_CHARGING` switch, the chain currently goes `BatterySOC → Range`. Change the `BatterySOC` case to route through `ChargeLimit` when valid, and add a `ChargeLimit` case. Replace:

```cpp
        case LcdInfoLine::BatterySOC:
          if(_evse->isVehicleRangeValid()) {
            return LcdInfoLine::Range;
          }
        case LcdInfoLine::Range:
```

with:

```cpp
        case LcdInfoLine::BatterySOC:
          if(_evse->isVehicleChargeLimitValid()) {
            return LcdInfoLine::ChargeLimit;
          }
          // fall through
        case LcdInfoLine::ChargeLimit:
          if(_evse->isVehicleRangeValid()) {
            return LcdInfoLine::Range;
          }
          // fall through
        case LcdInfoLine::Range:
```

(These are intentional fall-through cases, matching the existing style in this switch — when a value isn't valid the chain falls to the next case. The result: …BatterySOC → ChargeLimit (if valid) → Range (if valid) → TimeLeft (if valid) → EnergySession.)

- [ ] **Step 4: Build**

Run: `pio run -e nodemcu-32s`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/web_server.cpp src/lcd.cpp
git commit -m "feat: surface vehicle charge-limit in /status + TFT info-line

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: gui-nightshift — firmware-capability gating

**Files (in `gui-nightshift/`, in-tree):**
- Modify: `src/lib/stores/config.js`, `src/lib/config/pages.js`, `src/routes/Settings.svelte`, `src/routes/settings/Vehicle.svelte`
- Test: `src/lib/config/__tests__/pages.test.js` (exists), `src/routes/settings/__tests__/Vehicle.test.js` (exists)

Work from `/home/rar/oevse/openevse_esp32_firmware/gui-nightshift`. Commit in that repo (branch `main`).

- [ ] **Step 1: Read the current files**

Read `src/lib/stores/config.js` (the `withDerived()` function that adds `firmware_is_eu`), `src/lib/config/pages.js` (`SETTINGS_PAGES` + `pagesBySection()`), `src/routes/Settings.svelte` (calls `pagesBySection()`), `src/routes/settings/Vehicle.svelte` (the `srcOptions` `$derived` with the `'4'` HA entry), and `src/lib/config/__tests__/pages.test.js` (mirror its assertion style).

- [ ] **Step 2: Write failing tests**

In `src/lib/config/__tests__/pages.test.js`, add (adapt import of `pagesBySection` to the file's existing import):

```js
  it('hides the home-assistant page when ha is unsupported', () => {
    const groups = pagesBySection({})
    const keys = groups.flatMap((g) => g.pages.map((p) => p.key))
    expect(keys).not.toContain('home-assistant')
  })

  it('shows the home-assistant page when ha_supported', () => {
    const groups = pagesBySection({ ha_supported: true })
    const keys = groups.flatMap((g) => g.pages.map((p) => p.key))
    expect(keys).toContain('home-assistant')
  })

  it('still shows non-gated pages with no config', () => {
    const groups = pagesBySection(undefined)
    const keys = groups.flatMap((g) => g.pages.map((p) => p.key))
    expect(keys).toContain('network')
  })
```

In `src/routes/settings/__tests__/Vehicle.test.js`, add:

```js
  it('offers the Home Assistant source only when ha is supported', () => {
    config_store.set({ vehicle_data_src: 0, ha_supported: true })
    const { getByText } = render(Vehicle)
    expect(getByText('config.vehicle.src_homeassistant')).toBeInTheDocument()
  })

  it('hides the Home Assistant source when ha is unsupported', () => {
    config_store.set({ vehicle_data_src: 0 })
    const { queryByText } = render(Vehicle)
    expect(queryByText('config.vehicle.src_homeassistant')).not.toBeInTheDocument()
  })
```

- [ ] **Step 3: Run to verify failure**

Run: `npm test -- pages Vehicle`
Expected: FAIL (home-assistant still present without ha_supported; src_homeassistant always present).

- [ ] **Step 4: Add the `ha_supported` derived flag in `src/lib/stores/config.js`**

In `withDerived(obj)`, where it returns `{ ...obj, firmware_is_eu, max_current_firmware }`, add `ha_supported`:

```js
    const ha_supported = !!(obj && obj.ha_url !== undefined)
    return { ...obj, firmware_is_eu, max_current_firmware, ha_supported }
```

(Place the `const ha_supported = ...` next to the other derived consts inside the `if (obj && typeof obj === 'object')` block.)

- [ ] **Step 5: Gate the page list in `src/lib/config/pages.js`**

Add `requires: 'ha_supported'` to the `home-assistant` entry:

```js
  { key: 'home-assistant', route: '/settings/home-assistant', icon: 'mdi:home-assistant', labelKey: 'config.pages.home_assistant', section: 'connectivity', requires: 'ha_supported' },
```

Change `pagesBySection()` to accept config and filter:

```js
export function pagesBySection(config) {
  return SECTIONS.map((section) => ({
    section,
    pages: SETTINGS_PAGES.filter(
      (p) => p.section === section && (!p.requires || (config && config[p.requires])),
    ),
  }))
}
```

- [ ] **Step 6: Pass config in `src/routes/Settings.svelte`**

Find `const groups = pagesBySection()` and change it to read the config store reactively. If the file imports `config_store`, use:

```svelte
  import { config_store } from '../lib/stores/config.js'  // add if not already imported
  let groups = $derived(pagesBySection($config_store))
```

(If `groups` was a `const`, convert to `$derived` so it updates when config loads. Match the file's Svelte 5 rune style — other derived values in the codebase use `$derived`.)

- [ ] **Step 7: Gate the source option in `src/routes/settings/Vehicle.svelte`**

Change the `srcOptions` `$derived` so the HA entry is conditional:

```js
  let srcOptions = $derived([
    { value: '0', label: $_('config.vehicle.src_none') },
    { value: '1', label: $_('config.vehicle.src_tesla') },
    { value: '2', label: $_('config.vehicle.src_mqtt') },
    { value: '3', label: $_('config.vehicle.src_http') },
    ...($config_store?.ha_supported
      ? [{ value: '4', label: $_('config.vehicle.src_homeassistant') }]
      : []),
  ])
```

(Leave the `{:else if src === 4}` reveal block untouched — it renders by `src` value; the existing VD-Task-4 test that sets `vehicle_data_src: 4` still passes because that block isn't gated.)

- [ ] **Step 8: Run tests**

Run: `npm test -- pages Vehicle`
Expected: PASS (new cases green; the existing `vehicle_data_src: 4` reveal test still green).

Run: `npm test`
Expected: PASS (full suite; locale parity unaffected — no new i18n keys).

- [ ] **Step 9: Build the GUI**

Run: `npm run build`
Expected: SUCCESS.

- [ ] **Step 10: Commit (in gui-nightshift)**

```bash
cd gui-nightshift
git add src/lib/stores/config.js src/lib/config/pages.js src/routes/Settings.svelte src/routes/settings/Vehicle.svelte src/lib/config/__tests__/pages.test.js src/routes/settings/__tests__/Vehicle.test.js
git commit -m "feat(ha): gate HA settings page + vehicle source on firmware capability

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
cd ..
```

---

## Task 5: Full build + hardware validation

**Files:** none (verification).

- [ ] **Step 1: Native tests** — `pio test -e native -f test_ha_oauth` → PASS.
- [ ] **Step 2: Core-2 build** — `pio run -e nodemcu-32s` → SUCCESS.
- [ ] **Step 3: P4 build (gui bundled)** — `( cd gui-nightshift && npm run build )` then `GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4` → SUCCESS.
- [ ] **Step 4: Flash + on-hardware (manual)**

Flash: `GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4 -t upload --upload-port /dev/ttyACM5`; wait for HTTP 200. Then:
- Set `ha_vehicle_charge_limit` to a real HA entity reporting the car's target % (e.g. `number.car_charge_limit`). Confirm `GET /status` shows `vehicle_charge_limit` matching, within ~30 s.
- On the TFT, during a charging session, confirm the "Charge limit" line appears in the info-line rotation showing the value.
- Gating check: on this firmware (HA-capable), confirm the Home Assistant settings page and the Vehicle "Home Assistant" source are present (since `/config` has `ha_url`). (Gating-off can only be exercised against older firmware; the unit tests cover the negative path.)

- [ ] **Step 5: Commit any fixes; confirm clean tree.**

---

## Task 6: Publish

- [ ] **Step 1: Publish the GUI from the canonical clone**

```bash
S=/home/rar/oevse/openevse_esp32_firmware/gui-nightshift
T=/home/rar/openevse-gui-nightshift
git -C "$T" fetch origin && git -C "$T" checkout main && git -C "$T" rebase origin/main
rm -rf /tmp/ha-gate-patch && mkdir -p /tmp/ha-gate-patch
git -C "$S" format-patch -1 HEAD -o /tmp/ha-gate-patch        # the Task 4 gui commit
git -C "$T" am /tmp/ha-gate-patch/*.patch
git -C "$T" push origin main
```

If `am` conflicts, re-apply the Task 4 edits onto the canonical clone's files and `git -C "$T" am --continue`. Publish ONLY from `$T` (its remote is SSH; the in-tree clone's remote is HTTPS and the classifier blocks changing it).

- [ ] **Step 2: Push the firmware branch**

```bash
git push origin feature/home-assistant-oauth
```

---

## Notes for the implementer

- **`vehicleChargeLimit` is source-agnostic** — only HA feeds it this round; Tesla/MQTT could adopt `setVehicleChargeLimit()` later without refactoring.
- **LCD rotation fall-through** is the established style in `getNextInfoLine` — do not add `break;` to those cases; the fall-through is what skips invalid lines.
- **No new pure logic** → no new native test; the field is verified by build + on-hardware + the gui gating unit tests.
- **`vehicle_charge_limit`** `/status` key is deliberately distinct from the EVSE-side `/limit` endpoint and from Tesla's `charge_limit_soc` event field.
- **Two gui clones:** build/bundle from in-tree `gui-nightshift`; publish from `/home/rar/openevse-gui-nightshift` (Task 6).
