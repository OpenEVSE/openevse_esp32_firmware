#!/usr/bin/env python3
"""Charge Manager features, one at a time.

Each test configures the station the way the Charge Manager screen would
(see charge_manager.py) and checks the behaviour the user was promised on
docs/user/charge-manager.md. Scenario time starts Monday 08:00 UTC; every
run also checks that the EVSE obeyed its own winning claims.

Combinations of features live in test_charge_manager_combinations.py.
"""

import pytest

import charge_manager as cm
from charge_manager import between, delivered_wh, rule
from run_simulations import run_scenario_doc

TICK = 5
H = 3600
# The firmware learns a new session's state/energy a few polls late, so energy
# and time limits overshoot by up to this much charging at 7.2 kW.
LAG_WH = 7200 * 150 / 3600


def run(doc, name=None):
    rows = cm.rows_of(run_scenario_doc(doc, output=f"cm_{name}" if name else ""))
    cfg = doc["config"]
    problems = cm.arbitration_violations(rows, cfg["default_state"], cfg["max_current_soft"])
    assert not problems, "\n".join(problems[:10])
    return rows


def charging(rows):
    return [r for r in rows if r.charging]


# ── Station defaults ────────────────────────────────────────────────────────

def test_default_state_active_charges_with_no_claims():
    rows = run(cm.build(duration=1800), "default_active")
    assert all(r.charging for r in between(rows, 30, 1800))
    assert all(not r.claims for r in rows)


def test_default_state_disabled_waits_for_a_claim():
    rows = run(cm.build(config={"default_state": False}, duration=1800,
                        events=[{"time": 900, "manual": "active"}]), "default_disabled")
    assert not charging(between(rows, 0, 900))
    assert all(r.charging for r in between(rows, 930, 1800))


def test_station_current_caps_the_pilot():
    rows = run(cm.build(config={"max_current_soft": 16}, duration=1800), "station_current")
    assert all(r.pilot == 16 for r in between(rows, 30, 1800))


def test_default_disabled_is_not_enabled_by_an_always_on_limit():
    """Limit claims Active to start a session only for an auto-release limit
    set while the default state is Disabled; the Charge Manager's default
    limit is not auto-release, so it must not wake a disabled station."""
    rows = run(cm.build(always_on=["session_limit"], config={"default_state": False},
                        duration=1800), "limit_default_disabled")
    assert not charging(rows)
    assert all(r.claim("limit") is None for r in rows)


# ── Always active: session limit ────────────────────────────────────────────

def test_energy_limit_stops_every_session():
    rows = run(cm.build(always_on=["session_limit"], config={"limit_default_value": 3000},
                        duration=3 * H,
                        events=[{"time": 5400, "vehicle": False},
                                {"time": 6000, "vehicle": True}]), "limit_energy")
    first = between(rows, 0, 5400)
    second = between(rows, 6000, 3 * H)
    # Stops once the firmware's session meter passes the limit. The meter only
    # starts ~100 s after boot, hence the margin on the first session.
    assert 3000 <= delivered_wh(first, TICK) <= 3000 + LAG_WH
    assert first[-1].claim("limit") and first[-1].claim("limit").state == "disabled"
    # A new session gets the limit again rather than staying blocked.
    assert charging(second)
    assert 3000 <= delivered_wh(second, TICK) <= 3000 + LAG_WH
    assert not second[-1].charging


def test_time_limit_stops_after_the_set_minutes():
    rows = run(cm.build(always_on=["session_limit"],
                        config={"limit_default_type": "time", "limit_default_value": 30},
                        duration=H), "limit_time")
    on = charging(rows)
    assert 30 * 60 <= on[-1].t - on[0].t <= 30 * 60 + 150


