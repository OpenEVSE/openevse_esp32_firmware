#!/usr/bin/env python3
"""Charge Manager scenario builder and behaviour oracle for divert_sim.

The Charge Manager screen (gui-nightshift `src/routes/ChargeManager.svelte`)
does not have firmware of its own: it writes config options for the
"Always active" features and compiles scheduled rules into /schedule timer
events. This module reproduces those writes so tests can drive the simulator
exactly as the UI would, and parses the simulator's claim columns so tests
can check the firmware's arbitration.

Keep `rule_to_timers` and `ALWAYS_ON_CONFIG` in step with the GUI's
`src/lib/charge_manager/rules.js` (`rulesToTimers`) and `applyAlwaysOnAction`.
"""

from __future__ import annotations

import itertools
import re
from dataclasses import dataclass, field
from datetime import datetime
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

PEER = "evse-001"
ALL_DAYS = ["sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"]

# Claim priorities (src/evse_man.h).
PRIORITY = {
    "default": 10,
    "divert": 50,
    "timer": 100,
    "boost": 200,
    "api": 500,
    "timer_feature": 900,
    "manual": 1000,
    "rfid": 1030,
    "ocpp": 1050,
    "limit": 1100,
    "safety": 5000,
}

# "Always active" features the Charge Manager can add (GLOBAL_FEATURE_KEYS).
ALWAYS_ON_FEATURES = ("session_limit", "eco_divert", "shaping", "rfid", "ocpp")

# Scheduled rule actions (RuleModal) and the always-on feature each maps to.
# A feature is either always-on or scheduled, never both (saveCard enforces it).
RULE_ACTIONS = ("charge", "disable", "eco_divert", "shaper", "rfid", "ocpp")
ACTION_FEATURE = {"eco_divert": "eco_divert", "shaper": "shaping", "rfid": "rfid", "ocpp": "ocpp"}

RFID_TAG = "DEADBEEF"

# Config the Charge Manager writes when a feature is made always-on. Eco divert
# only takes effect while the charge mode is Eco (the Dashboard selector), so the
# scenarios select it explicitly; see test_eco_divert_needs_eco_charge_mode.
ALWAYS_ON_CONFIG: Dict[str, Dict[str, Any]] = {
    "session_limit": {"limit_default_type": "energy", "limit_default_value": 6000},
    "eco_divert": {"divert_enabled": True, "charge_mode": "eco"},
    "shaping": {"current_shaper_enabled": True},
    "rfid": {"rfid_enabled": True},
    "ocpp": {"ocpp_enabled": True},
}

# Baseline config shared by every Charge Manager scenario: UTC so rule times
# are scenario times, no randomised rule start, every feature switched off.
BASE_CONFIG: Dict[str, Any] = {
    "time_zone": "UTC|UTC0",
    "scheduler_start_window": 0,
    "default_state": True,
    "max_current_soft": 32,
    "charge_mode": "fast",
    "divert_enabled": False,
    "divert_type": 0,
    "divert_PV_ratio": 1.1,
    "divert_attack_smoothing_time": 20,
    "divert_decay_smoothing_time": 120,
    "divert_min_charge_time": 300,
    "current_shaper_enabled": False,
    "current_shaper_max_pwr": 6500,
    "current_shaper_smoothing_time": 30,
    "current_shaper_min_pause_time": 60,
    "current_shaper_data_maxinterval": 600,
    "rfid_enabled": False,
    "rfid_storage": RFID_TAG,
    "ocpp_enabled": False,
    "limit_default_type": "none",
    "limit_default_value": 0,
    "temp_throttle_enabled": False,
    "temp_throttle_setpoint": 60,
}


def rule(action: str, start: str, stop: Optional[str] = None,
         days: Sequence[str] = ALL_DAYS, current: Optional[int] = None,
         limit: Optional[Tuple[str, int]] = None) -> Dict[str, Any]:
    """A Charge Manager rule as RuleModal produces it."""
    return {
        "action": action,
        "startTime": start,
        "stopTime": stop,
        "days": list(days),
        "chargeCurrent": current,
        "limit": {"type": limit[0], "value": limit[1]} if limit else None,
    }


def _shift_days_forward(days: Iterable[str]) -> List[str]:
    return [ALL_DAYS[(ALL_DAYS.index(d) + 1) % 7] for d in days]


def _is_next_day(start: str, stop: str) -> bool:
    to_min = lambda t: int(t.split(":")[0]) * 60 + int(t.split(":")[1])
    return to_min(stop) <= to_min(start)


