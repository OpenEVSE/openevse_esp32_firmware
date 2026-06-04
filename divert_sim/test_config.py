#!/usr/bin/env python3
"""Config plumbing tests for scenario-driven divert_sim."""

import json
from subprocess import PIPE, run


def check_config(config: dict | None = None, load: bool = False, commit: bool = False) -> dict:
    cmd = ["./divert_sim", "--config-check"]
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
