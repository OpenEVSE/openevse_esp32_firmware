# Home Assistant Data Sources — Firmware Design

**Goal:** Implement the firmware half of the gui-nightshift "HA data sources" feature: poll four more Home Assistant feeds — solar/grid (eco divert), whole-home live power (shaper), home-battery SoC/power (display-only), and two vehicle attributes (plugged-in, charging state) — reusing the existing vehicle-HA polling engine, and surface the results through the same `status` fields the GUI already reads.

**Approach:** Generalize the existing single-purpose vehicle poll chain in `HomeAssistantClient` into a small **static poll table** driven by value type and a per-row sink. Numeric feeds keep writing the same sinks the MQTT/`/status`-POST paths use today; the two non-numeric (bool/string) and the home-battery feeds are new and surface display-only fields. All new config defaults preserve current behavior (sources default to MQTT).

**Companion GUI spec:** `openevse-gui-nightshift/docs/superpowers/specs/2026-06-03-ha-data-sources-design.md`
**Companion GUI plan:** `openevse-gui-nightshift/docs/superpowers/plans/2026-06-03-ha-data-sources.md`

This design is the authoritative source for the firmware-side **contract** (config keys the GUI writes, status fields the GUI reads). The GUI is inert without it.

---

## Scope

In scope (firmware, two phases — see Phasing):
1. **Solar / Eco divert** — solar production or grid import/export from HA.
2. **Shaper** — whole-home live power from HA.
3. **Home battery** — SoC and power from HA, **display-only** (no charging-logic change).
4. **Vehicle extras** — plugged-in state and charging state from HA.

Out of scope (mirrors the GUI spec):
- Home battery influencing divert/eco/shaper logic.
- Odometer, battery state-of-health, vehicle location.
- Staleness/timeout tracking or UI for any feed (keep-last-good, like the vehicle path today).
- Any new files — every change is additive to existing HA/config/web/mqtt modules.

---

## Background: what exists today

`HomeAssistantClient` (`src/home_assistant.cpp`) is a `MicroTasks::Task` with a working poll engine added in the vehicle-data work (tasks VD/CL):

- `loop()` runs every `HA_LOOP_INTERVAL_MS` (30 s); when `isConnected()` and `vehicle_data_src == VEHICLE_DATA_SRC_HOMEASSISTANT`, and no poll is in flight, it kicks `pollVehicle()` every `HA_VEHICLE_POLL_MS` (30 s).
- `pollVehicleField(field)` walks a hardcoded chain (0=soc,1=range,2=eta,3=charge_limit), issuing **one in-flight GET at a time** on the shared `MongooseHttpClient`, parsing via `ha_parse_entity_state()`, and writing numeric values through `evse.setVehicle*()`. It explicitly **skips non-numeric states** (`strtod` failure → keep last good) and chains to the next field in the `onResponse` handler.
- `get(path, onResponse)` is refresh-aware (declines and returns `false` when a token refresh is due), Bearer-authenticated, against `<ha_url>/api/states/<entity>`.
- In-flight guard (`_vehicleInFlight`) + a `HA_VEHICLE_TIMEOUT_MS` recovery clear a stuck chain.

The sinks for the new feeds already exist for the MQTT and `/status`-POST paths:
- `solar` / `grid_ie` — globals in `app_config`/`mqtt.cpp`, read by the divert algorithm; emitted at `web_server.cpp:258-259`. Written by MQTT (`mqtt.cpp:300/308`) and `/status` POST (`web_server.cpp:472-489`).
- `shaper.setLivePwr(double)` / `shaper.getLivePwr()` — emitted at `web_server.cpp:264`.
- Vehicle numerics — `evse.setVehicle*()`; emitted as `battery_level`/`battery_range`/`vehicle_charge_limit` at `web_server.cpp:291-300`.
- **Home battery + vehicle extras — no sink exists.** New.

`divert_type` enum (`divert.h`): `DIVERT_TYPE_UNSET=-1`, `DIVERT_TYPE_SOLAR=0`, `DIVERT_TYPE_GRID=1` — matches the GUI's `divert_type===0`=production / `1`=grid.

Config-change dispatch (`app_config.cpp:440`) already routes any key starting with `ha_` (and `vehicle_data_src`) to `homeAssistant.notifyConfigChanged()`.

