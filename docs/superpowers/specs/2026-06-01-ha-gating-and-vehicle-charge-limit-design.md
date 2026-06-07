# HA Firmware-Capability Gating + Vehicle Charge-Limit — Design

**Date:** 2026-06-01
**Status:** Approved (design); implementation plan to follow.
**Builds on:** the HA OAuth connection + HA vehicle-data source (both on `feature/home-assistant-oauth`).

## Goal

Two small, independent additions to the Home Assistant integration:

1. **Firmware-capability gating (gui-nightshift):** hide the HA-specific UI (the Home
   Assistant settings page and the Vehicle "Home Assistant" data-source option) when the
   running firmware doesn't support HA — so the web UI never shows dead controls on older
   firmware.
2. **Vehicle charge-limit (firmware):** read the **vehicle's own charge limit** — the
   percent the car stops charging *itself* at (like Tesla's `charge_limit_soc`) — from a
   configurable HA entity, and surface it **informationally** in `/status` and on the
   original TFT display. It is display-only: the car enforces it; the EVSE does NOT stop on
   it (that is the separate, already-existing `limitSoc` cutoff).

**Explicitly out of scope this round** (deferred): a web dashboard vehicle card, and the
P4 EEZ/LVGL on-device vehicle display (the latter needs EEZ model-binding groundwork that
is still a TODO — its own later effort).

## Context (existing mechanisms)

- Vehicle data sources feed the EVSE manager via `setVehicleStateOfCharge/Range/Eta`;
  validity is a bitmask (`_vehicleValid |= EVSE_VEHICLE_SOC`, `_vehicleUpdated`,
  `_vehicleLastUpdated = millis()`, `wakeTask`) — see `evse_man.cpp:581`.
- The HA vehicle poll (`HomeAssistantClient::pollVehicleField`, `home_assistant.cpp`)
  already chains `ha_vehicle_soc`/`range`/`eta` (fields 0/1/2) via the `get()` seam +
  `ha_parse_entity_state`, with an `_vehicleInFlight` + timeout guard and a `strtod`
  numeric guard.
- The original TFT (`lcd.cpp`) already cycles **Charge level / Range / ETA** from the
  vehicle getters; the **`LcdInfoLine::ChargeLimit`** line (`lcd.cpp:565`) is a stub with
  no value.
- `/status` (`web_server.cpp:281`) exposes `battery_level`/`battery_range`/
  `time_to_full_charge` only when the matching `isVehicle*Valid()` is true.
- The gui-nightshift config store (`src/lib/stores/config.js`) already augments the config
  object with derived flags (`firmware_is_eu`, `max_current_firmware`) via `withDerived()`.
- Settings pages come from a flat `SETTINGS_PAGES` list + `pagesBySection()`
  (`src/lib/config/pages.js`); no capability gating exists yet.
- `ConfigOpt` entries always serialize their key, so a firmware WITH the HA feature
  includes `ha_url` (and the other `ha_*` keys) in `/config`; firmware WITHOUT it does not.
  Presence of `ha_url` is therefore a reliable capability signal.

## Section 1 — Firmware-capability gating (gui-nightshift only)

**Detection:** add a derived flag in `src/lib/stores/config.js` `withDerived()`:
`ha_supported: !!(obj && obj.ha_url !== undefined)`. (Mirrors how `firmware_is_eu` is
derived.) When `/config` hasn't loaded yet (`obj` undefined) or lacks `ha_url`,
`ha_supported` is false → HA UI hidden until proven supported.

**Apply gating in two places:**

- **Settings nav** (`src/lib/config/pages.js`): give the `home-assistant` entry a
  `requires: 'ha_supported'` field. Change `pagesBySection()` to accept the config object
  and filter out any entry whose `requires` flag is falsy in that config; entries with no
  `requires` always show:
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
  `src/routes/Settings.svelte` passes `$config_store`: `pagesBySection($config_store)`.

- **Vehicle source option** (`src/routes/settings/Vehicle.svelte`): build `srcOptions` so
  the `{ value: '4', label: src_homeassistant }` entry is included only when
  `$config_store?.ha_supported` — e.g. start from the base options and conditionally
  append the HA entry. (The existing `{:else if src === 4}` reveal block can stay as-is;
  on unsupported firmware `vehicle_data_src` can't be 4 anyway since the option is hidden.)

**Routes:** the `routes.js` route for `/settings/home-assistant` stays (harmless — not
reachable from the hidden nav entry). Gating the nav entry + the source option is
sufficient for v1.

**Tests (vitest):**
- `pages.test.js` (or a new case): `pagesBySection({})` (no `ha_supported`) omits the
  home-assistant page; `pagesBySection({ ha_supported: true })` includes it. Pages without
  `requires` are unaffected either way.
- `Vehicle.test.js`: with `config_store.set({ ha_supported: true, ... })` the source
  `<select>` offers the HA option; with `ha_supported` falsy it does not.

## Section 2 — Vehicle charge-limit field (firmware + TFT + /status)

**EVSE manager (`src/evse_man.h` / `evse_man.cpp`):** add the field mirroring SoC exactly.
- A new validity/updated bit `EVSE_VEHICLE_CHARGE_LIMIT` (next free bit alongside
  `EVSE_VEHICLE_SOC`/`_RANGE`/`_ETA` — implementer finds the bit definitions and adds the
  next one).
- `int _vehicleChargeLimit;`
- `void setVehicleChargeLimit(int v)` — body mirrors `setVehicleStateOfCharge`:
  ```cpp
  _vehicleChargeLimit = v;
  _vehicleValid |= EVSE_VEHICLE_CHARGE_LIMIT;
  _vehicleUpdated |= EVSE_VEHICLE_CHARGE_LIMIT;
  _vehicleLastUpdated = millis();
  MicroTask.wakeTask(this);
  ```
- `int getVehicleChargeLimit() { return _vehicleChargeLimit; }`
- `int isVehicleChargeLimitValid() { return 0 != (_vehicleValid & EVSE_VEHICLE_CHARGE_LIMIT); }`
  (mirror the exact form of `isVehicleStateOfChargeValid`).
- Initialize `_vehicleChargeLimit` in the constructor like the other vehicle members.

**Config (`src/app_config.{h,cpp}`):** add `ha_vehicle_charge_limit` (String), registered
`new ConfigOptDefinition<String>(ha_vehicle_charge_limit, "", "ha_vehicle_charge_limit", "hvc")`
next to the other `ha_vehicle_*` entries; extern + definition added. (The existing
`config_changed` `name.startsWith("ha_")` branch already covers it.)

**HA poll (`src/home_assistant.cpp`):** extend `pollVehicleField` to a 4th field:
- `case 3: entity = ha_vehicle_charge_limit; break;`
- `default: _vehicleInFlight = false; return;` (now reached at `field == 4`).
- In the response handler, the field-3 branch: `evse.setVehicleChargeLimit((int)lround(v));`
- Everything else (blank-skip, numeric guard, chain-advance, in-flight/timeout) is the
  existing logic, unchanged.

**/status (`src/web_server.cpp`):** alongside the `battery_level`/etc. block:
```cpp
if (evse.isVehicleChargeLimitValid()) {
  doc["vehicle_charge_limit"] = evse.getVehicleChargeLimit();
}
```
Key name `vehicle_charge_limit` (deliberately distinct from the EVSE-side `/limit`
endpoint; not reusing Tesla's `charge_limit_soc` to avoid implying the Tesla event).

**TFT (`src/lcd.cpp`):** fill in the `LcdInfoLine::ChargeLimit` case (line 565):
```cpp
case LcdInfoLine::ChargeLimit:
  displayNumberValue(1, "Charge limit", _evse->getVehicleChargeLimit(), 0, "%");
  break;
```
Ensure that info-line is included in the cycling set when the value is valid — mirror
exactly how the `BatterySOC` line is added to the rotation (the implementer must locate
where the info-line sequence is built / gated on vehicle-data validity and add
`ChargeLimit` there the same way `BatterySOC` is). If `BatterySOC` is unconditionally in
the rotation, match that; if it is gated on `isVehicleStateOfChargeValid()`, gate
`ChargeLimit` on `isVehicleChargeLimitValid()`.

## Error handling & edge cases

- **Charge-limit poll** reuses the existing per-field guards: blank
  `ha_vehicle_charge_limit` → field skipped; HTTP error / `unavailable` / non-numeric →
  skipped, last good value retained; the in-flight + 30 s timeout guard already covers the
  4-field chain (the `default` clear just moves from `case 3` to `case 4`).
- **Never set** → `isVehicleChargeLimitValid()` false → absent from `/status`; the TFT
  line shows nothing (consistent with SoC behavior before first read).
- **Gating with config not loaded:** `ha_supported` is false until `/config` is fetched,
  so the HA UI is hidden during the brief load window rather than flashing then vanishing.
- **Old firmware + stale saved config:** if a device's saved `vehicle_data_src` were 4 on
  firmware lacking HA, the option is hidden but the value persists harmlessly (the firmware
  wouldn't act on an unknown source).

## Testing

- **Firmware:** builds green (native `pio test -e native -f test_ha_oauth`,
  `pio run -e nodemcu-32s`, `GUI_NAME=gui-nightshift ./scripts/pio run -e openevse_p4`).
  No new pure logic (reuses `ha_parse_entity_state`), so the charge-limit field is verified
  by build + on-hardware: set `ha_vehicle_charge_limit` to an HA entity reporting the car's
  target %, confirm `/status.vehicle_charge_limit` matches and the TFT "Charge limit" line
  shows it.
- **gui-nightshift (vitest):** the two gating tests above (pages filter + Vehicle option),
  plus the full suite + `npm run build` stay green; locale parity unaffected (no new i18n
  keys required — `src_homeassistant` etc. already exist).

## Out of scope (deferred)

- Web dashboard vehicle card (SoC/range/ETA/charge-limit readout).
- P4 EEZ/LVGL on-device vehicle display (needs EEZ model-binding groundwork — separate
  larger effort).
- Feeding the charge-limit field from Tesla/MQTT (this round wires HA only; the evse field
  is source-agnostic so other sources can adopt it later without refactoring).
