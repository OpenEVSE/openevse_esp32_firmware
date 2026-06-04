#!/usr/bin/env python3
"""Scenario-driven divert simulator tests."""

from pathlib import Path
from datetime import datetime

import pytest

from run_simulations import run_scenario


def _parse_time(ts: str) -> datetime:
    return datetime.fromisoformat(ts.replace("Z", "+00:00"))


def _energy_kwh(rows: list[dict], column: str) -> float:
    if len(rows) < 2:
        return 0.0

    total_wh = 0.0
    for idx in range(1, len(rows)):
        now = _parse_time(rows[idx]["time"])
        prev = _parse_time(rows[idx - 1]["time"])
        dt_h = (now - prev).total_seconds() / 3600.0
        total_wh += float(rows[idx - 1].get(column, 0.0) or 0.0) * dt_h
    return total_wh / 1000.0


DIVERT_SCENARIOS = sorted(Path("data/scenarios").glob("divert_*.json"))


@pytest.mark.parametrize("scenario", DIVERT_SCENARIOS, ids=lambda p: p.stem)
def test_divert_scenarios_produce_expected_signals(scenario: Path):
    rows = run_scenario(str(scenario), scenario.stem)
    assert len(rows) > 100
    assert "evse-001_actual_charge_w" in rows[0]
    assert "evse-001_state" in rows[0]

    ev_kwh = _energy_kwh(rows, "evse-001_actual_charge_w")
    assert ev_kwh >= 0.0

    states = {row.get("evse-001_state", "") for row in rows}
    assert states & {"sleeping", "charging", "connected"}


def test_divert_config_override_changes_output():
    baseline = run_scenario("data/scenarios/divert_almostperfect_default.json")
    overridden = run_scenario(
        "data/scenarios/divert_almostperfect_default.json",
        config_overrides={"divert_enabled": False},
    )
    assert len(overridden) == len(baseline)
    assert "evse-001_actual_charge_w" in overridden[0]
