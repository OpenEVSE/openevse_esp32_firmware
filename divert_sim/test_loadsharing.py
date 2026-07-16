"""Scenario-driven load-sharing tests."""

import math

from pytest import approx

from run_simulations import run_loadsharing_simulation


def assert_physical_budget(result):
    assert not result["_physical_supply_exceeded"]
    for row in result["_rows"]:
        if not row["budget_transition_grace"]:
            assert row["total_actual"] <= row["available_a"] * 1.001


def assert_valid_allocations(result):
    valid_reasons = {
        "idle", "equal_share", "connected_min", "min_subset",
        "insufficient", "offline", "failsafe_disabled",
    }
    for row in result["_rows"]:
        for peer_id in result["_peer_ids"]:
            allocation = row[f"{peer_id}_allocated"]
            actual = row[f"{peer_id}_actual"]
            assert math.isfinite(allocation) and allocation >= 0
            assert math.isfinite(actual) and actual >= 0
            assert row[f"{peer_id}_reason"] in valid_reasons
            if not row[f"{peer_id}_online"]:
                assert allocation == approx(0.0)
                assert row[f"{peer_id}_reason"] == "offline"


def compress_allocation_runs(rows, peer_ids):
    compressed = []
    previous = None
    for row in rows:
        signature = tuple(round(row[f"{peer_id}_allocated"], 2) for peer_id in peer_ids)
        if signature != previous:
            compressed.append(row)
            previous = signature
    return compressed


def row_input_signature(row, peer_ids):
    return tuple(
        (
            row[f"{peer_id}_online"],
            row[f"{peer_id}_vehicle"],
            row[f"{peer_id}_state"],
            round(row[f"{peer_id}_actual"], 1),
        )
        for peer_id in peer_ids
    )


def assert_no_period_two_flapping(result, threshold=0.5):
    rows = compress_allocation_runs(result["_rows"], result["_peer_ids"])
    for peer_id in result["_peer_ids"]:
        allocations = [row[f"{peer_id}_allocated"] for row in rows]
        signatures = [row_input_signature(row, result["_peer_ids"]) for row in rows]
        for i in range(len(allocations) - 2):
            first = allocations[i]
            middle = allocations[i + 1]
            last = allocations[i + 2]
            if signatures[i] != signatures[i + 1] or signatures[i] != signatures[i + 2]:
                continue
            assert not (
                abs(middle - first) > threshold
                and abs(last - first) <= 0.01
            ), (
                f"{peer_id} allocation flapped {first:.2f} → {middle:.2f} → "
                f"{last:.2f} at recompute points with unchanged inputs"
            )


def test_loadsharing_2peer_basic_split():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_2peer_basic.json",
        "loadsharing_2peer_basic",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)

    last_row = result["_rows"][-1]
    assert last_row["available_a"] == approx(32.0)
    assert last_row["evse-001_allocated"] == approx(16.0)
    assert last_row["evse-002_allocated"] == approx(16.0)


def test_loadsharing_variable_supply_tracks_budget():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_variable_supply.json",
        "loadsharing_variable_supply",
    )
    assert_physical_budget(result)

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
    assert_physical_budget(result)
    assert_valid_allocations(result)
    assert all(
        row["total_actual"] <= row["available_a"] * 1.001
        for row in result["_rows"]
        if 1800 <= row["time"] <= 2400
    )


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

    # Once the allocation opens up after the (delayed) start, the actual draw
    # must expand to fill it rather than staying pinned at the connected_min
    # ramp point. The EV can pull ~30 A (7.2 kW / 240 V) against a 32 A grant.
    assert rows_by_time[700]["evse-001_allocated"] >= 30.0
    assert rows_by_time[700]["evse-001_actual"] >= 28.0


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

    # While EV1 is still drawing its sub-minimum taper trickle (before it truly
    # completes), the demand cap must hold it steady at the minimum pilot rather
    # than cycling it between charging and sleeping. Count enable/disable edges
    # over the trickle window and require none.
    trickle = [
        row for row in result["_rows"]
        if row["time"] <= 170 and row["evse-001_actual"] > 0.0
    ]
    sleep_edges = sum(
        1 for prev, cur in zip(trickle, trickle[1:])
        if prev["evse-001_state"] == "charging"
        and cur["evse-001_state"] in ("sleeping", "disabled")
    )
    assert sleep_edges == 0, f"EV1 oscillated into sleep {sleep_edges} times"

    assert rows_by_time[1800]["evse-001_state"] == "charging"
    assert rows_by_time[1800]["evse-001_actual"] > 0.0
    assert rows_by_time[1800]["evse-002_actual"] > 0.0


