# OpenEVSE Divert Simulator

Scenario-driven backend simulator for OpenEVSE divert, current shaper, and load-sharing behavior.

## Overview

`divert_sim` now runs from JSON scenarios and emits one unified CSV format with:

- ISO time column (`time`)
- Per-peer columns prefixed with `<peer_id>_...`
- Group totals (`group_*`)
- All current-bearing values expressed in watts (`*_w`)

Main components:

- CLI simulator binary: `./divert_sim`
- Python runner/helpers: `run_simulations.py`
- Interactive server/UI: `server.py`, `view.html`, `interactive.html`

## Build

```bash
pio run -e native_simulator
```

This writes the simulator binary to:

`../.pio/build/native_simulator/program`

`run_simulations.py` and pytest will use that binary automatically. If you prefer,
you can still provide a local `./divert_sim` binary.

## CLI Usage

### Run a scenario

```bash
./divert_sim --scenario data/scenarios/divert_almostperfect_default.json -o output/almostperfect.csv
```

### Print resolved config only

```bash
./divert_sim --config-check
```

### Validate scenario config plumbing

```bash
./divert_sim --scenario data/scenarios/divert_almostperfect_default.json --config-check
```

### Options

- `--scenario <path>`: Scenario JSON file to run
- `-o, --output <path>`: Output CSV path (stdout when omitted)
- `-c, --config <json>`: Apply config JSON override before run
- `--config-check`: Print resolved config and exit
- `--config-load`: Load config from EpoxyFS before applying args
- `--config-commit`: Commit config to EpoxyFS after applying args
- `--help`: Show command help

## Scenario Corpus

Scenarios are stored in:

- `data/scenarios/*.json`

Legacy top-level load-sharing scenario files under `data/` have been removed in favor of this unified location.

## Charge Manager

Each simulated peer runs the firmware's Charge Manager machinery: the
`Scheduler` (scheduled rules), `Limit` (session limits), `TempThrottleTask`
(temperature protection) and `RfidTask` (card authorisation, with a simulated
reader), alongside divert, the current shaper, manual override and boost.

Scenario keys for it:

- `schedule` (top level, or per peer): timer events in the firmware
  `/schedule` format — what the Charge Manager writes for scheduled rules.
- `config`: the always-on features are config options, exactly as the
  Charge Manager saves them (`limit_default_type`/`limit_default_value`,
  `divert_enabled` + `charge_mode`, `current_shaper_enabled`, `rfid_enabled`
  + `rfid_storage`, `ocpp_enabled`, `temp_throttle_enabled`,
  `default_state`, `max_current_soft`). Set `time_zone` and
  `scheduler_start_window: 0` for deterministic rule times.