def rule_to_timers(r: Dict[str, Any], start_id: int) -> List[Dict[str, Any]]:
    """Port of rules.js rulesToTimers() for a new rule."""
    feature = {"eco": "divert", "eco_divert": "divert", "shaper": "shaper",
               "rfid": "rfid", "ocpp": "ocpp"}.get(r["action"])
    start: Dict[str, Any] = {
        "id": start_id,
        "time": r["startTime"],
        "state": "disabled" if r["action"] == "disable" else "active",
        "days": r["days"],
    }
    if feature:
        start.update(feature=feature, feature_value=1)
    elif (r.get("chargeCurrent") or 0) > 0:
        start.update(feature="current", feature_value=r["chargeCurrent"])
    lim = r.get("limit")
    if lim and lim.get("type") not in (None, "none") and lim.get("value", 0) > 0:
        start.update(limit=lim["type"], limit_value=lim["value"])
    timers = [start]
    if r.get("stopTime"):
        stop: Dict[str, Any] = {
            "id": start_id + 1,
            "time": r["stopTime"],
            "state": "disabled",
            "days": _shift_days_forward(r["days"]) if _is_next_day(r["startTime"], r["stopTime"]) else r["days"],
        }
        if feature:
            stop.update(feature=feature, feature_value=0)
        timers.append(stop)
    return timers


def rules_to_schedule(rules: Sequence[Dict[str, Any]]) -> List[Dict[str, Any]]:
    timers: List[Dict[str, Any]] = []
    next_id = 1
    for r in rules:
        new = rule_to_timers(r, next_id)
        timers += new
        next_id += len(new) + 1
    return timers


def check_allowed(always_on: Iterable[str], rules: Sequence[Dict[str, Any]]) -> None:
    """Raise if the combination is one the Charge Manager would not save."""
    always_on = set(always_on)
    unknown = always_on - set(ALWAYS_ON_FEATURES)
    if unknown:
        raise ValueError(f"unknown always-on features: {sorted(unknown)}")
    for r in rules:
        feat = ACTION_FEATURE.get(r["action"])
        if feat and feat in always_on:
            raise ValueError(f"{feat} cannot be both always-on and scheduled")


def build(always_on: Iterable[str] = (), rules: Sequence[Dict[str, Any]] = (),
          events: Sequence[Dict[str, Any]] = (), config: Optional[Dict[str, Any]] = None,
          duration: int = 4 * 3600, tick: int = 5,
          start_time: str = "2024-01-15T08:00:00Z",  # a Monday
          inputs: Optional[Dict[str, Any]] = None,
          ev: Optional[Dict[str, Any]] = None,
          peer: Optional[Dict[str, Any]] = None,
          title: str = "") -> Dict[str, Any]:
    """Build a single-peer scenario document for a Charge Manager setup."""
    always_on = tuple(always_on)
    check_allowed(always_on, rules)
    cfg = dict(BASE_CONFIG)
    for f in always_on:
        cfg.update(ALWAYS_ON_CONFIG[f])
    cfg.update(config or {})
    p: Dict[str, Any] = {
        "id": PEER,
        "voltage": 240,
        "min_current": 6,
        "max_current": 32,
        "ev": {"battery_capacity_kwh": 75.0, "initial_soc": 30.0,
               "max_charge_rate_kw": 7.2, **(ev or {})},
        "inputs": inputs or {},
        # live_pwr in these scenarios is the rest of the house; the site
        # meter the shaper reads also sees this EV.
        "live_pwr_add_ev": True,
        "events": sorted(events, key=lambda e: e["time"]),
    }
    p.update(peer or {})
    doc: Dict[str, Any] = {
        "meta": {"id": "charge_manager", "title": title or "Charge Manager",
                 "category": "charge_manager"},
        "simulation": {"duration": duration, "tick_interval": tick,
                       "start_time": start_time},
        "config": cfg,
        "peers": [p],
    }
    if rules:
        doc["schedule"] = rules_to_schedule(rules)
    return doc


# ── Output parsing ──────────────────────────────────────────────────────────

@dataclass
class Claim:
    client: str
    priority: int
    state: str  # active / disabled / other / none
    charge_current: Optional[int] = None
    max_current: Optional[int] = None


_CLAIM_RE = re.compile(r"^(?P<client>[\w]+)@(?P<prio>\d+):(?P<state>\w+)(?P<rest>.*)$")


def parse_claims(details: str) -> Tuple[List[Claim], Dict[str, str]]:
    """Parse the claim_details column into claims and the winners map."""
    if not details or details == "No active claims":
        return [], {}
    claims_part, _, wins_part = details.partition(" ; wins")
    claims: List[Claim] = []
    for chunk in claims_part.split(" | "):
        m = _CLAIM_RE.match(chunk.strip())
        if not m:
            continue
        c = Claim(m["client"], int(m["prio"]), m["state"])
        for key, val in re.findall(r"(charge_current|max_current)=(\d+)", m["rest"]):
            setattr(c, key, int(val))
        claims.append(c)
    winners = dict(re.findall(r"(\w+)=(\w+)", wins_part))
    return claims, winners


@dataclass
class Row:
    t: float
    raw: Dict[str, str]
    claims: List[Claim] = field(default_factory=list)
    winners: Dict[str, str] = field(default_factory=dict)

    def __getitem__(self, key: str) -> str:
        return self.raw[f"{PEER}_{key}"]

    @property
    def state(self) -> str:
        return self["state"]

    @property
    def charging(self) -> bool:
        return self.state == "charging"

    @property
    def pilot(self) -> int:
        return int(self["pilot_a"])

    @property
    def actual_w(self) -> float:
        return float(self["actual_charge_w"])

    @property
    def vehicle(self) -> bool:
        return self["vehicle"] == "1"

    @property
    def rfid_auth(self) -> bool:
        return self["rfid_auth"] == "1"

    @property
    def schedule_event(self) -> int:
        return int(self["schedule_event"])

    def claim(self, client: str) -> Optional[Claim]:
        return next((c for c in self.claims if c.client == client), None)