def test_soc_limit_needs_a_vehicle_soc_source():
    cfg = {"limit_default_type": "soc", "limit_default_value": 35}
    with_soc = run(cm.build(always_on=["session_limit"], config=cfg, duration=2 * H,
                            ev={"report_soc": True}), "limit_soc")
    stop = next(r for r in with_soc if not r.charging and r.t > 60)
    assert 35.0 <= float(stop["soc"]) <= 35.5

    without = run(cm.build(always_on=["session_limit"], config=cfg, duration=2 * H))
    assert float(without[-1]["soc"]) > 40, "SoC limit fired with no SoC reading"


# ── Always active: eco / solar divert ───────────────────────────────────────

SOLAR = {"solar": [{"time": 0, "value": 0}, {"time": 1800, "value": 3600},
                   {"time": 5400, "value": 0}]}


def test_eco_divert_follows_solar():
    rows = run(cm.build(always_on=["eco_divert"], inputs=SOLAR, duration=3 * H), "eco")
    assert not charging(between(rows, 0, 1800))
    sunny = between(rows, 2400, 5400)
    assert all(r.charging for r in sunny)
    # 3.6 kW / 240 V = 15 A, divided by the 1.1 PV ratio is ~13-15 A.
    assert all(12 <= r.pilot <= 15 for r in sunny)
    # Stops after the minimum charge time once the sun goes.
    assert not charging(between(rows, 5400 + 300 + 120, 3 * H))


def test_eco_divert_needs_eco_charge_mode():
    """The Charge Manager's Eco feature only writes divert_enabled. Divert
    stays in Normal mode (no solar following) until the Dashboard charge
    mode is Eco, so this station just charges flat out."""
    rows = run(cm.build(always_on=["eco_divert"], config={"charge_mode": "fast"},
                        inputs=SOLAR, duration=H), "eco_fast_mode")
    assert all(r.charging and r.pilot == 32 for r in between(rows, 60, H))


# ── Always active: grid shaping ─────────────────────────────────────────────

def test_shaping_keeps_site_under_the_grid_limit():
    house = [{"time": 0, "value": 1500}, {"time": 1800, "value": 4500},
             {"time": 3600, "value": 6200}]
    rows = run(cm.build(always_on=["shaping"], inputs={"live_pwr": house},
                        duration=2 * H), "shaping")
    # (6500 - 1500) / 240 = 20.8 A, (6500 - 4500) / 240 = 8.3 A
    assert all(r.pilot == 20 for r in between(rows, 60, 1800) if r.charging)
    assert all(r.pilot == 8 for r in between(rows, 1860, 3600) if r.charging)
    # 300 W of headroom is below the 6 A minimum: pause.
    assert not charging(between(rows, 3700, 2 * H))
    # Once the shaper has reacted to each step (one meter update), the site
    # stays at or under the limit.
    steps = [0, 1800, 3600]
    for r in between(rows, 60, 2 * H):
        if any(s <= r.t < s + 30 for s in steps):
            continue
        site_w = float(r["live_pwr_w"])
        assert site_w <= 6500, f"t={r.t}: site at {site_w} W"


# ── Always active: RFID ─────────────────────────────────────────────────────

def test_rfid_requires_a_known_card_per_session():
    rows = run(cm.build(always_on=["rfid"], duration=2 * H, events=[
        {"time": 600, "rfid": "BADCARD"},
        {"time": 1200, "rfid": cm.RFID_TAG},
        {"time": 3600, "vehicle": False},
        {"time": 4200, "vehicle": True},
    ]), "rfid")
    assert not charging(between(rows, 0, 1200)), "charged before a valid card"
    assert all(r.charging for r in between(rows, 1230, 3600))
    # Unplugging ends the authorisation: the next session needs a tap.
    assert not charging(between(rows, 4200, 2 * H))
    assert all(not r.rfid_auth for r in between(rows, 3630, 2 * H))


def test_rfid_card_tap_ends_the_session():
    rows = run(cm.build(always_on=["rfid"], duration=H, events=[
        {"time": 300, "rfid": cm.RFID_TAG},
        {"time": 1800, "rfid": cm.RFID_TAG},
    ]), "rfid_tap_off")
    assert all(r.charging for r in between(rows, 330, 1800))
    assert not charging(between(rows, 1830, H))


