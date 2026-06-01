# Home Assistant as a Vehicle-Data Source — Design

**Date:** 2026-06-01
**Status:** Approved (design); implementation plan to follow.
**Builds on:** the Home Assistant OAuth connection
(`docs/superpowers/specs/2026-06-01-home-assistant-oauth-design.md`) — specifically the
`homeAssistant.get(path, onResponse)` Bearer-authenticated seam and `isConnected()`.

## Goal

Let the user select **Home Assistant** as the charger's vehicle-data source (alongside
the existing None/Tesla/MQTT/HTTP options) and configure HA entity IDs for vehicle
state-of-charge, range, and time-to-full. The firmware periodically reads those entities
via the HA connection and feeds them into the EVSE manager
(`setVehicleStateOfCharge/Range/Eta`), exactly as the Tesla and MQTT sources do.

## Existing mechanism (context)

- `vehicle_data_src` (uint8_t config, `app_config.h`) selects the source; enum currently
  `NONE, TESLA, MQTT, HTTP`.
- Every source converges on `evse.setVehicleStateOfCharge(int %)`,
  `evse.setVehicleRange(int)`, `evse.setVehicleEta(int seconds)`.
  - Tesla: `teslaClient.loop()` pumped from `main.cpp` when src==TESLA; pulls charge_state.
  - MQTT (`mqtt.cpp`): when a configured topic (`mqtt_vehicle_soc/range/eta`) arrives AND
    src==MQTT, sets the values. Topic names are **optional free-text** config strings.
  - HTTP (`web_server.cpp`): accepts a POST of battery_level/battery_range/
    time_to_full_charge when src==HTTP (push model).
- Range units: the existing `mqtt_vehicle_range_miles` flag is the EVSE's single
  range-unit setting; reused as-is (no new units config).
- `/status` already exposes `battery_level`/`battery_range` read from the EVSE manager, so
  HA-sourced values reach the UI without new plumbing.

## Architecture (Approach A — poll inside HomeAssistantClient)

Chosen over (B) main.cpp dispatch and (C) a separate module: the HA client already owns
the 30 s `loop()`, the `get()` Bearer seam, and `isConnected()`; reusing them is the
least-friction path and matches how the Tesla client bundles its own polling and sets the
EVSE values. The source→EVSE coupling already exists for Tesla/MQTT, so adding an
`evse_man` dependency to the HA client is consistent.

### Files

- **`src/ha_oauth.{h,cpp}` + `test/test_ha_oauth/test_ha_oauth.cpp`** — add one pure,
  natively-tested helper:
  `bool ha_parse_entity_state(const std::string &json, std::string &out)` — extracts
  `.state` from HA's `/api/states/<entity>` JSON. Returns false if the body doesn't
  parse, `state` is missing/non-string, or `state` is `"unknown"` / `"unavailable"`.
- **`src/home_assistant.{h,cpp}`** — vehicle polling:
  - `loop()` gains a gated section: when
    `isConnected() && vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT` and a 30 s
    `_lastVehiclePoll` interval has elapsed, call `pollVehicle()`.
  - private `pollVehicle()` + per-field response handlers (sequential chain, below).
  - new `#include "evse_man.h"` (uses the global `evse`).
  - new member `unsigned long _lastVehiclePoll`.
- **`src/app_config.{h,cpp}`** — enum value `VEHICLE_DATA_SRC_HOMEASSISTANT` appended (=4);
  three optional config strings.
- **`main.cpp`** — no change (the HA client's own loop drives polling; it is not pumped
  from main like Tesla).

### Config

`enum vehicle_data_src { VEHICLE_DATA_SRC_NONE, VEHICLE_DATA_SRC_TESLA,
VEHICLE_DATA_SRC_MQTT, VEHICLE_DATA_SRC_HTTP, VEHICLE_DATA_SRC_HOMEASSISTANT };`
(append HA last to preserve existing numeric values.)

| key | short | type | meaning |
|---|---|---|---|
| `ha_vehicle_soc` | `hvs` | String | entity_id for state-of-charge (%) |
| `ha_vehicle_range` | `hvr` | String | entity_id for range |
| `ha_vehicle_eta` | `hve` | String | entity_id for time-to-full (numeric state read as seconds) |

All optional; blank = that field is not polled. Default "".

