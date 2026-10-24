#!/usr/bin/env python3
"""Charge Manager features in combination.

Every combination the Charge Manager lets a user save is run through the same
five-hour day, and each feature's contract is checked on every row:

* the EVSE obeys its winning claims (state and pilot), and charges whenever
  the winning state is Active;
* each feature holds its claim exactly while it is in force (always-on, or
  inside its scheduled window) and is absent otherwise;
* shaping caps the pilot, RFID blocks until a card is tapped, the session
  limit stops each session, temperature protection throttles when hot.

Combinations: all 32 subsets of the always-on features x default state x
temperature protection, then each scheduled rule action with every subset
of the always-on features it may coexist with (a feature is never both
always-on and scheduled).

The day (scenario t in seconds, Monday 08:00 UTC = 0):
    0      EV plugged in, 30 % SoC; house load 2 kW
    1200   a valid RFID card is tapped
    1800   solar 3.6 kW until 9000
    3600   scheduled rule window opens (09:00)
    5400   EVSE temperature 70 C until 6600
    7200   house load 4.5 kW until 9000
    10800  scheduled rule window closes (11:00)
    12600  EV unplugged, back at 13200
OCPP, when always-on, is a backend that refuses until 2400, then allows 16 A
for the rest of that session, and allows the second session from 13800 (the
simulator stands in for MicroOcpp with the claims OcppTask makes).
"""

from typing import Dict, List, Optional, Sequence, Tuple

import pytest

import charge_manager as cm
from charge_manager import ALWAYS_ON_FEATURES, Row, between, rule
from run_simulations import run_scenario_doc

TICK = 5
DURATION = 5 * 3600
WINDOW = (3600, 10800)
TAP, OCPP_ALLOW, HOT, COOL = 1200, 2400, 5400, 6600
UNPLUG, REPLUG = 12600, 13200
OCPP_REALLOW = 13800
SETTLE = 60  # seconds a feature gets to take effect after a change
LIMIT_WH = cm.ALWAYS_ON_CONFIG["session_limit"]["limit_default_value"]
LAG_WH = 7200 * 150 / 3600

INPUTS = {
    "solar": [{"time": 0, "value": 0}, {"time": 1800, "value": 3600},
              {"time": 9000, "value": 0}],
    "live_pwr": [{"time": 0, "value": 2000}, {"time": 7200, "value": 4500},
                 {"time": 9000, "value": 2000}],
}
EVENTS = [
    {"time": TAP, "rfid": cm.RFID_TAG},
    {"time": HOT, "temperature": 70},
    {"time": COOL, "temperature": 25},
    {"time": UNPLUG, "vehicle": False},
    {"time": REPLUG, "vehicle": True},
]
OCPP_BACKEND = [
    {"time": 0, "claim": {"client": "ocpp", "state": "disabled"}},
    {"time": OCPP_ALLOW, "claim": {"client": "ocpp", "state": "active", "charge_current": 16}},
    {"time": UNPLUG, "claim": {"client": "ocpp", "state": "disabled"}},
    {"time": OCPP_REALLOW, "claim": {"client": "ocpp", "state": "active", "charge_current": 16}},
]
CHANGES = sorted({0, TAP, 1800, OCPP_ALLOW, *WINDOW, HOT, COOL, 7200, 9000,
                  UNPLUG, REPLUG, OCPP_REALLOW})