---

## Architecture: unified poll table

Replace the hardcoded `switch(field)` in `pollVehicleField` with a static table the chain walks generically. Each row:

```c
enum HaValueType { HA_NUMERIC, HA_BOOL, HA_STRING };

struct HaPollEntry {
  const String *entity;   // pointer to the config string, e.g. &ha_vehicle_soc
  bool (*gate)();         // is this row active right now?
  HaValueType type;       // how to parse the state
  int sinkId;             // selects the write target in applyValue()
};
```

A single static `_pollTable[]` lists every pollable entity. `loop()` polls when **`isConnected()` AND at least one row is active** (its `gate()` true and `*entity` non-empty). The existing sequential single-in-flight chain, in-flight guard, timeout recovery, and refresh-aware `get()` are **unchanged** — `pollVehicleField` becomes `pollNext(index)` and iterates the table, skipping rows whose gate is false or whose entity is empty, advancing in `onResponse` exactly as today.

### Value coercion (centralized by `type`)

- **`HA_NUMERIC`** — `strtod`; on non-numeric state, skip (keep last good) — today's exact behavior.
- **`HA_BOOL`** — new pure helper `ha_parse_bool(state, bool &out)`: `on`/`true`/`1`/`home` → `true`; `off`/`false`/`0`/`not_home`/`unplugged` → `false`; anything else (incl. `unavailable`/`unknown`) → skip. (HA `binary_sensor` reports `on`/`off`.)
- **`HA_STRING`** — pass the raw state through **only if** `ha_parse_entity_state()` accepts it (it already rejects `unavailable`/`unknown`) and it is non-empty; otherwise skip.

### Sinks (`applyValue(sinkId, ...)` — one small switch)

| sinkId | Row | Gate | Type | Sink |
|---|---|---|---|---|
| soc | `ha_vehicle_soc` | `vehicle_data_src==HA` | numeric | `evse.setVehicleStateOfCharge()` (unchanged) |
| range | `ha_vehicle_range` | `vehicle_data_src==HA` | numeric | `evse.setVehicleRange()` (unchanged) |
| eta | `ha_vehicle_eta` | `vehicle_data_src==HA` | numeric | `evse.setVehicleEta()` (unchanged) |
| chargeLimit | `ha_vehicle_charge_limit` | `vehicle_data_src==HA` | numeric | `evse.setVehicleChargeLimit()` (unchanged) |
| vehiclePlugged | `ha_vehicle_plugged` | `vehicle_data_src==HA` | bool | HA-client member → `/status` |
| vehicleChargingState | `ha_vehicle_charging_state` | `vehicle_data_src==HA` | string | HA-client member → `/status` |
| homeBatterySoc | `ha_battery_soc` | `ha_supported` (always) | numeric | HA-client member → `/status` |
| homeBatteryPower | `ha_battery_power` | `ha_supported` (always) | numeric | HA-client member → `/status` |
| solar | `ha_solar` | `divert_data_src==1 && divert_type==DIVERT_TYPE_SOLAR` | numeric | global `solar` (+ shaper recalc) |
| gridIe | `ha_grid_ie` | `divert_data_src==1 && divert_type==DIVERT_TYPE_GRID` | numeric | global `grid_ie` (+ shaper recalc) |
| shaperLivePwr | `ha_live_pwr` | `shaper_data_src==1` | numeric | `shaper.setLivePwr()` |

**Display-only values live in the HA client, not the EVSE manager.** Vehicle numerics keep going through `evse.setVehicle*()` because the EVSE *consumes* them (charge-limit logic, TFT). Home-battery and the two vehicle-extras are display-only — the EVSE never reads them — so they live as `HomeAssistantClient` members and are merged into `/status` via a new `homeAssistant.addStatusFields(JsonDocument &doc)` called from `buildStatus()` in `web_server.cpp` (near the existing vehicle block).

**Solar/grid recalc:** after writing the `solar`/`grid_ie` global, trigger the same "recalculate shaper" step the `/status` POST path uses (`if (shaper.getState()) shaper.shapeCurrent();`) so downstream divert/shaper behavior is identical regardless of source.

---