## Polling flow

Driven by the HA client's existing 30 s `loop()`. Each tick:

```
if (isConnected()
    && vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT
    && (millis() - _lastVehiclePoll) >= 30000) {
  _lastVehiclePoll = millis();
  pollVehicle();
}
```

`pollVehicle()` fetches the configured entities **sequentially chained** on the shared
`MongooseHttpClient` (only one in-flight request at a time — avoids overlap with itself or
a concurrent token refresh):

1. If `ha_vehicle_soc` non-empty → `get("/api/states/" + ha_vehicle_soc, cb_soc)`.
2. `cb_soc`: `ha_parse_entity_state(body, state)`; on success
   `evse.setVehicleStateOfCharge((int)lround(atof(state)))`. Then chain to range.
3. range step: if `ha_vehicle_range` non-empty → `get(..., cb_range)`; on success
   `evse.setVehicleRange((int)lround(atof(state)))`. Then chain to eta.
4. eta step: if `ha_vehicle_eta` non-empty → `get(..., cb_eta)`; on success
   `evse.setVehicleEta((int)atof(state))` (seconds). End of chain.

Blank fields are skipped (the chain advances). A failed/`unavailable`/`unknown`/non-200
response for one field skips just that field (keeps the last good value) and still chains
to the next. URL-encode the entity ID in the path (`/api/states/<encoded>`), reusing
`ha_url_encode` (entity IDs are normally safe, but encode defensively).

Note on `get()`: it currently passes a raw `MongooseHttpResponseHandler`. The chaining is
implemented by having each handler invoke the next `get()` for the following field. Since
`homeAssistant` is a program-lifetime global, capturing member context in the handlers is
safe (same basis as the existing exchange/refresh callbacks).

## GUI (gui-nightshift)

`src/routes/settings/Vehicle.svelte` already renders the data-source selector and reveals
per-source fields (e.g. the MQTT topic fields). Add:
- a **"Home Assistant"** option to the source selector (maps to the new enum value);
- when selected, three free-text entity-ID inputs bound to `ha_vehicle_soc`/`range`/`eta`,
  saved via the existing config form — mirroring the MQTT field-reveal pattern;
- a short hint linking to **Settings → Home Assistant** (the connection must be set up
  first; these fields do nothing until HA is connected);
- i18n strings across all four locale files (en/es/fr/hu) for label/fields/hint;
- a vitest test asserting that selecting the HA source reveals the three entity fields.

## Error handling & edge cases

- **HA not connected** (src=HA, no tokens): `pollVehicle()` is gated on `isConnected()`,
  so it simply doesn't run — no crash, no clobbering of existing values.
- **Entity unavailable / unknown / 404 / non-200 / unparseable**: that field is skipped,
  last good value retained, debug-logged. Validity is never cleared on a transient blip.
- **Blank entity field**: skipped; chain continues to the next configured field.
- **Source switched away from HA**: the loop stops polling on the next tick (gated);
  existing EVSE vehicle values persist, same as the other sources.
- **Shared `MongooseHttpClient`**: sequential chaining guarantees a single in-flight
  request; no concurrent-request hazard.
- **NTP/time**: irrelevant here (no expiry math in the poll path).

## Testing

- **Native doctest** (`ha_parse_entity_state`): valid numeric state extracted;
  `"unavailable"` and `"unknown"` → false; missing/`null` `state` → false; state present
  alongside an `attributes` object still parses.
- **Firmware**: builds green (native, nodemcu-32s, openevse_p4 with GUI bundled).
  On-hardware: select source = Home Assistant, set `ha_vehicle_soc` to a real HA
  `sensor.*` (battery %), confirm `battery_level` appears in `/status` and on the display;
  set range/eta entities and confirm likewise; set an entity to a down sensor and confirm
  the last good value is retained (no clobber).
- **GUI**: vitest — selecting the HA source reveals the three entity fields and they bind
  to the correct config keys.

## Out of scope (deferred)

- Dropdown entity picker populated from HA (`/api/states`) — v1 uses free-text entity IDs
  (matches the MQTT source).
- Reading arbitrary non-vehicle HA entities / a generic entity reader.
- ETA timestamp/duration-string parsing — the ETA entity's numeric state is read as
  seconds (pass-through, same as the MQTT source).