# ── Always active: OCPP ─────────────────────────────────────────────────────
# The simulator does not run MicroOcpp; these claims mirror what
# OcppTask::updateEvseClaim makes for a backend that withholds, then grants,
# permission with a 16 A charging profile.

OCPP_BACKEND = [
    {"time": 0, "claim": {"client": "ocpp", "state": "disabled"}},
    {"time": 1200, "claim": {"client": "ocpp", "state": "active", "charge_current": 16}},
    {"time": 2400, "claim": {"client": "ocpp", "state": "disabled"}},
]


def test_ocpp_backend_controls_charging_and_rate():
    rows = run(cm.build(always_on=["ocpp"], events=OCPP_BACKEND, duration=H), "ocpp")
    assert not charging(between(rows, 0, 1200))
    assert all(r.charging and r.pilot == 16 for r in between(rows, 1230, 2400))
    assert not charging(between(rows, 2430, H))


def test_ocpp_outranks_manual_but_not_the_session_limit():
    rows = run(cm.build(always_on=["ocpp", "session_limit"], config={"limit_default_value": 1000},
                        duration=H, events=OCPP_BACKEND[:2] + [
                            {"time": 600, "manual": "active"}]), "ocpp_priority")
    # Manual (1000) cannot override the backend's refusal (1050)...
    assert not charging(between(rows, 600, 1200))
    # ...and the backend's permission (1050) cannot override the limit (1100).
    on = charging(rows)
    assert on and delivered_wh(on, TICK) <= 1000 + LAG_WH


# ── Temperature protection ──────────────────────────────────────────────────

HEAT = [{"time": 1200, "temperature": 70}, {"time": 2400, "temperature": 30}]


def test_temperature_throttle_steps_down_then_recovers():
    """1 A per 30 s down to the minimum while at/over the setpoint, then
    1 A per 30 s back up and release."""
    rows = run(cm.build(config={"temp_throttle_enabled": True}, events=HEAT,
                        duration=H), "temp_throttle")
    assert all(r.pilot == 32 for r in between(rows, 60, 1200))
    hot = between(rows, 1200, 2400)
    assert min(r.pilot for r in hot) == 6, "never throttled to the minimum"
    assert all(r.claim("temp_throttle") for r in between(rows, 1300, 2400))
    pilots = [r.pilot for r in hot]
    assert pilots == sorted(pilots, reverse=True), "throttle should only step down while hot"
    assert all(r.pilot == 32 and r.claim("temp_throttle") is None
               for r in between(rows, 3300, H))


def test_temperature_throttle_off_ignores_heat():
    rows = run(cm.build(events=HEAT, duration=H))
    assert all(r.pilot == 32 for r in between(rows, 60, H))


# ── Scheduled rules ─────────────────────────────────────────────────────────
# Window 09:00-10:00 = scenario t 3600..7200. Station default is Disabled so
# only the rule can start a charge.

WINDOW = (3600, 7200)
RULE_INPUTS = {"solar": 3600, "live_pwr": 3000}


def run_rule(action, events=(), name=None, **kw):
    doc = cm.build(rules=[rule(action, "09:00", "10:00", **kw)], events=events,
                   config={"default_state": False}, inputs=RULE_INPUTS,
                   duration=3 * H)
    return run(doc, name or f"rule_{action}")


def inside(rows):
    return between(rows, WINDOW[0] + 60, WINDOW[1])


def outside(rows):
    return between(rows, 0, WINDOW[0]) + between(rows, WINDOW[1] + 60, 3 * H)


def test_rule_charge_window():
    rows = run_rule("charge")
    assert all(r.charging and r.pilot == 32 for r in inside(rows))
    assert not charging(outside(rows))
    assert all(r.claim("schedule").priority == cm.PRIORITY["timer"] for r in inside(rows))