## Contract (authoritative)

### Config keys (GUI writes → firmware reads)

| Key | Short | Type | Default | Notes |
|---|---|---|---|---|
| `divert_data_src` | `dvs` | `uint8_t` | `0` | `0`=MQTT, `1`=Home Assistant (NB: `dds` is already taken by `divert_decay_smoothing_time`) |
| `shaper_data_src` | `shs` | `uint8_t` | `0` | `0`=MQTT, `1`=Home Assistant |
| `ha_solar` | `hso` | `String` | `""` | entity ID, used when `divert_type==SOLAR` |
| `ha_grid_ie` | `hgi` | `String` | `""` | entity ID, used when `divert_type==GRID` |
| `ha_live_pwr` | `hlp` | `String` | `""` | entity ID (shaper live power) |
| `ha_battery_soc` | `hbs` | `String` | `""` | entity ID (home battery %) |
| `ha_battery_power` | `hbp` | `String` | `""` | entity ID (home battery W) |
| `ha_vehicle_plugged` | `hvp` | `String` | `""` | binary entity ID |
| `ha_vehicle_charging_state` | `hcs` | `String` | `""` | entity ID |

The seven `ha_*` keys auto-route to `notifyConfigChanged()` via the existing `app_config.cpp:440` dispatch. The two `*_data_src` keys do **not** match `ha_*`, so add a dispatch branch that calls `homeAssistant.notifyConfigChanged()` (re-arms polling) — and, since changing the source also changes whether MQTT should subscribe, the same branch must re-evaluate MQTT subscriptions (Phase 2; e.g. `mqttClient.notifyConfigChanged()` or equivalent re-subscribe).

(Short keys above are the proposed 3-char NVS codes; confirm uniqueness against `app_config.cpp` at implementation time and adjust if any collide.)

### Status fields (firmware writes → GUI reads)

| Field | Type | Omit when | Notes |
|---|---|---|---|
| `solar` | int (W) | — (already emitted) | written from HA when `divert_data_src==1 && divert_type==SOLAR` |
| `grid_ie` | int (W) | — (already emitted) | written from HA when `divert_data_src==1 && divert_type==GRID` |
| `shaper_live_pwr` | int (W) | — (already emitted) | written from HA when `shaper_data_src==1` |
| `home_battery_soc` | number (%) | no valid reading | **NEVER emit `0` as "no data"** — `0` is a valid SoC; the GUI shows the group whenever the field is a finite number. Omit the field entirely until a real reading exists. |
| `home_battery_power` | number (W) | no valid reading | signed; HA's own sign passes through unchanged. |
| `vehicle_plugged` | bool | never successfully parsed | emit only after a successful `ha_parse_bool`. |
| `vehicle_charging_state` | string | empty / `unavailable` / `unknown` | **raw passthrough** — firmware does NO label mapping; the GUI lowercases and maps known values, falling back to the literal. |

The omit-when-absent rule for the four new fields is the load-bearing correctness detail: emit a field only once the HA client holds a value parsed from a successful poll. Before that (unconfigured entity, never-yet-polled, or last poll unavailable) the field is left out of `/status`, matching the GUI's "absent → hide" logic. This is the same lesson as the energy-logger SoC `-1` sentinel: a display-only reading must never collide with a legitimate zero.

---

## MQTT ⇄ HA arbitration (Phase 2)

Today `mqtt.cpp:209-220` subscribes to `mqtt_solar`/`mqtt_grid_ie`/`mqtt_live_pwr` and writes the globals on message. When a feed's source is HA, MQTT must not also write it (they would thrash every cycle). Gate each subscription on its source still being MQTT, mirroring the existing exclusive-source model:

- solar: subscribe `mqtt_solar` only when `divert_data_src == 0 && divert_type == DIVERT_TYPE_SOLAR && mqtt_solar != ""`.
- grid: subscribe `mqtt_grid_ie` only when `divert_data_src == 0 && divert_type == DIVERT_TYPE_GRID && mqtt_grid_ie != ""`.
- live power: subscribe `mqtt_live_pwr` only when `shaper_data_src == 0 && mqtt_live_pwr != ""`.

