#!/usr/bin/env python3
"""Replay a charger's /debug/replay package through the simulator.

A user downloads the package from their charger (Settings > Terminal, or
http://<charger>/debug/replay?download=1). This tool turns it into a
divert_sim scenario -- the same configuration, schedule, solar/grid/site
power, voltage, temperature, plug events, and the claims made by things the
simulator does not run itself (manual override, OCPP, MQTT, evcc, ...) --
runs it through the firmware logic, and reports where the simulated charger
did something different from the real one.

    python3 replay.py package.json                  # convert, run, report
    python3 replay.py package.json -o scenario.json # also keep the scenario
    python3 replay.py package.json --install        # add it to the viewer
    python3 replay.py package.json --compare out.csv

Package format: src/replay_format.h / src/replay_recorder.cpp (version 1).
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

PEER = "replay"
TICK = 5
FALLBACK_START = 1704067200  # 2024-01-01T00:00:00Z, when the charger had no clock

# EvseClient ids (src/evse_man.h): EVC(vendor, client) = vendor << 16 | client.
OPENEVSE = 0x0001 << 16
CLIENT_NAMES = {
    OPENEVSE | 0x01: "manual",
    OPENEVSE | 0x02: "divert",
    OPENEVSE | 0x03: "boost",
    OPENEVSE | 0x04: "schedule",
    OPENEVSE | 0x06: "limit",
    OPENEVSE | 0x07: "error",
    OPENEVSE | 0x09: "ocpp",
    OPENEVSE | 0x0A: "rfid",
    OPENEVSE | 0x0B: "mqtt",
    OPENEVSE | 0x0C: "shaper",
    OPENEVSE | 0x0D: "temp_throttle",
    OPENEVSE | 0x0E: "loadsharing",
    (0x0004 << 16) | 0x01: "evcc",
}
# Claims the simulator recreates by running the firmware module itself. Every
# other client's claims are replayed as recorded.
NATIVE_CLIENTS = {"divert", "boost", "schedule", "limit", "rfid", "shaper",
                  "temp_throttle", "loadsharing"}

STATE_NAMES = {
    1: "ready", 2: "connected", 3: "charging", 4: "vent_required",
    5: "diode_check_failed", 6: "gfi_fault", 7: "no_ground", 8: "stuck_relay",
    9: "gfi_self_test_failed", 10: "over_temperature", 11: "over_current",
    254: "sleeping", 255: "disabled",
}

FLAG_VEHICLE = 1 << 0
FLAG_CHARGING = 1 << 1
FLAG_TARGET_ACTIVE = 1 << 2

RFID_TAG = "REPLAY01"


@dataclass
class Sample:
    t: int            # seconds from the first sample
    up: int
    solar_w: float
    grid_ie_w: float
    live_pwr_w: float
    volts: float
    amps: float
    temp_c: Optional[float]
    session_wh: float
    pilot: int
    state: int
    soc: Optional[int]
    target_current: int
    flags: int

    @property
    def vehicle(self) -> bool:
        return bool(self.flags & FLAG_VEHICLE)

    @property
    def charging(self) -> bool:
        return self.state == 3

    @property
    def target_active(self) -> bool:
        return bool(self.flags & FLAG_TARGET_ACTIVE)


@dataclass
class Replay:
    scenario: Dict[str, Any]
    samples: List[Sample]
    warnings: List[str] = field(default_factory=list)


def load_package(path: str) -> Dict[str, Any]:
    with open(path) as f:
        pkg = json.load(f)
    if pkg.get("format") != "openevse-replay":
        raise ValueError(f"{path}: not an OpenEVSE replay package")
    if pkg.get("version") != 1:
        raise ValueError(f"{path}: unsupported package version {pkg.get('version')}")
    return pkg


def parse_samples(pkg: Dict[str, Any]) -> List[Sample]:
    cols = pkg["samples"]["columns"]
    rows = pkg["samples"]["data"]
    if not rows:
        raise ValueError("package has no samples (the charger had only just started)")
    first_up = rows[0][cols.index("up")]
    out = []
    for row in rows:
        r = dict(zip(cols, row))
        out.append(Sample(
            t=r["up"] - first_up, up=r["up"],
            solar_w=r["solar_w"], grid_ie_w=r["grid_ie_w"], live_pwr_w=r["live_pwr_w"],
            volts=r["volts"], amps=r["amps"], temp_c=r["temp_c"],
            session_wh=r["session_wh"], pilot=r["pilot"], state=r["state"],
            soc=r["soc"], target_current=r["target_current"], flags=r["flags"],
        ))
    return out


def _series(samples: List[Sample], attr: str) -> Optional[List[Dict[str, float]]]:
    values = [(s.t, getattr(s, attr)) for s in samples if getattr(s, attr) is not None]
    if not values or all(v == 0 for _, v in values):
        return None
    # Only keep changes: valueAt() holds the last value anyway.
    out, last = [], object()
    for t, v in values:
        if v != last:
            out.append({"time": t, "value": v})
            last = v
    return out


def _client_name(client: int) -> str:
    return CLIENT_NAMES.get(client, str(client))


def _sample_at(samples: List[Sample], t: float) -> Sample:
    """The first sample at or after t (the last one if t is past the end)."""
    for s in samples:
        if s.t >= t:
            return s
    return samples[-1]


def _ev_model(samples: List[Sample], hw_max: int, warnings: List[str]) -> Dict[str, Any]:
    ev: Dict[str, Any] = {}
    socs = [s for s in samples if s.soc is not None]
    if socs:
        ev["initial_soc"] = socs[0].soc
        ev["report_soc"] = True
        # Capacity from energy over SoC gained, when the SoC moved enough.
        gained = socs[-1].soc - socs[0].soc
        wh = socs[-1].session_wh - socs[0].session_wh
        if gained >= 3 and wh > 0:
            ev["battery_capacity_kwh"] = round(wh / gained * 100 / 1000, 1)
    else:
        ev["initial_soc"] = 50
        warnings.append("no vehicle SoC in the recording: the EV model starts at 50 %")

    # An EV drawing well under the pilot is limiting itself: that is its rate.
    limited = [s.amps * s.volts for s in samples if s.charging and s.amps < s.pilot - 1.5]
    if limited:
        ev["max_charge_rate_kw"] = round(max(limited) / 1000, 2)
    else:
        volts = statistics.median([s.volts for s in samples if s.volts > 0] or [240])
        ev["max_charge_rate_kw"] = round(hw_max * volts / 1000, 2)
    return ev


def _ev_events(samples: List[Sample], warnings: List[str]) -> List[Dict[str, Any]]:
    """Plug in/out, and the EV pausing its own charge (connected, offered
    current, but not drawing for two samples in a row)."""
    events: List[Dict[str, Any]] = []
    vehicle = samples[0].vehicle
    requesting = True
    for prev, s, nxt in zip(samples, samples[1:], samples[2:] + samples[-1:]):
        if s.vehicle != vehicle:
            events.append({"time": s.t, "vehicle": s.vehicle})
            vehicle = s.vehicle
            if vehicle and not requesting:
                events.append({"time": s.t, "request_current": True})
                requesting = True
        if not s.vehicle:
            continue
        offered = s.target_active and s.pilot >= 6 and prev.target_active
        idle = s.state == 2 and nxt.state == 2 and s.amps < 0.5
        if requesting and offered and idle:
            events.append({"time": s.t, "request_current": False})
            requesting = False
        elif not requesting and s.charging:
            events.append({"time": prev.t, "request_current": True})
            requesting = True
    if any("request_current" in e for e in events):
        warnings.append("the EV paused charging on its own; replayed as request_current events")
    return events


def _claim_events(pkg: Dict[str, Any], first_up: int, samples: List[Sample],
                  warnings: List[str]) -> List[Dict[str, Any]]:
    cfg = pkg.get("config", {})
    default_limit = (cfg.get("limit_default_type") or "none", cfg.get("limit_default_value", 0))
    schedule_limits = {(e.get("limit"), e.get("limit_value")) for e in pkg.get("schedule", [])
                       if e.get("limit") not in (None, "none")}
    rfid_required = bool(cfg.get("rfid_enabled")) or any(
        e.get("feature") == "rfid" for e in pkg.get("schedule", []))

    events: List[Dict[str, Any]] = []
    if pkg["events"].get("overwritten"):
        warnings.append(f"{pkg['events']['overwritten']} older events were overwritten on the "
                        "charger; claims made before them may be missing")

    for e in pkg["events"]["data"]:
        t = max(0, e["up"] - first_up)
        kind = e["type"]
        if kind in ("claim", "release"):
            name = _client_name(e["client"])
            if name in NATIVE_CLIENTS:
                continue
            client: Any = name if name in CLIENT_NAMES.values() else e["client"]
            if kind == "release":
                events.append({"time": t, "release": client})
                continue
            claim = {"client": client, "priority": e["priority"],
                     "auto_release": e.get("auto_release", False)}
            for key in ("state", "charge_current", "max_current"):
                if key in e:
                    claim[key] = e[key]
            events.append({"time": t, "claim": claim})
        elif kind == "limit":
            setting = (e["limit_type"], e["value"])
            if setting == default_limit or setting in schedule_limits:
                continue  # the simulator's Limit / Scheduler re-create these
            if e["limit_type"] == "none":
                events.append({"time": t, "limit": "clear"})
            else:
                events.append({"time": t, "limit": {"type": e["limit_type"], "value": e["value"],
                                                    "auto_release": e.get("auto_release", True)}})
        elif kind == "boost":
            if e["limit_type"] == "none":
                events.append({"time": t, "boost": "cancel"})
            else:
                events.append({"time": t, "boost": {"type": e["limit_type"], "value": e["value"]}})
        elif kind == "rfid_auth":
            events.append({"time": t, "rfid": RFID_TAG})
        elif kind == "rfid_deauth":
            # Unplugging ends it by itself; a deauth while plugged in was a tap.
            if rfid_required and _sample_at(samples, t).vehicle:
                events.append({"time": t, "rfid": RFID_TAG})
        elif kind == "schedule" and t > 0:
            warnings.append(f"t={t}s: the schedule was rebuilt (edited, or a config change); "
                            "the replay uses the schedule as downloaded throughout")
        elif kind == "config" and t > 0:
            warnings.append(f"t={t}s: the configuration changed; the replay uses the "
                            "configuration as downloaded throughout")
    return events


def package_to_scenario(pkg: Dict[str, Any], name: str = "replay") -> Replay:
    warnings: List[str] = []
    samples = parse_samples(pkg)
    first_up = samples[0].up
    hw = pkg.get("hardware", {})
    hw_max = int(hw.get("max_current") or 32)
    hw_min = int(hw.get("min_current") or 6)

    boot = pkg.get("boot_epoch")
    if boot is None:
        start = FALLBACK_START
        warnings.append("the charger had no wall-clock time: scheduled rules will not line up")
    else:
        start = boot + first_up

    cfg = {k: v for k, v in pkg.get("config", {}).items() if k not in ("version", "buildenv")}
    if cfg.get("scheduler_start_window") and pkg.get("schedule"):
        warnings.append("scheduler_start_window randomises rule start times on the charger; "
                        "the replay starts rules on time")
    cfg["scheduler_start_window"] = 0

    volts = [s.volts for s in samples if s.volts >= 100]
    peer: Dict[str, Any] = {
        "id": PEER,
        "voltage": round(statistics.median(volts)) if volts else 240,
        "min_current": hw_min,
        "max_current": hw_max,
        "ev": _ev_model(samples, hw_max, warnings),
        "initial": {"vehicle": samples[0].vehicle},
        "inputs": {},
        "events": [],
    }
    for key, attr in (("solar", "solar_w"), ("grid_ie", "grid_ie_w"),
                      ("live_pwr", "live_pwr_w"), ("vrms", "volts"),
                      ("temperature", "temp_c")):
        series = _series(samples, attr)
        if series:
            peer["inputs"][key] = series

    events = _ev_events(samples, warnings) + _claim_events(pkg, first_up, samples, warnings)
    peer["events"] = sorted(events, key=lambda e: e["time"])

    if pkg["samples"].get("overwritten"):
        warnings.append("the recording covers only the most recent part of the session")
    for section in ("samples", "events"):
        if pkg[section].get("truncated"):
            warnings.append(f"the charger was short of memory and left out the "
                            f"{pkg[section]['truncated']} oldest {section}")

    scenario = {
        "meta": {
            "id": name,
            "title": f"Replay: {pkg.get('firmware', '?')} "
                     f"{datetime.fromtimestamp(start, timezone.utc):%Y-%m-%d %H:%M}Z",
            "category": "replay",
            "profile": name,
        },
        "simulation": {
            "duration": samples[-1].t + pkg["samples"].get("interval", 10),
            "tick_interval": TICK,
            "start_time": datetime.fromtimestamp(start, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        },
        "config": cfg,
        "schedule": pkg.get("schedule", []),
        "peers": [peer],
    }
    return Replay(scenario, samples, warnings)


# ── Comparison ──────────────────────────────────────────────────────────────

@dataclass
class Comparison:
    rows: List[Dict[str, Any]]
    agreement: float          # share of samples where charging matches
    pilot_mae: Optional[float]
    divergences: List[Tuple[int, int, str]]  # (start t, end t, description)


def compare(replay: Replay, sim_rows: List[Dict[str, str]]) -> Comparison:
    by_t: Dict[int, Dict[str, str]] = {}
    t0 = datetime.fromisoformat(sim_rows[0]["time"].replace("Z", "+00:00"))
    for r in sim_rows:
        t = int((datetime.fromisoformat(r["time"].replace("Z", "+00:00")) - t0).total_seconds())
        by_t[t] = r

    rows, agree, pilot_err = [], 0, []
    for s in replay.samples:
        sim = by_t.get(s.t) or by_t[min(by_t, key=lambda t: abs(t - s.t))]
        sim_state = sim[f"{PEER}_state"]
        sim_charging = sim_state == "charging"
        same = sim_charging == s.charging
        agree += same
        sim_pilot = int(sim[f"{PEER}_pilot_a"])
        if s.charging and sim_charging:
            pilot_err.append(abs(sim_pilot - s.pilot))
        rows.append({
            "t": s.t,
            "recorded_state": STATE_NAMES.get(s.state, str(s.state)),
            "simulated_state": sim_state,
            "recorded_pilot": s.pilot,
            "simulated_pilot": sim_pilot,
            "recorded_amps": s.amps,
            "simulated_amps": round(float(sim[f"{PEER}_actual_charge_w"]) / max(s.volts, 1), 2),
            "match": "yes" if same and (not s.charging or abs(sim_pilot - s.pilot) <= 1) else "no",
            "simulated_claims": sim[f"{PEER}_claim_details"],
        })

    # Group consecutive mismatches into intervals.
    divergences: List[Tuple[int, int, str]] = []
    run: List[Dict[str, Any]] = []
    for row in rows + [{"match": "yes"}]:
        if row["match"] == "no":
            run.append(row)
            continue
        if run:
            a = run[0]
            desc = (f"device {a['recorded_state']} @ {a['recorded_pilot']}A, "
                    f"sim {a['simulated_state']} @ {a['simulated_pilot']}A "
                    f"[{a['simulated_claims']}]")
            divergences.append((run[0]["t"], run[-1]["t"], desc))
            run = []

    return Comparison(rows, agree / len(rows),
                      statistics.mean(pilot_err) if pilot_err else None, divergences)


def run(pkg_path: str, scenario_out: Optional[str] = None, install: bool = False,
        compare_out: Optional[str] = None, quiet: bool = False) -> Comparison:
    from run_simulations import SCENARIO_DIR, run_scenario_doc

    pkg = load_package(pkg_path)
    name = "replay_" + Path(pkg_path).stem.replace("-", "_").replace(".", "_")
    replay = package_to_scenario(pkg, name)
    if scenario_out:
        Path(scenario_out).write_text(json.dumps(replay.scenario, indent=2) + "\n")
    if install:
        (SCENARIO_DIR / f"{name}.json").write_text(json.dumps(replay.scenario, indent=2) + "\n")

    sim_rows = run_scenario_doc(replay.scenario)
    result = compare(replay, sim_rows)

    if compare_out:
        import csv
        with open(compare_out, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(result.rows[0]))
            w.writeheader()
            w.writerows(result.rows)

    if not quiet:
        print(f"{pkg.get('firmware')} ({pkg.get('buildenv')}): "
              f"{len(replay.samples)} samples over {replay.samples[-1].t}s, "
              f"{len(replay.scenario['peers'][0]['events'])} replayed events")
        for w in replay.warnings:
            print(f"  note: {w}")
        print(f"charging state matches {result.agreement:.0%} of samples", end="")
        if result.pilot_mae is not None:
            print(f"; pilot off by {result.pilot_mae:.1f} A on average while both charge")
        else:
            print()
        if result.divergences:
            print("where the simulation differs from the device:")
            for a, b, desc in result.divergences:
                print(f"  t={a}..{b}s  {desc}")
        else:
            print("the simulation reproduces the recorded behaviour")
    return result


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("package", help="replay package downloaded from /debug/replay")
    ap.add_argument("-o", "--scenario", help="write the generated scenario JSON here")
    ap.add_argument("--install", action="store_true",
                    help="also save the scenario under data/scenarios for the viewer")
    ap.add_argument("--compare", help="write a per-sample recorded vs simulated CSV here")
    args = ap.parse_args(argv)
    run(args.package, args.scenario, args.install, args.compare)
    return 0


if __name__ == "__main__":
    sys.exit(main())
