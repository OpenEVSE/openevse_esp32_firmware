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


def test_loadsharing_longrun_handoff_reallocates_unused_current():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_longrun_2peer_handoff.json",
        "loadsharing_longrun_2peer_handoff",
    )
    assert not result["_supply_exceeded"]

    rows = result["_rows"]
    taper_rows = [
        r for r in rows if r["evse-001_actual"] < (r["evse-001_allocated"] - 0.5)
    ]
    assert taper_rows, "Expected at least one row where evse-001 actual current tapers below allocation"

    assert any(
        r["evse-002_allocated"] > 16.5 for r in taper_rows
    ), "Expected evse-002 allocation to rise above equal-share once evse-001 tapers"

    handoff_rows = [
        r for r in rows if r["evse-001_soc"] > 90 and r["evse-001_allocated"] > 6.1
    ]
    assert handoff_rows, "Expected rows during the smooth handoff before evse-001 reaches minimum current"
    for previous, current in zip(handoff_rows, handoff_rows[1:]):
        assert current["evse-001_allocated"] <= previous["evse-001_allocated"] + 0.2

    evse_002_taper_rows = [r for r in rows if r["evse-002_soc"] > 90]
    assert evse_002_taper_rows, "Expected rows where evse-002 eventually tapers"
    for row in evse_002_taper_rows:
        assert row["evse-002_allocated"] <= max(6.0, row["evse-002_actual"] + 2.0)

    complete_rows = [
        r for r in rows if r["evse-001_soc"] >= 100.0 and r["evse-001_actual"] == approx(0.0)
    ]
    assert complete_rows, "Expected evse-001 to complete during the longrun scenario"
    assert any(r["evse-001_allocated"] == approx(0.0) for r in complete_rows)
    assert any(r["evse-002_allocated"] > 26.0 for r in complete_rows)

    for row in rows:
        assert row["total_allocated"] <= row["available_a"] + 0.05