class Combo:
    def __init__(self, always_on: Sequence[str], default_active: bool = True,
                 temp: bool = False, action: Optional[str] = None,
                 rule_current: Optional[int] = None,
                 rule_limit: Optional[Tuple[str, int]] = None):
        self.always_on = tuple(always_on)
        self.default_active = default_active
        self.temp = temp
        self.action = action
        self.rule_current = rule_current
        self.rule_limit = rule_limit

    @property
    def id(self) -> str:
        parts = ["on=" + ("+".join(self.always_on) or "none"),
                 "default=" + ("active" if self.default_active else "disabled")]
        if self.temp:
            parts.append("temp")
        if self.action:
            extra = ""
            if self.rule_current:
                extra += f",{self.rule_current}A"
            if self.rule_limit:
                extra += f",{self.rule_limit[0]}:{self.rule_limit[1]}"
            parts.append(f"rule={self.action}{extra}")
        return "|".join(parts)

    def doc(self) -> Dict:
        rules = []
        if self.action:
            rules = [rule(self.action, "09:00", "11:00", current=self.rule_current,
                          limit=self.rule_limit)]
        events = list(EVENTS)
        if "ocpp" in self.always_on:
            events += OCPP_BACKEND
        cfg = {"default_state": self.default_active, "temp_throttle_enabled": self.temp}
        return cm.build(always_on=self.always_on, rules=rules, events=events, config=cfg,
                        inputs=INPUTS, duration=DURATION, tick=TICK, title=self.id)

    # When is each feature in force?
    def in_window(self, t: float) -> bool:
        return self.action is not None and WINDOW[0] <= t < WINDOW[1]

    def feature_on(self, feature: str, t: float) -> bool:
        if feature in self.always_on:
            return True
        return self.in_window(t) and cm.ACTION_FEATURE.get(self.action) == feature


def combos() -> List[Combo]:
    out: List[Combo] = []
    for subset in cm.all_subsets(ALWAYS_ON_FEATURES):
        for default_active in (True, False):
            for temp in (False, True):
                out.append(Combo(subset, default_active, temp))
    for action in cm.RULE_ACTIONS:
        feature = cm.ACTION_FEATURE.get(action)
        allowed = [f for f in ALWAYS_ON_FEATURES if f != feature]
        for subset in cm.all_subsets(allowed):
            for default_active in (True, False):
                out.append(Combo(subset, default_active, action=action))
    # A charge rule with its own current and limit, over every always-on set.
    for subset in cm.all_subsets(ALWAYS_ON_FEATURES):
        out.append(Combo(subset, True, action="charge", rule_current=10,
                         rule_limit=("energy", 4000)))
    return out


ALL = combos()


def settled(t: float) -> bool:
    return not any(c <= t < c + SETTLE for c in CHANGES)


