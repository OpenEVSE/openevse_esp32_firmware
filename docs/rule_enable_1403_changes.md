# #1403: rule on/off switch, Active badge, mobile dialog, rate above the default

Issue [#1403](https://github.com/OpenEVSE/openevse_esp32_firmware/issues/1403) asks for a switch to turn a scheduled rule on and off without editing it. It also reports three other problems:

- the Active badge never shows;
- the Edit Rule dialog cuts off Save on phones;
- the home-screen current chooser stops at the default boot current.

The work spans three repos:

| Repo | Branch | State |
|---|---|---|
| `openevse_esp32_firmware` | `feat/schedule-rule-enable` (from `master`) | PR [#1412](https://github.com/OpenEVSE/openevse_esp32_firmware/pull/1412) |
| `gui-nightshift` | `feat/rule-enable-1403` (from `main`) | PR [OpenEVSE/openevse-gui-nightshift#205](https://github.com/OpenEVSE/openevse-gui-nightshift/pull/205) |
| `open_evse` (controller) | `dev` | local, uncommitted, part of D9.4.2 |

## 1. Rule on/off switch

### Why not "priority 0"

The first idea was to disable a rule by setting its priority to 0. That doesn't work, for these reasons:

- **There's no per-rule priority.** Every scheduled event claims at one fixed priority: `Timer` (100) for a start and `Default` (10) for a stop.
- **A low priority doesn't mean "ignored".** The EVSE manager uses the highest-priority claim that sets each property. When nothing else claims, a priority-0 "active" claim still starts charging, which is the night charge the user wants to block.
- **The stop event would still fire.** A rule's stop is a separate event, and it would still claim "disabled".
- **The Active badge and the plan would still show the rule.**

### Firmware (`src/scheduler.*`)

- **New flag:** each event has `bool enabled`, which defaults to true.
- **Plan:** `buildSchedule()` skips a disabled event, so it leaves the weekly plan entirely. The event before it stays in force. With a day rule (08:00–16:00) and a night rule (23:00–05:00), switching off the night rule keeps the 16:00 stop in force until 08:00.
- **On read:** `GET /schedule` and `GET /schedule/{id}` always report `enabled`. This is how a client can tell that the firmware supports it.
- **On write:** an event without `enabled` is enabled, so saved schedules and older clients behave as before.
- **Reused slots:** a reused event slot is reset to enabled.
- **API spec:** `api.yml` (`ScheduleEvent`) documents the flag.

### GUI (`gui-nightshift`)

- **Rule card:** each scheduled rule has a switch. Turning it off rewrites both of the rule's timers with `enabled: false`, keeping the same ids and settings. A switched-off rule is dimmed and shows an **Off** badge instead of **Active**.
- **Older firmware:** the switch appears only when the firmware reports `enabled`, so firmware that would ignore it never shows a switch that does nothing.
- **Saving:** `rulesToTimers` writes the flag on every save, so editing a rule keeps its on/off state. The legacy Schedule page copies the whole stored timer when it saves, so it keeps the flag too.
- **Known limit:** editing a switched-off rule from an older GUI turns it back on, because that GUI doesn't send the flag.

## 2. Active badge (firmware bug)

`GET /schedule/plan` returned the rule list instead of the plan on every charger checked (.201 and .204). So the GUI never received a `current_event` to badge.

**Cause:** ArduinoMongoose stores routes with `push_front` and matches the most recently registered route first. `/schedule/#` was registered after `/schedule/plan`, so it caught `/schedule/plan` as event `"plan"`. `"plan".toInt()` is 0, which means "all events". `/logs/export` was shadowed by `/logs/#` the same way and returned 404. Both regressed in #1155, the ArduinoMongoose v7 migration.

**Fix:** register each specific route after its wildcard. A comment at the registration explains the ordering rule. The other wildcard routes are already in the right order.

## 3. Edit Rule dialog cut off on phones

The shared `Modal.svelte` had no height limit and centred the dialog. A tall dialog therefore lost its top and bottom off-screen, and they couldn't be scrolled to.

- **`Modal.svelte` (every dialog):**
  - the dialog is capped at `100dvh` minus its margin and scrolls inside itself (`overscroll-contain`);
  - phones get less padding (`p-4`, or `sm:p-6` on larger screens).
- **`RuleModal.svelte`:** Save/Cancel is pinned to the bottom (`sticky`).

**Checked:** in Edge at 375×600, the dialog sits inside the screen with 16 px margins, and Save stays visible while the form scrolls.

## 4. Home-screen current above the default

**What you asked for:**
- Changing the default boot current writes it to EEPROM.
- Changing the current from the main screen is a temporary override that uses the volatile `$SC … V` option.
- The main screen never changes the default.

Three layers stood in the way:

| Layer | Before | After |
|---|---|---|
| Controller `SetCurrentCapacity` | A volatile `$SC amps V` was capped at the EEPROM value. | A RAPI `$SC amps V` may go up to the hardware or service-level max (new `nosave == 2`). Internal temporary callers (PP auto-ampacity, temperature throttle, heartbeat restore) still pass `nosave == 1` and stay capped at the EEPROM default. |
| ESP32 `EvseMonitor::setPilot` | Clamped to `max_current_soft`. | Clamped to the hardware max. |
| ESP32 `EvseManager::setTargetState` | Every claim was capped at `max_current_soft`. | Same cap, except a claim marked `aboveDefault`, which may go up to the hardware max. `max_current` (shaper, load sharing, limits) still caps the result. |
| GUI rate pill | The slider stopped at `min(max_current_soft, hardware max)`. | The slider runs to the hardware max. |

### Which claims may exceed the default

A claim exceeds the default only when it's flagged with `claim(…, aboveDefault)`. Two claims are flagged:

- **Manual override** (`manual.cpp`): the rate pill, `/override`, and MQTT override.
- **A schedule rule that sets its own current** (`feature: current`). The Edit Rule slider already offered values up to the hardware max; before this change they were silently held at the default.

Other claims stay at or below the default:

- **Divert:** follows the available solar.
- **OCPP:** its smart-charging value is a limit.
- **Temperature throttle:** starts from the pilot.
- **Custom `/claims` clients.**
- **A schedule rule with no current of its own.** It still claims the hardware max so that it wins over divert, and that claim is held at the default.

### What the pill shows

- **Manual override in force:** the override value.
- **Any other claim:** the pilot the firmware applied. For example, a schedule rule with no current of its own asks for the hardware max but runs at the default.
- **Load sharing:** still uses the default as this unit's own ceiling.

### Compatibility

- **Older controllers (before D9.4.2):** they cap a volatile `$SC` at their saved value, answer `$NK` with that value, and the ESP32 adopts the echoed pilot. The pill then settles at the default; nothing loops or fails.
- **Without the ESP32:** the controller boots at its saved (EEPROM) default as before.

## Tests

- **GUI (`vitest`):** all 149 files pass.
  - New `rules.test.js` cases: reading the flag, `canDisable`, writing the flag to both timers, new rules default to on.
  - New `RuleCard.test.js` cases: no switch on older firmware, the switch reports the change, Off replaces Active.
  - `Dashboard.test.js`: the slider reaches the hardware max, a manual override above the default is shown, the applied pilot is shown for other claims.
  - `vite build` succeeds.
- **Firmware integration (`tests/integration/test_schedule.py`, runs in CI on the native build):**
  - `/schedule/plan` returns the plan;
  - `/schedule/{id}` still works;
  - `/logs/export` is no longer the `/logs/#` 404;
  - `enabled` defaults to true and is reported;
  - a disabled event keeps its settings and leaves the plan, and re-enabling puts it back.
- **Controller:** `m328p_core` and `samd` build.
- **Not tested on hardware:** the volatile override above the default on a real controller, and toggling a rule while its window is active.

## Follow-ups

- After the GUI PR merges, bump the firmware's `gui-nightshift` pointer and regenerate `src/web_static/`.
- Commit the controller change with D9.4.2. Its `rapi_proc.*` files also hold the in-progress stuck-relay work.
