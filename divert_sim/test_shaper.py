#!/usr/bin/env python3
"""Scenario-driven current shaper tests."""

from pathlib import Path

import pytest

from run_simulations import run_scenario


SHAPER_SCENARIOS = sorted(Path("data/scenarios").glob("shaper_*.json"))


@pytest.mark.parametrize("scenario", SHAPER_SCENARIOS, ids=lambda p: p.stem)
def test_shaper_scenarios_generate_charge_and_limit_signal(scenario: Path):
    rows = run_scenario(str(scenario), scenario.stem)
    assert len(rows) > 50

    max_values = [float(r.get("evse-001_shaper_max_w", 0) or 0) for r in rows]
    live_values = [float(r.get("evse-001_live_pwr_w", 0) or 0) for r in rows]
    actual_values = [float(r.get("evse-001_actual_charge_w", 0) or 0) for r in rows]

    assert any(v > 0 for v in live_values)
    assert any(v > 0 for v in max_values)
    assert all(v >= 0 for v in actual_values)


def test_shaper_override_max_power_changes_outcome():
    baseline = run_scenario("data/scenarios/shaper_data_shaper_default.json")
    lowered = run_scenario(
        "data/scenarios/shaper_data_shaper_default.json",
        config_overrides={"current_shaper_max_pwr": 3000},
    )

    base_peak = max(float(r.get("evse-001_actual_charge_w", 0) or 0) for r in baseline)
    low_peak = max(float(r.get("evse-001_actual_charge_w", 0) or 0) for r in lowered)

    assert low_peak <= base_peak