def test_rule_disable_window():
    doc = cm.build(rules=[rule("disable", "09:00", "10:00")], duration=3 * H)
    rows = run(doc, "rule_disable")
    assert not charging(inside(rows))
    assert all(r.claim("schedule").state == "disabled" for r in inside(rows))


@pytest.mark.xfail(strict=True, reason=(
    "rules.js rulesToTimers() writes a Disable rule's stop as another "
    "'disabled' timer, so a timed Disable window never ends: the station "
    "stays disabled outside it as well"))
def test_rule_disable_window_ends_at_its_stop_time():
    doc = cm.build(rules=[rule("disable", "09:00", "10:00")], duration=3 * H)
    rows = run(doc)
    assert all(r.charging for r in between(rows, WINDOW[1] + 60, 3 * H))
    assert all(r.charging for r in between(rows, 60, WINDOW[0]))


def test_rule_eco_divert_window():
    rows = run_rule("eco_divert")
    win = inside(rows)
    assert all(r.claim("divert") and r.claim("divert").priority == cm.PRIORITY["timer_feature"]
               for r in win)
    assert all(r.charging and r.pilot < 32 for r in win), "should follow solar, not full rate"
    assert all(r.claim("divert") is None or r.claim("divert").priority < cm.PRIORITY["timer"]
               for r in outside(rows))
    assert not charging(outside(rows))


def test_rule_shaper_window():
    rows = run_rule("shaper")
    # (6500 - 3000) / 240 = 14.6 A
    assert all(r.charging and r.pilot == 14 for r in inside(rows))
    assert all(r.claim("shaper") is None for r in outside(rows))


def test_rule_rfid_window():
    rows = run_rule("rfid", events=[{"time": 5400, "rfid": cm.RFID_TAG}])
    assert not charging(between(rows, WINDOW[0], 5400))
    assert all(r.claim("rfid") for r in between(rows, WINDOW[0] + 10, 5400))
    assert all(r.charging for r in between(rows, 5430, WINDOW[1]))
    assert all(r.claim("rfid") is None for r in outside(rows))


def test_rule_rfid_without_reader_fails_open_for_the_window():
    """No reader on the bus: the firmware skips timer RFID (and reports it)
    rather than locking the station for the whole window."""
    doc = cm.build(rules=[rule("rfid", "09:00", "10:00")], config={"default_state": False},
                   peer={"rfid_reader": False}, duration=3 * H)
    rows = run(doc, "rule_rfid_no_reader")
    assert all(r.charging for r in inside(rows))


@pytest.mark.xfail(strict=True, reason=(
    "SchedulerFeature::OCPP is a placeholder: a scheduled OCPP rule only opens "
    "a plain charge window and never hands control to the OCPP backend"))
def test_rule_ocpp_window_hands_control_to_backend():
    rows = run_rule("ocpp", events=[{"time": 0, "claim": {"client": "ocpp", "state": "disabled"}}])
    # Backend has not authorised anyone, so the window must not charge.
    assert not charging(inside(rows))
    # And outside the window the backend's refusal must not apply.
    doc = cm.build(rules=[rule("ocpp", "09:00", "10:00")], duration=3 * H,
                   events=[{"time": 0, "claim": {"client": "ocpp", "state": "disabled"}}])
    rows = run(doc)
    assert charging(between(rows, 60, WINDOW[0]))


def test_rule_current_and_limit_apply_only_in_window():
    doc = cm.build(always_on=["session_limit"],
                   config={"default_state": False, "limit_default_value": 20000},
                   rules=[rule("charge", "09:00", "11:00", current=10, limit=("energy", 3000))],
                   duration=4 * H)
    rows = run(doc, "rule_current_limit")
    win = between(rows, 3660, 3 * H)
    assert all(r.pilot == 10 for r in win if r.charging)
    assert all(r["limit"] == "energy:3000" for r in win)
    assert 2900 <= delivered_wh(win, TICK) <= 3100
    # After the window the user's own default limit is restored.
    assert all(r["limit"] == "energy:20000" for r in between(rows, 3 * H + 60, 4 * H))


