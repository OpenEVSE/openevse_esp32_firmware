#!/usr/bin/env python3
"""Config plumbing tests for scenario-driven divert_sim."""

import json
from pathlib import Path
from subprocess import PIPE, run
from tempfile import NamedTemporaryFile

from run_simulations import BINARY


def check_config(config: dict | None = None, load: bool = False, commit: bool = False) -> dict:
    cmd = [str(BINARY), "--config-check"]
    if config:
        cmd.extend(["-c", json.dumps(config)])
    if load:
        cmd.append("--config-load")
    if commit:
        cmd.append("--config-commit")

    result = run(cmd, stdout=PIPE, stderr=PIPE, text=True, check=False)
    assert result.returncode == 0, result.stderr
    return json.loads(result.stdout)


def test_config_defaults_present():
    cfg = check_config()
    assert "hostname" in cfg
    assert "flags" in cfg
    assert cfg["sntp_enabled"] is True


def test_config_round_trip_commit_and_load():
    updated = check_config({"mqtt_enabled": True, "divert_PV_ratio": 1.25}, commit=True)
    assert updated["mqtt_enabled"] is True

    loaded = check_config(load=True)
    assert loaded["mqtt_enabled"] is True
    assert loaded["divert_PV_ratio"] == 1.25


def test_scenario_accepts_solar_and_grid_ie_together():
    # solar and grid_ie may come from different columns of the same CSV
    # (e.g. day*_grid_ie.csv col1=solar, col2=grid_ie); both inputs are valid
    # simultaneously and DivertTask reads them independently each tick.
    scenario = {
        "simulation": {
            "duration": 60,
            "tick_interval": 60,
            "start_time": "2019-03-30T07:03:00Z",
        },
        "config": {
            "divert_enabled": True,
            "charge_mode": "eco",
            "divert_type": 1,
        },
        "peers": [
            {
                "id": "evse-001",
                "inputs": {
                    "solar": {
                        "csv": "data/day3_grid_ie.csv",
                        "time_column": 0,
                        "column": 1,
                        "skip_header": True,
                    },
                    "grid_ie": {
                        "csv": "data/day3_grid_ie.csv",
                        "time_column": 0,
                        "column": 2,
                        "skip_header": True,
                    },
                },
            }
        ],
    }

    with NamedTemporaryFile("w", suffix=".json", dir=Path.cwd(), delete=False) as tmp:
        json.dump(scenario, tmp)
        tmp_path = tmp.name

    try:
        result = run(
            [str(BINARY), "--scenario", tmp_path],
            stdout=PIPE,
            stderr=PIPE,
            text=True,
            check=False,
        )
        assert result.returncode == 0, f"Expected success; stderr: {result.stderr}"
    finally:
        Path(tmp_path).unlink(missing_ok=True)
