#!/usr/bin/env python3
"""Boost module simulator tests: boost overrides divert, releases on target,
loses to a Manual claim, and targets the new session when armed between
sessions."""

from datetime import datetime

from run_simulations import run_scenario


def _t(row):
    return datetime.fromisoformat(row["time"].replace("Z", "+00:00"))


def _rel(rows, row):
    return (_t(row) - _t(rows[0])).total_seconds()


def _rows_between(rows, start_s, end_s):
    t0 = _t(rows[0])
    return [r for r in rows if start_s <= (_t(r) - t0).total_seconds() < end_s]


def _boost_active(row):
    return str(row.get("evse-001_boost", "0")).strip() in ("1", "true", "True")


def test_time_boost_overrides_divert_then_releases():
    rows = run_scenario("data/scenarios/boost_time_over_divert.json", "boost_time_over_divert")

    # Before the boost: solar (300 W) is far below the 6 A minimum, so eco
    # divert never starts a charge.
    before = _rows_between(rows, 300, 1800)
    assert before and all(r["evse-001_state"] != "charging" for r in before)
    assert all(not _boost_active(r) for r in before)

    # During the boost window (1800..2700): charging at full current.
    during = _rows_between(rows, 1810, 2690)
    assert during and all(_boost_active(r) for r in during)
    assert all(r["evse-001_state"] == "charging" for r in during)
    assert all(float(r["evse-001_actual_charge_w"]) > 6500 for r in during)

    # After the deadline: boost released, divert back in control, no charging.
    after = _rows_between(rows, 2760, 5400)
    assert after and all(not _boost_active(r) for r in after)
    assert all(r["evse-001_state"] != "charging" for r in after)


def test_energy_boost_releases_on_delta_not_session_total():
    rows = run_scenario("data/scenarios/boost_energy_delta.json", "boost_energy_delta")

    # The session had already delivered well over 500 Wh before t=1800
    # (7.2 kW for 30 min = 3.6 kWh). A session-total interpretation would
    # release instantly; the delta interpretation runs until 500 Wh MORE.
    armed = [r for r in rows if _boost_active(r)]
    assert armed, "boost never armed"
    first = _rel(rows, armed[0])
    last = _rel(rows, armed[-1])
    assert first >= 1800
    # 500 Wh at ~7.2 kW is ~250 s. Instant release (< 3 ticks) means the
    # delta basis is broken; a long tail means release never fired.
    assert 100 <= (last - first) <= 400


def test_manual_disabled_outranks_active_boost():
    rows = run_scenario("data/scenarios/boost_manual_wins.json", "boost_manual_wins")

    # Boost alone (600..1200): charging.
    during_boost = _rows_between(rows, 650, 1150)
    assert during_boost and all(r["evse-001_state"] == "charging" for r in during_boost)

    # Manual Disabled on top (1200..1800): boost still armed, but NOT charging.
    manual_window = _rows_between(rows, 1260, 1740)
    assert manual_window and all(_boost_active(r) for r in manual_window)
    assert all(r["evse-001_state"] != "charging" for r in manual_window)

    # Manual released (1800..3000): boost (still inside its 2400 s window)
    # resumes charging.
    resumed = _rows_between(rows, 1900, 2900)
    assert resumed and any(r["evse-001_state"] == "charging" for r in resumed)


def test_energy_boost_armed_between_sessions_targets_the_new_session():
    """An energy boost armed while the vehicle is unplugged must deliver its
    target Wh in the session that follows, not release on the previous
    session's total.

    Scenario: charge 0..900 s (~1.8 kWh), unplug at 900, arm a 500 Wh boost at
    1200 while unplugged, plug back in at 1500. A session-total (absolute)
    reading of the threshold would release the boost the instant it is armed,
    because 1.8 kWh already exceeds 500 Wh. The delta basis must instead hold
    the claim until 500 Wh have been delivered after the vehicle returns.

    NOTE (coverage limit): this does not execute Boost's
    `session_wh < _energy_basis_wh` re-base branch. The simulator's RAPI shim
    answers `$GS` on the pre-OCPP protocol, where vflags are not parsed at all
    (always 0), so `EVSE_MONITOR_SESSION_COMPLETE` never triggers and the
    firmware never clears the session counter. Even if it did, Boost's own
    session-complete listener cancels an active boost at that moment, so a
    boost can never be observed straddling a session-energy reset. See the
    task 7 report.
    """
    rows = run_scenario("data/scenarios/boost_energy_rebase_on_new_session.json",
                        "boost_energy_rebase_on_new_session")

    armed = [r for r in rows if _boost_active(r)]
    assert armed, "boost never armed"
    first = _rel(rows, armed[0])
    last = _rel(rows, armed[-1])

    # Armed at t=1200 while unplugged and NOT released on the old session's
    # 1.8 kWh total: it must still be armed when the vehicle returns at 1500.
    assert 1200 <= first <= 1260
    assert last > 1500, "boost released before the new session even started"

    # No charging while unplugged, so no progress against the target.
    unplugged = _rows_between(rows, 950, 1500)
    assert unplugged and all(r["evse-001_state"] != "charging" for r in unplugged)

    # 500 Wh at ~7.2 kW is ~250 s of the new session; allow slack for the
    # energy meter's poll lag around the plug event.
    charging_after = [r for r in rows
                      if 1500 <= _rel(rows, r) <= last and r["evse-001_state"] == "charging"]
    delivered_wh = sum(float(r["evse-001_actual_charge_w"]) for r in charging_after) * 5.0 / 3600.0
    assert 450 <= delivered_wh <= 1200, f"delivered {delivered_wh:.0f} Wh before release"

    # And it does release — control hands back rather than charging forever.
    after = _rows_between(rows, last + 60, 3600)
    assert after and all(not _boost_active(r) for r in after)