def check(combo: Combo, rows: List[Row]) -> List[str]:
    problems: List[str] = []
    bad = problems.append

    problems += cm.arbitration_violations(rows, combo.default_active)

    for prev, r in zip(rows, rows[1:]):
        t = r.t
        if not settled(t):
            continue
        stable = prev.raw[f"{cm.PEER}_claim_details"] == r.raw[f"{cm.PEER}_claim_details"]

        # Liveness: an Active winner with a hungry EV attached means charging.
        if (stable and r.vehicle and float(r["soc"]) < 95
                and cm.winning_state(r, combo.default_active) == "active" and not r.charging):
            bad(f"t={t:.0f}: winning state active but {r.state} ({r['claim_details']})")

        # RFID: claim exactly while required and the card has not been tapped.
        rfid = r.claim("rfid")
        if combo.feature_on("rfid", t):
            if r.vehicle and not r.rfid_auth and rfid is None:
                bad(f"t={t:.0f}: RFID required, no card, but no RFID claim")
            # Only a higher-priority grant (an OCPP authorisation) may
            # charge without a local card.
            overridden = any(c.state == "active" and c.priority > cm.PRIORITY["rfid"]
                             for c in r.claims)
            if r.charging and not r.rfid_auth and not overridden:
                bad(f"t={t:.0f}: charging without an RFID card")
        elif rfid is not None:
            bad(f"t={t:.0f}: RFID claim while RFID not in force")

        # Shaping: claim and pilot cap while in force, nothing otherwise.
        shaper = r.claim("shaper")
        if combo.feature_on("shaping", t):
            if shaper is None:
                bad(f"t={t:.0f}: shaping in force but no shaper claim")
            elif r.charging and shaper.max_current is not None and r.pilot > max(shaper.max_current, 6):
                bad(f"t={t:.0f}: pilot {r.pilot}A over shaper cap {shaper.max_current}A")
        elif shaper is not None:
            bad(f"t={t:.0f}: shaper claim while shaping not in force")

        # Eco divert: divert claims while in force; the timer feature raises
        # its priority to TimerFeature inside the window.
        divert = r.claim("divert")
        if combo.feature_on("eco_divert", t):
            if divert is None:
                bad(f"t={t:.0f}: eco divert in force but no divert claim")
            elif combo.in_window(t) and "eco_divert" not in combo.always_on \
                    and divert.priority != cm.PRIORITY["timer_feature"]:
                bad(f"t={t:.0f}: scheduled divert at priority {divert.priority}")
        elif divert is not None:
            bad(f"t={t:.0f}: divert claim while eco not in force")

        # OCPP backend (always-on only; see module docstring).
        ocpp = r.claim("ocpp")
        if "ocpp" in combo.always_on:
            want = "active" if (OCPP_ALLOW <= t < UNPLUG or t >= OCPP_REALLOW) else "disabled"
            if ocpp is None or ocpp.state != want:
                bad(f"t={t:.0f}: expected OCPP {want} claim, got {ocpp}")
        elif ocpp is not None:
            bad(f"t={t:.0f}: OCPP claim without OCPP")

        # Schedule: the rule's claim while in its window.
        sched = r.claim("schedule")
        if combo.in_window(t):
            want = "disabled" if combo.action == "disable" else "active"
            if sched is None or sched.state != want:
                bad(f"t={t:.0f}: in window, expected schedule {want}, got {sched}")
            if combo.rule_current and sched and sched.charge_current != combo.rule_current:
                bad(f"t={t:.0f}: rule current {combo.rule_current}A not claimed")
        elif combo.action is None and sched is not None:
            bad(f"t={t:.0f}: schedule claim with no rules")

        # Session limit: which limit is armed.
        if combo.rule_limit and combo.in_window(t):
            want_limit = f"{combo.rule_limit[0]}:{combo.rule_limit[1]}"
        elif "session_limit" in combo.always_on:
            want_limit = f"energy:{LIMIT_WH}"
        else:
            want_limit = "none"
        if r["limit"] != want_limit and stable:
            bad(f"t={t:.0f}: limit {r['limit']}, expected {want_limit}")

        # Temperature protection: never without it; engaged while hot.
        throttle = r.claim("temp_throttle")
        if not combo.temp and throttle is not None:
            bad(f"t={t:.0f}: temperature throttle claim with protection off")

    # Per-session energy never exceeds an always-on limit.
    if "session_limit" in combo.always_on and not combo.rule_limit:
        for start, end in ((0, UNPLUG), (REPLUG, DURATION)):
            wh = cm.delivered_wh(between(rows, start, end), TICK)
            if wh > LIMIT_WH + LAG_WH:
                bad(f"session {start}-{end}: delivered {wh:.0f} Wh over {LIMIT_WH} Wh limit")

    # Temperature protection engages if it was charging when the heat arrived.
    if combo.temp:
        at_heat = next(r for r in rows if r.t >= HOT)
        hot_rows = between(rows, HOT + 60, COOL)
        if at_heat.charging and all(r.charging for r in hot_rows):
            if not all(r.claim("temp_throttle") for r in hot_rows):
                bad("charging through the heat without temperature throttling")
            if hot_rows[-1].pilot >= at_heat.pilot:
                bad("pilot not reduced while over the throttle setpoint")

    return problems


@pytest.mark.parametrize("combo", ALL, ids=lambda c: c.id)
def test_charge_manager_combination(combo: Combo):
    rows = cm.rows_of(run_scenario_doc(combo.doc()))
    problems = check(combo, rows)
    assert not problems, f"{combo.id}:\n" + "\n".join(problems[:15])


def test_combination_matrix_covers_every_feature_and_action():
    seen_on = {f for c in ALL for f in c.always_on}
    seen_actions = {c.action for c in ALL if c.action}
    assert seen_on == set(ALWAYS_ON_FEATURES)
    assert seen_actions == set(cm.RULE_ACTIONS)
    # Every pair of always-on features appears together.
    pairs = {(a, b) for c in ALL for a in c.always_on for b in c.always_on if a < b}
    assert len(pairs) == len(ALWAYS_ON_FEATURES) * (len(ALWAYS_ON_FEATURES) - 1) // 2
