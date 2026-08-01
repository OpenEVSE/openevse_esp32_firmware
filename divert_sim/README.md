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
- `<id>_reason`

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
