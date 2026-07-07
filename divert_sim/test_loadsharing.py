"""Scenario-driven load-sharing tests."""

from pytest import approx

from run_simulations import run_loadsharing_simulation


def test_loadsharing_2peer_basic_split():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_2peer_basic.json",
        "loadsharing_2peer_basic",
    )
    assert not result["_supply_exceeded"]

    last_row = result["_rows"][-1]
    assert last_row["available_a"] == approx(32.0)
    assert last_row["evse-001_allocated"] == approx(16.0)
    assert last_row["evse-002_allocated"] == approx(16.0)


def test_loadsharing_variable_supply_tracks_budget():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_variable_supply.json",
        "loadsharing_variable_supply",
    )
    assert not result["_supply_exceeded"]

    rows_by_time = {r["time"]: r for r in result["_rows"]}
    assert rows_by_time[0]["available_a"] == approx(32.0)
    assert rows_by_time[1800]["available_a"] == approx(24.0)
    assert rows_by_time[3600]["available_a"] == approx(40.0)
    assert rows_by_time[5400]["available_a"] == approx(16.0)


def test_loadsharing_peer_offline_failsafe_safe_current():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_peer_offline.json",
        "loadsharing_peer_offline",
    )
    rows_by_time = {r["time"]: r for r in result["_rows"]}

    offline = rows_by_time[1800]
    assert offline["evse-002_allocated"] == approx(0.0)
    assert offline["evse-002_reason"] == "offline"
    assert offline["evse-001_allocated"] > 20.0


def test_loadsharing_failsafe_disable():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_failsafe_disable.json",
        "loadsharing_failsafe_disable",
    )
    rows_by_time = {r["time"]: r for r in result["_rows"]}

    offline = rows_by_time[1800]
    assert offline["evse-001_allocated"] == approx(0.0)
    assert offline["evse-002_allocated"] == approx(0.0)


def test_loadsharing_ev_taper_reaches_high_soc():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_taper.json",
        "loadsharing_ev_taper",
    )
    peer = result["evse-001"]
    assert peer["final_soc"] >= 99.0
    assert peer["soc_delta"] > 0


def test_loadsharing_ev_taper_releases_current_to_other_peer():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_taper_redistribution.json",
        "loadsharing_ev_taper_redistribution",
    )
    assert max(row["total_actual"] for row in result["_rows"]) <= 32.0 * 1.001

    rows_by_time = {row["time"]: row for row in result["_rows"]}
    assert rows_by_time[300]["evse-001_allocated"] == approx(16.0)

    redistributed_rows = [
        row for row in result["_rows"]
        if row["time"] >= 900
        and row["evse-001_allocated"] < 16.0
        and row["evse-002_allocated"] > 16.0
    ]
    assert redistributed_rows


def test_loadsharing_ev_can_delay_start_while_connected():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_delayed_start.json",
        "loadsharing_ev_delayed_start",
    )
    rows_by_time = {row["time"]: row for row in result["_rows"]}

    assert rows_by_time[0]["evse-001_state"] == "connected"
    assert rows_by_time[0]["evse-001_actual"] == approx(0.0)
    assert rows_by_time[600]["evse-001_state"] == "charging"
    assert rows_by_time[600]["evse-001_actual"] > 0.0


def test_loadsharing_finished_ev_stops_then_requests_aux_load():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_finish_aux_resume.json",
        "loadsharing_ev_finish_aux_resume",
    )
    rows_by_time = {row["time"]: row for row in result["_rows"]}

    finished = [
        row for row in result["_rows"]
        if row["time"] < 1800
        and row["evse-001_state"] == "connected"
        and row["evse-001_actual"] == approx(0.0)
    ]
    assert finished
    assert rows_by_time[1800]["evse-001_state"] == "charging"
    assert rows_by_time[1800]["evse-001_actual"] > 0.0
    assert rows_by_time[1800]["evse-002_actual"] > 0.0


def test_loadsharing_priority_selects_winner_under_scarcity():
    """Priority (lower value = higher priority, per loadsharing_algorithm.h)
    decides who charges when the budget only fits one minimum: evse-002
    (priority 0) must win over evse-001 (priority 10) despite its higher id."""
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_priority_wins.json",
        "loadsharing_priority_wins",
    )
    for row in result["_rows"]:
        assert row["evse-002_allocated"] == approx(6.0)
        assert row["evse-001_allocated"] == approx(0.0)
    assert result["_rows"][-1]["evse-002_reason"] == "min_subset"
    assert result["_rows"][-1]["evse-001_reason"] == "insufficient"