def test_rule_window_wraps_midnight():
    doc = cm.build(config={"default_state": False},
                   rules=[rule("charge", "23:00", "01:00", days=["monday"])],
                   start_time="2024-01-15T22:00:00Z", duration=4 * H)
    rows = run(doc, "rule_midnight")
    assert not charging(between(rows, 0, H))
    assert all(r.charging for r in between(rows, H + 60, 3 * H))
    assert not charging(between(rows, 3 * H + 60, 4 * H))


def test_rule_only_runs_on_its_days():
    doc = cm.build(config={"default_state": False},
                   rules=[rule("charge", "09:00", "10:00", days=["tuesday"])], duration=3 * H)
    assert not charging(run(doc))


def test_rule_without_stop_runs_until_next_event():
    """A rule with no stop time sets a state that holds until the next timer
    event. With no other rule that is the same rule a day later, so a daily
    "Charge from 09:00" is in force around the clock."""
    doc = cm.build(config={"default_state": False},
                   rules=[rule("charge", "09:00")], duration=3 * H)
    rows = run(doc)
    assert all(r.charging for r in between(rows, 60, 3 * H))
    assert all(r.claim("schedule").state == "active" for r in rows)


def test_rule_without_stop_ends_at_the_next_rule():
    doc = cm.build(config={"default_state": False},
                   rules=[rule("charge", "09:00"), rule("disable", "10:00")], duration=3 * H)
    rows = run(doc)
    assert not charging(between(rows, 0, WINDOW[0]))
    assert all(r.charging for r in inside(rows))
    assert not charging(between(rows, WINDOW[1] + 60, 3 * H))


def test_manual_override_beats_schedule_until_released():
    doc = cm.build(config={"default_state": False}, rules=[rule("charge", "09:00", "11:00")],
                   duration=4 * H, events=[{"time": 4500, "manual": "disabled"},
                                           {"time": 5400, "manual": "release"}])
    rows = run(doc, "rule_manual")
    assert not charging(between(rows, 4530, 5400))
    assert all(r.charging for r in between(rows, 5430, 3 * H))


def test_editing_the_active_rule_applies_immediately():
    rules = [rule("charge", "09:00", "11:00", current=10)]
    edited = cm.rules_to_schedule([rule("charge", "09:00", "11:00", current=20)])
    doc = cm.build(config={"default_state": False}, rules=rules, duration=3 * H,
                   events=[{"time": 5400, "schedule": edited}])
    rows = run(doc, "rule_edit")
    assert all(r.pilot == 10 for r in between(rows, 3660, 5400))
    assert all(r.pilot == 20 for r in between(rows, 5430, 3 * H))


def test_deleting_the_active_rule_releases_its_feature():
    doc = cm.build(config={"default_state": False}, rules=[rule("shaper", "09:00", "11:00")],
                   inputs=RULE_INPUTS, duration=3 * H,
                   events=[{"time": 5400, "schedule": []}])
    rows = run(doc, "rule_delete")
    assert all(r.claim("shaper") for r in between(rows, 3660, 5400))
    assert all(r.claim("shaper") is None and r.claim("schedule") is None
               for r in between(rows, 5430, 3 * H))


def test_combination_rules_reject_feature_both_always_on_and_scheduled():
    with pytest.raises(ValueError):
        cm.build(always_on=["rfid"], rules=[rule("rfid", "09:00", "10:00")])


def test_showcase_scenarios_are_up_to_date():
    """data/scenarios/charge_manager_*.json are generated; regenerate with
    `python3 charge_manager.py` after changing the day script."""
    import json
    from pathlib import Path

    for name, doc in cm.showcase().items():
        path = Path(__file__).resolve().parent / "data" / "scenarios" / f"{name}.json"
        assert json.loads(path.read_text()) == doc, f"{path.name} is stale"