- Peer options: `rfid_reader` (reader present, default true),
  `ev.report_soc` (feed the simulated SoC to the firmware, enabling SoC
  limits), `live_pwr_add_ev` (treat `inputs.live_pwr` as the rest of the
  house and add this EV's draw, as a site meter would).
- Peer input `temperature` (deg C time series).
- Peer events: `{"rfid": "<uid>"}`, `{"temperature": 70}`, `{"soc": 80}`,
  `{"limit": {"type": "energy", "value": 5000}}` / `{"limit": "clear"}`,
  `{"schedule": [...]}` (a Charge Manager edit), and
  `{"claim": {"client": "ocpp", "state": "disabled", "charge_current": 16}}` /
  `{"release": "ocpp"}` for claim sources the simulator does not run
  (OCPP backend, MQTT, evcc, ...; a numeric client id also works).

The RAPI shim answers as a protocol 5 controller (vflags for EV connected and
charging), reports the simulated temperature on `$GP`, and holds the station
current (`$GC` cmaxamps, `$SC <amps> M`).

Each scenario run uses a private EpoxyFS/EEPROM directory, so runs in parallel
do not share the persisted schedule or config.

### Charge Manager tests

- `charge_manager.py` builds scenarios the way the Charge Manager screen
  configures the station (a port of the GUI's `rulesToTimers`), parses the
  claim columns, and provides an arbitration oracle.
- `test_charge_manager.py` covers each feature on its own: station defaults,
  every always-on feature, temperature protection, every scheduled rule
  action, rule current/limit, midnight wrap, day filters, manual override and
  live rule edits. Known gaps are strict `xfail`s, so they fail loudly once
  fixed.
- `test_charge_manager_combinations.py` runs every combination the Charge
  Manager allows (417 of them) through one scripted day and checks each
  feature's contract and the claim arbitration on every row.
- `python3 charge_manager.py` regenerates the `charge_manager_*` showcase
  scenarios for the viewer.

Use the default 5 s `tick_interval` for Charge Manager scenarios: the firmware
monitor counts RAPI polls (one per second on hardware), and longer ticks
stretch its poll-counted intervals.

## Replaying a charger's recording

The firmware keeps the last hour of what the charger saw and decided, and
serves it at `/debug/replay` (see `docs/user/troubleshooting.md`). Turn a
downloaded package into a scenario, run it, and compare:

```bash
python3 replay.py openevse-replay-openevse-1234.json
python3 replay.py package.json -o scenario.json --compare compare.csv
python3 replay.py package.json --install   # also add it to the viewer
```

The scenario gets the charger's configuration and schedule, its solar, grid,
site power, voltage and temperature readings as inputs, plug-in/out and
EV-paused events, RFID authorisations as card taps, user session limits and
boosts, and the claims of every client the simulator does not run itself
(manual override, OCPP, MQTT, evcc, ...) exactly as recorded. Divert, shaper,
schedule, limit, RFID and temperature claims are left to the firmware modules,
which is what the comparison tests. The report lists where the simulated
charger's state or pilot differs from the recorded one, plus anything the
replay cannot reproduce (a config or schedule edit during the hour, no clock,
an overwritten event log).

`data/replay/native_eco_manual_override.json` is a real capture from the
native firmware build against the OpenEVSE emulator; `test_replay.py` replays
it and checks the converter's rules.

## Unified CSV Schema

Columns are generated dynamically by peer id.

Per-peer columns:

- `<id>_online`
- `<id>_vehicle`
- `<id>_solar_w`
- `<id>_grid_ie_w`
- `<id>_live_pwr_w`
- `<id>_divert_smoothed_available_w`
- `<id>_shaper_max_w`
- `<id>_shaper_smoothed_live_w`
- `<id>_loadshare_allocated_w`
- `<id>_pilot_w`
- `<id>_charge_available_w`
- `<id>_state`
- `<id>_ev_max_charge_w`
- `<id>_actual_charge_w`
- `<id>_soc`
- `<id>_boost`
- `<id>_claim_state`
- `<id>_claim_details` (every claim as `client@priority:state`, plus the winners)
- `<id>_reason`
- `<id>_pilot_a`
- `<id>_temperature_c`
- `<id>_limit` (`<type>:<value>` or `none`)
- `<id>_rfid_auth`
- `<id>_schedule_event` (id of the schedule event in force, 0 = none)
- `<id>_session_wh` (the firmware's session energy)

Group columns:

- `group_max_w`
- `group_total_actual_w`
- `group_total_demand_w`
- `failsafe_active`

## Python and Tests

Install deps:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Run tests:

```bash
./.venv/bin/pytest -v
```

`run_simulations.py` provides:

- `run_scenario(path, output="", config_overrides=None)`
- `build_index(...)` to generate `output/index.json`

`build_index(...)` runs scenarios in parallel by default using the host CPU count.
Set `DIVERT_SIM_JOBS=1` for serial execution, or another integer to choose the
worker count.

## Web UI

Start server:

```bash
python3 server.py
```

`server.py` will generate `output/index.json` and the matching scenario CSVs on first load if they are missing.

Then open:

- `http://localhost:8000/view.html`
- `http://localhost:8000/interactive.html`

UI behavior:

- Reads scenario metadata from `output/index.json` (or `output/interactive.json`)
- Renders categories/profiles dynamically
- Uses unified CSV headers for chart series
- Treats `output/` as generated runtime state rather than committed source data

## Notes

- This simulator is backend-focused and hardware-free.
- Runtime firmware behavior on physical ESP32/OpenEVSE hardware is out of scope for this tool.