def test_loadsharing_scarcity_rotates_equal_priority_peers():
    """Under scarcity, equal-priority peers time-slice: the winner rotates
    every rotation_interval (1800 s here), so both cars make progress
    instead of the lowest id starving the other all night."""
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_scarcity_rotation.json",
        "loadsharing_scarcity_rotation",
    )
    rows = result["_rows"][1:]
    winners = []
    for row in rows:
        one, two = row["evse-001_allocated"], row["evse-002_allocated"]
        # Exactly one winner at the 6 A minimum each tick; never both.
        assert sorted([one, two]) == [approx(0.0), approx(6.0)]
        winners.append("evse-001" if one > 0 else "evse-002")

    # The winner changes over the run (no permanent starvation)...
    assert len(set(winners)) == 2, "rotation never happened"
    # ...and each peer holds the slot for a contiguous window, not per-tick
    # flapping: count winner changes. With the rotation clock seeded on the
    # first scarcity allocation (t=60 in this scenario), windows advance at
    # 1860/3660/5460 s. The initial t=0 connected-min offer does not seed the
    # scarcity rotation clock.
    changes = sum(1 for a, b in zip(winners, winners[1:]) if a != b)
    assert changes == 3, f"expected 3 rotations after scarcity began, got {changes}"


def test_loadsharing_priority_selects_winner_under_scarcity():
    """Priority (lower value = higher priority, per loadsharing_algorithm.h)
    decides who charges when the budget only fits one minimum: evse-002
    (priority 0) must win over evse-001 (priority 10) despite its higher id."""
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_priority_wins.json",
        "loadsharing_priority_wins",
    )
    for row in result["_rows"][1:]:
        assert row["evse-002_allocated"] == approx(6.0)
        assert row["evse-001_allocated"] == approx(0.0)
    assert result["_rows"][-1]["evse-002_reason"] == "min_subset"
    assert result["_rows"][-1]["evse-001_reason"] == "insufficient"


def test_loadsharing_connected_member_gets_min_without_taking_budget():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_connected_min_no_budget.json",
        "loadsharing_connected_min_no_budget",
    )
    rows_by_time = {r["time"]: r for r in result["_rows"]}
    row = rows_by_time[600]

    # Connected-but-not-charging peer is offered its minimum, drawing nothing.
    assert row["evse-002_state"] == "connected"
    assert row["evse-002_reason"] == "connected_min"
    assert row["evse-002_allocated"] == approx(6.0)
    assert row["evse-002_actual"] == approx(0.0)

    # The connected minimum is not taken from the budget, so the charging peer
    # keeps the full group allocation instead of being cut to a 16/16 split.
    assert row["evse-001_state"] == "charging"
    assert row["evse-001_allocated"] > 25.0


def test_loadsharing_insufficient_selects_minimum_subset():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_insufficient.json",
        "loadsharing_insufficient",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)
    last = result["_rows"][-1]
    winners = [
        peer_id for peer_id in result["_peer_ids"]
        if last[f"{peer_id}_reason"] == "min_subset"
    ]
    losers = [
        peer_id for peer_id in result["_peer_ids"]
        if last[f"{peer_id}_reason"] == "insufficient"
    ]
    assert len(winners) == 3
    assert len(losers) == 1
    assert sum(last[f"{peer_id}_allocated"] for peer_id in winners) == approx(18.0)


def test_loadsharing_three_peer_staggered_stays_complete_and_safe():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_3peer_staggered.json",
        "loadsharing_3peer_staggered",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)
    rows = {row["time"]: row for row in result["_rows"]}
    assert rows[0]["evse-001_allocated"] > 0
    assert rows[300]["evse-002_allocated"] > 0
    assert rows[600]["evse-003_allocated"] > 0


def test_loadsharing_ev_limit_redistributes_within_one_cycle():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_limited.json",
        "loadsharing_ev_limited",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)
    redistributed = [
        row for row in result["_rows"][5:]
        if row["evse-002_actual"] <= 15.0
        and row["evse-001_allocated"] > 16.0
    ]
    assert redistributed


def test_loadsharing_ev_limited_does_not_flap():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_limited.json",
        "loadsharing_ev_limited",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)
    assert_no_period_two_flapping(result)


def test_loadsharing_ev_taper_redistribution_does_not_flap():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_taper_redistribution.json",
        "loadsharing_ev_taper_redistribution",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)
    assert_no_period_two_flapping(result)


def test_loadsharing_ev_finish_aux_resume_does_not_flap():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_ev_finish_aux_resume.json",
        "loadsharing_ev_finish_aux_resume",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)
    assert_no_period_two_flapping(result)


def test_loadsharing_longrun_handoff_does_not_flap():
    result = run_loadsharing_simulation(
        "data/scenarios/loadsharing_longrun_2peer_handoff.json",
        "loadsharing_longrun_2peer_handoff",
    )
    assert_physical_budget(result)
    assert_valid_allocations(result)
    assert_no_period_two_flapping(result)
    assert result["evse-001"]["final_soc"] >= 99.0
    assert result["evse-002"]["soc_delta"] > 0