Changing `divert_data_src`/`shaper_data_src` must re-evaluate the subscription set (the dispatch branch added above). The `/status` POST writes (`web_server.cpp:472-489`) stay as-is: an explicit external push is a deliberate act, not a competing background source. The HA poll writes the **same** globals the MQTT path did and reuses the same shaper-recalc trigger, so divert/shaper downstream behavior is unchanged regardless of source.

---

## Phasing (one design, two plans/branches)

**Phase 1 — display-only (low risk; unblocks GUI Monitoring).**
- Refactor the vehicle chain into the generic poll table (no behavior change; covered by the existing `ha_parse_entity_state` native tests plus new ones).
- Add the four display-only rows: `home_battery_soc`, `home_battery_power`, `vehicle_plugged`, `vehicle_charging_state`.
- Add the four config keys (`ha_battery_soc`, `ha_battery_power`, `ha_vehicle_plugged`, `ha_vehicle_charging_state`) — all `ha_*`, so dispatch is automatic.
- Add `ha_parse_bool` + `HomeAssistantClient` members + `addStatusFields()` + the `buildStatus()` call.
- Ship + HW-validate on the P4 against a real HA instance.

**Phase 2 — control-path (higher risk; touches charge-current inputs).**
- Add `divert_data_src`/`shaper_data_src` config + the dispatch branch (notify HA + re-evaluate MQTT subscriptions).
- Add the three control rows (solar/grid/shaper) + the shaper-recalc trigger.
- Add MQTT subscription gating.
- HW-validate divert + shaper behavior with HA as the source (solar/grid/live-power values move the charge current as expected; flipping the selector back to MQTT restores MQTT behavior).

---

## Error handling

- **Poll failure / unavailable state / unconfigured entity:** skip that row, keep last good (numeric/bool/string), and — for the four omit-when-absent fields — simply never start emitting until a real value lands. Matches the vehicle path.
- **In-flight / stuck chain:** unchanged — `_vehicleInFlight` guard + `HA_VEHICLE_TIMEOUT_MS` recovery (rename to a feed-neutral name, e.g. `_pollInFlight`, but same mechanism).
- **`get()` declines (refresh due / not connected):** end the chain (as today) so the in-flight flag isn't stranded; next cycle retries after `loop()` refreshes.
- **Disconnect:** existing `disconnect()` clears tokens; display-only members should also be cleared so stale values stop being emitted after a disconnect.

---

## Testing

Pull value coercion into pure, native-testable helpers so logic isn't trapped behind the network (same `test/test_ha_oauth` doctest env as VD Task 1):

- **`ha_parse_bool(state, out)`** — new pure helper; cases: `on`/`off`/`true`/`false`/`1`/`0`/`home`/`not_home` → expected bool; `unavailable`/`unknown`/`""`/garbage → returns false-to-parse (skip).
- **String passthrough** — assert `ha_parse_entity_state` rejects `unavailable`/`unknown` (add a case if missing) so `HA_STRING` rows never emit junk.
- **Existing `ha_parse_entity_state` numeric tests** — must stay green after the table refactor.

The poll chain, sinks, `addStatusFields()`, and MQTT arbitration are network/hardware paths → HW-validated on the P4 against a real HA instance, exactly like VD Task 3/5 and CL Task 5. Build must stay green on both core-2.x (`openevse_wifi_v1`) and the P4 (`openevse_p4`).

---

## File Structure (all additive — no new files)

| File | Change | Phase |
|---|---|---|
| `src/home_assistant.{h,cpp}` | poll table + generic `pollNext` chain (replaces `pollVehicleField` switch); display-only members; `addStatusFields()`; `ha_parse_bool`; clear members on disconnect | 1 (+ 3 rows in 2) |
| `src/app_config.{h,cpp}` | 4 `ha_*` string keys (Phase 1) + 2 `*_data_src` `uint8_t` keys & dispatch branch (Phase 2) | 1 / 2 |
| `src/web_server.cpp` | call `homeAssistant.addStatusFields(doc)` in `buildStatus()` | 1 |
| `src/mqtt.cpp` | gate solar/grid/live-pwr subscriptions on `*_data_src == 0` | 2 |
| `test/test_ha_oauth/test_ha_oauth.cpp` | native cases for `ha_parse_bool` + string filtering | 1 |