def rows_of(raw_rows: List[Dict[str, str]]) -> List[Row]:
    t0 = datetime.fromisoformat(raw_rows[0]["time"].replace("Z", "+00:00"))
    out = []
    for r in raw_rows:
        t = (datetime.fromisoformat(r["time"].replace("Z", "+00:00")) - t0).total_seconds()
        claims, winners = parse_claims(r[f"{PEER}_claim_details"])
        out.append(Row(t, r, claims, winners))
    return out


def between(rows: List[Row], start: float, end: float) -> List[Row]:
    return [r for r in rows if start <= r.t < end]


def delivered_wh(rows: List[Row], tick: int) -> float:
    return sum(r.actual_w for r in rows) * tick / 3600.0


# ── Arbitration oracle ──────────────────────────────────────────────────────

def winning_state(row: Row, default_active: bool) -> str:
    """The state EvseManager::evaluateClaims should pick for these claims."""
    best: Optional[Claim] = None
    for c in row.claims:
        if c.state in ("active", "disabled") and (best is None or c.priority > best.priority):
            best = c
    if best is None:
        return "active" if default_active else "disabled"
    return best.state


def pilot_bounds(row: Row, hw_max: int = 32, hw_min: int = 6) -> Tuple[int, int]:
    """(lowest, highest) pilot the winning claims allow while charging."""
    cap = min([hw_max] + [c.max_current for c in row.claims if c.max_current is not None])
    want: Optional[int] = None
    best_prio = -1
    for c in row.claims:
        if c.charge_current is not None and c.priority > best_prio:
            want, best_prio = c.charge_current, c.priority
    target = min(cap, want) if want is not None else cap
    return max(hw_min, min(target, cap)), max(hw_min, cap)


def arbitration_violations(rows: List[Row], default_active: bool,
                           max_current_soft: int = 32) -> List[str]:
    """Rows where the EVSE disobeys its own winning claims.

    A charging EVSE must have an active winner, and its pilot must sit between
    the winning charge current and the tightest max_current. Rows right after a
    change are skipped: the controller follows a claim on the next poll.
    """
    problems = []
    for prev, row in zip(rows, rows[1:]):
        if prev.raw[f"{PEER}_claim_details"] != row.raw[f"{PEER}_claim_details"]:
            continue
        if row.charging and winning_state(row, default_active) == "disabled":
            problems.append(f"t={row.t:.0f}: charging while winning claim is disabled ({row['claim_details']})")
        if row.charging:
            lo, hi = pilot_bounds(row, hw_max=max_current_soft)
            if not (lo <= row.pilot <= hi):
                problems.append(f"t={row.t:.0f}: pilot {row.pilot}A outside [{lo},{hi}] ({row['claim_details']})")
    return problems


def all_subsets(items: Sequence[str]) -> List[Tuple[str, ...]]:
    return [c for n in range(len(items) + 1) for c in itertools.combinations(items, n)]


# ── Showcase scenarios for view.html ────────────────────────────────────────

def showcase() -> Dict[str, Dict[str, Any]]:
    """A few Charge Manager setups worth looking at in the scenario viewer.

    Regenerate data/scenarios/charge_manager_*.json with:
        python3 charge_manager.py
    """
    from test_charge_manager_combinations import Combo  # the shared day script

    picks = {
        "everything_always_on": ("Charge Manager: every feature always active",
                                 Combo(ALWAYS_ON_FEATURES, temp=True)),
        "eco_rule_with_shaping": ("Charge Manager: 09:00-11:00 eco rule, shaping always on",
                                  Combo(("shaping",), default_active=False, action="eco_divert")),
        "rfid_rule_with_limit": ("Charge Manager: 09:00-11:00 RFID rule, session limit always on",
                                 Combo(("session_limit",), action="rfid")),
        "charge_rule_current_limit": ("Charge Manager: 09:00-11:00 charge rule at 10 A / 4 kWh",
                                      Combo((), default_active=False, action="charge",
                                            rule_current=10, rule_limit=("energy", 4000))),
        "ocpp_and_rfid": ("Charge Manager: OCPP and RFID always active",
                          Combo(("ocpp", "rfid"))),
    }
    out = {}
    for key, (title, combo) in picks.items():
        doc = combo.doc()
        doc["meta"] = {"id": f"charge_manager_{key}", "title": title,
                       "category": "charge_manager", "profile": key}
        out[f"charge_manager_{key}"] = doc
    return out


if __name__ == "__main__":
    import json
    from pathlib import Path

    target = Path(__file__).resolve().parent / "data" / "scenarios"
    for name, doc in showcase().items():
        path = target / f"{name}.json"
        path.write_text(json.dumps(doc, indent=2) + "\n")
        print(f"wrote {path.relative_to(target.parent.parent)}")
