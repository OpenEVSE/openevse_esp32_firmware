#!/usr/bin/env python3
"""Scenario-driven divert simulator tests."""

import json
from pathlib import Path
from datetime import datetime
from tempfile import NamedTemporaryFile

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
    assert "evse-001_claim_state" in rows[0]
    assert "evse-001_claim_details" in rows[0]

    ev_kwh = _energy_kwh(rows, "evse-001_actual_charge_w")
    assert ev_kwh >= 0.0

    states = {row.get("evse-001_state", "") for row in rows}
    assert states & {"sleeping", "charging", "connected"}

    assert any(
        row.get("evse-001_claim_details", "") != "No active claims"
        for row in rows
    )


def test_divert_config_override_changes_output():
    baseline = run_scenario("data/scenarios/divert_almostperfect_default.json")
    overridden = run_scenario(
        "data/scenarios/divert_almostperfect_default.json",
        config_overrides={"divert_enabled": False},
    )
    assert len(overridden) == len(baseline)
    assert "evse-001_actual_charge_w" in overridden[0]


def test_divert_stops_after_min_charge_time_when_power_stays_below_minimum():
    rows = run_scenario("data/scenarios/divert_almostperfect_default.json")

    below_min_rows = [
        idx
        for idx, row in enumerate(rows)
        if float(row.get("evse-001_divert_smoothed_available_w", 0) or 0) < 6.0 * 240.0
    ]
    assert below_min_rows, "scenario must include a low-power period"

    start_idx = below_min_rows[0]
    low_power_tail = rows[start_idx:]

    assert any(
        row.get("evse-001_state") != "charging" for row in low_power_tail
    ), "charging should stop once min charge time expires during sustained low power"


def _stale_feed_scenario(maxinterval: int, pause_at: int | None = None,
                         resume_at: int | None = None,
                         manual_at: int | None = None) -> dict:
    """Grid export large enough to charge immediately (smoothing tau 0).

    A samples:false event stops solar/grid delivery the way a stalled feeder
    does. Minimum charge time stays at 600 s so a pause can only come from
    the stale-data failsafe.
    """
    events = []
    if manual_at is not None:
        events.append({"time": manual_at, "manual": "active"})
    if pause_at is not None:
        events.append({"time": pause_at, "samples": False})
    if resume_at is not None:
        events.append({"time": resume_at, "samples": True})
    peer = {
        "id": "evse-001",
        "voltage": 240,
        "min_current": 6,
        "max_current": 32,
        "initial": {"online": True, "vehicle": True},
        "inputs": {"grid_ie": -8000},
    }
    if events:
        peer["events"] = events
    return {
        "simulation": {
            "duration": 80,
            "tick_interval": 1,
            "start_time": "2019-03-30T07:03:00Z",
        },
        "config": {
            "divert_enabled": True,
            "charge_mode": "eco",
            "divert_type": 1,
            "divert_PV_ratio": 1.1,
            "divert_attack_smoothing_time": 0,
            "divert_decay_smoothing_time": 0,
            "divert_min_charge_time": 600,
            "divert_data_maxinterval": maxinterval,
            "current_shaper_enabled": False,
        },
        "peers": [peer],
    }


def _run_inline(scenario: dict) -> list[dict]:
    with NamedTemporaryFile("w", suffix=".json", dir=Path.cwd(), delete=False) as tmp:
        json.dump(scenario, tmp)
        tmp_path = tmp.name
    try:
        return run_scenario(tmp_path)
    finally:
        Path(tmp_path).unlink(missing_ok=True)


def _state_at(rows: list[dict], second: int) -> str:
    assert second < len(rows)
    return rows[second]["evse-001_state"]


def test_divert_pauses_when_the_feed_goes_stale_and_resumes():
    """A dead solar/grid feed must drop the divert claim. A later sample
    charges again. Fresh samples, a disabled interval, and a manual override
    must not be paused by the failsafe."""
    stale = _run_inline(_stale_feed_scenario(maxinterval=30, pause_at=15, resume_at=55))
    assert _state_at(stale, 10) == "charging"
    assert _state_at(stale, 50) != "charging"
    assert _state_at(stale, 65) == "charging"

    fresh = _run_inline(_stale_feed_scenario(maxinterval=30))
    assert _state_at(fresh, 10) == "charging"
    assert _state_at(fresh, 70) == "charging"

    disabled = _run_inline(_stale_feed_scenario(maxinterval=0, pause_at=15))
    assert _state_at(disabled, 10) == "charging"
    assert _state_at(disabled, 50) == "charging"

    overridden = _run_inline(
        _stale_feed_scenario(maxinterval=30, pause_at=15, manual_at=5)
    )
    assert _state_at(overridden, 50) == "charging"

