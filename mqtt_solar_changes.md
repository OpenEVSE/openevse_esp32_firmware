# mqtt_solar branch changes

Fixes for solar divert when driven by a **scheduler timer window** ("divert"
schedule feature) rather than the config enable flag, plus a cleanup of the
legacy emonpi/data.openevse.com defaults.

Branched from `master`. Verified against a live station (`openevse-c620`) fed
by `powerwall/solar` (integer watts, ~10 s interval).

## Background / symptoms

On a setup where eco divert is activated by a schedule (config
`divert_enabled` off, schedule event with `"feature": "divert"`):

1. **Divert never saw solar data.** The station showed a few watts of solar
   while the MQTT feed carried ~3–5 kW, and never started an eco charge.
2. **Manual override was ignored** during a scheduled eco window.
3. **Cancelling an override (reselecting Auto)** brought the timer window back
   *without* eco mode — the EVSE charged at full rate for the rest of the
   window.

## Fixes

### 1. Subscribe to divert/shaper feed topics whenever configured (`effd9878`)

`Mqtt::subscribeTopics()` only subscribed to `mqtt_solar` / `mqtt_grid_ie`
when `config_divert_enabled()` was true (and `mqtt_live_pwr` only when
`config_current_shaper_enabled()`). The scheduler's Divert and Shaper timer
features activate those subsystems at runtime **without touching the config
flags**, so a schedule-driven divert received no feed data at all.

The subscriptions now depend only on a topic being configured (and, for the
divert feeds, on `divert_type` matching). `subscribeTopics()` runs on every
(re)connect, so topic edits still take effect via the MQTT restart on config
change.

### 2. Manual override now beats schedule-activated divert/shaper (`cc26ce11`)

Timer-window divert and timer-only shaper claimed at
`EvseManager_Priority_Limit` (1100), unintentionally outranking Manual (1000),
RFID (1030) and OCPP (1050). The elevation only ever needed to beat the
scheduler's base Timer claim (100) and API/MQTT (500).

New constant:

```c
#define EvseManager_Priority_TimerFeature 900
```

Used by all timer-window divert claims (`divert.cpp`) and timer-only shaper
claims including the stale-data failsafe (`current_shaper.cpp`). A
config-enabled (always-on) shaper still claims at Safety (5000) with its
failsafe at Limit (1100), so supply protection cannot be overridden.

Resulting order: schedule Timer (100) < API/MQTT (500) < **timer
divert/shaper (900)** < Manual (1000) < RFID (1030) < OCPP (1050) < Limit
(1100) < Safety (5000).

### 3. Ignore `divertmode=Normal` while a timer window is active (`1f9d47e7`)

When reselecting **Auto** after an override, the GUI releases the override and
then normalises divert mode via `POST /divertmode divertmode=1`. During a
timer window that dropped the divert task out of Eco and released its claim,
leaving the schedule's base claim (Active at hardware max) in charge.

`DivertTask::setMode()` now ignores a request for `Normal` while
`_timer_divert_active` is set — the same intent as the existing
`isTimerDivertActive()` guard on the config-change path, extended to the
`/divertmode` endpoint and MQTT `divertmode/set`. End-of-window cleanup is
unaffected (`setTimerDivertActive(false)` clears the flag before restoring the
configured mode). To stop eco charging inside a window, use a manual override,
which now outranks it.

## Default config changes (`effd9878`)

| Setting | Old default | New default |
|---|---|---|
| `mqtt_server` | `emonpi` | *(blank)* |
| `mqtt_user` | `emonpi` | *(blank)* |
| `mqtt_pass` | `emonpimqtt2016` | *(blank)* |
| `mqtt_vrms` (voltage topic) | `emon/emonpi/vrms` | *(blank)* |
| `mqtt_grid_ie` | `emon/emonpi/power1` | *(blank)* |
| `emoncms_server` | `https://data.openevse.com/emoncms` | `https://emoncms.org` |

`mqtt_topic` (base topic) already defaults to the device hostname
(`openevse-XXXX`) — unchanged.

Blanking `mqtt_grid_ie` also means `initDivertType()` now guesses
`DIVERT_TYPE_SOLAR` (not GRID) for fresh installs, since the guess keys off a
non-empty grid topic.

Note: devices that silently relied on the old emonpi defaults (never
explicitly saved those values) will come up unconfigured after this change and
need their MQTT settings entered once.

## Related, not in this branch

- The GUI submodules still contain two `data.openevse.com` references
  (gui-v2 EmonCMS settings example link, gui-nightshift dev fixture). Edited
  locally; publishing goes through the GUI submodule flow.
- Anything POSTing `/status` with `"solar"` in **kilowatts** will fight the
  MQTT feed — `setSolar()` stores integer watts, so `3.048` truncates to 3 W.
  Feed watts to both inputs.

## Testing

- `pio run -e openevse_wifi_v1_16mb` clean after each commit.
- On-device: scheduled eco window picks up the solar feed and eco-charges;
  manual override On takes over at the requested rate; reselecting Auto
  returns to eco divert within the window.
