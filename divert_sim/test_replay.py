#!/usr/bin/env python3
"""Replay packages (/debug/replay) through the simulator.

data/replay/native_eco_manual_override.json was downloaded from the native
firmware build running against the OpenEVSE emulator: eco mode following a
solar profile (0 -> 4 kW -> 1.5 kW -> 0) with a manual "disabled" override
for ~50 s in the middle.
"""

import copy
import json
import re
from pathlib import Path

import pytest

import replay
from run_simulations import run_scenario_doc

ROOT = Path(__file__).resolve().parent
CAPTURE = ROOT / "data" / "replay" / "native_eco_manual_override.json"
MANUAL = 0x10001


def load():
    return replay.load_package(str(CAPTURE))


def events_of(scn):
    return scn["peers"][0]["events"]


# ── Round trip against a real capture ───────────────────────────────────────

def test_capture_replays_like_the_device():
    result = replay.run(str(CAPTURE), quiet=True)
    assert result.agreement >= 0.95, result.divergences
    assert result.pilot_mae is not None and result.pilot_mae <= 2
    # Plug-in and override edges may lag a tick; nothing longer.
    assert all(b - a <= 10 for a, b, _ in result.divergences), result.divergences


def test_capture_converts_inputs_and_external_claims():
    r = replay.package_to_scenario(load())
    scn = r.scenario
    peer = scn["peers"][0]
    # Solar drove divert; it is replayed as an input, not as divert claims.
    assert [p["value"] for p in peer["inputs"]["solar"]] == [0, 4000, 1500, 0]
    claims = [e for e in events_of(scn) if "claim" in e or "release" in e]
    assert [("claim" in e, (e.get("claim") or {}).get("client", e.get("release")))
            for e in claims] == [(True, "manual"), (False, "manual")]
    assert claims[0]["claim"]["state"] == "disabled"
    assert claims[0]["claim"]["priority"] == 1000
    # Configuration and clock come from the device.
    assert scn["config"]["divert_enabled"] is True
    assert scn["config"]["charge_mode"] == "eco"
    assert scn["config"]["scheduler_start_window"] == 0
    assert scn["simulation"]["start_time"].startswith("2026-10-07T19:44")


def test_capture_config_is_only_allowlisted_keys():
    """The Python side re-checks the firmware's allowlist: nothing outside
    src/replay_redact.cpp may appear in a package."""
    source = (ROOT.parent / "src" / "replay_redact.cpp").read_text()
    block = source[source.index("ALLOWED[] = {"):source.index("};", source.index("ALLOWED[] = {"))]
    allowed = set(re.findall(r'"([a-zA-Z0-9_]+)"', block)) | {"rfid_storage"}
    assert set(load()["config"]) <= allowed


# ── Converter rules, on synthetic packages ──────────────────────────────────

def package(samples, events=(), config=None, schedule=(), boot_epoch=1704103200):
    cols = ["up", "solar_w", "grid_ie_w", "live_pwr_w", "volts", "amps", "temp_c",
            "session_wh", "pilot", "state", "soc", "target_current", "flags"]
    return {
        "format": "openevse-replay", "version": 1, "firmware": "test", "buildenv": "test",
        "uptime": samples[-1][0], "boot_epoch": boot_epoch,
        "hardware": {"min_current": 6, "max_current": 32, "max_configured": 32, "voltage": 240},
        "config": {"time_zone": "UTC|UTC0", "default_state": True, **(config or {})},
        "schedule": list(schedule),
        "state": {},
        "samples": {"interval": 10, "columns": cols, "overwritten": 0, "data": samples},
        "events": {"overwritten": 0, "data": list(events)},
    }


def row(up, state=3, pilot=32, amps=None, flags=None, soc=None, temp=25.0):
    charging = state == 3
    amps = (pilot if charging else 0) if amps is None else amps
    if flags is None:
        flags = (1 if state in (2, 3, 254) else 0) | (2 if charging else 0) | 4
    return [up, 0, 0, 0, 240.0, amps, temp, 0, pilot, state, soc, pilot, flags]


def test_native_clients_are_not_injected():
    pkg = package([row(t) for t in range(0, 300, 10)], events=[
        {"up": 5, "type": "claim", "client": 0x10002, "priority": 50, "state": "active"},
        {"up": 6, "type": "claim", "client": 0x10004, "priority": 100, "state": "active"},
        {"up": 7, "type": "claim", "client": 0x10009, "priority": 1050, "state": "disabled"},
        {"up": 8, "type": "claim", "client": 262145, "priority": 500, "charge_current": 12},
        {"up": 9, "type": "claim", "client": 999999, "priority": 500, "state": "active"},
        {"up": 50, "type": "release", "client": 0x10009, "priority": 1050},
    ])
    ev = events_of(replay.package_to_scenario(pkg).scenario)
    injected = [(e.get("claim") or {}).get("client", e.get("release")) for e in ev
                if "claim" in e or "release" in e]
    assert injected == ["ocpp", "evcc", 999999, "ocpp"]


def test_events_before_the_window_apply_at_start():
    pkg = package([row(t) for t in range(600, 900, 10)], events=[
        {"up": 100, "type": "claim", "client": 0x10001, "priority": 1000, "state": "disabled"},
    ])
    ev = events_of(replay.package_to_scenario(pkg).scenario)
    assert ev[0]["time"] == 0 and ev[0]["claim"]["client"] == "manual"


def test_default_and_schedule_limits_are_left_to_the_firmware():
    sched = [{"id": 1, "time": "09:00", "state": "active", "days": ["monday"],
              "limit": "energy", "limit_value": 3000}]
    pkg = package([row(t) for t in range(0, 300, 10)], config={
        "limit_default_type": "time", "limit_default_value": 60}, schedule=sched, events=[
        {"up": 0, "type": "limit", "limit_type": "time", "value": 60, "auto_release": False},
        {"up": 10, "type": "limit", "limit_type": "energy", "value": 3000, "auto_release": True},
        {"up": 20, "type": "limit", "limit_type": "soc", "value": 80, "auto_release": True},
        {"up": 30, "type": "limit", "limit_type": "none", "value": 0, "auto_release": True},
    ])
    ev = [e for e in events_of(replay.package_to_scenario(pkg).scenario) if "limit" in e]
    assert ev == [{"time": 20, "limit": {"type": "soc", "value": 80, "auto_release": True}},
                  {"time": 30, "limit": "clear"}]


def test_rfid_auth_and_tap_off_become_card_taps():
    samples = [row(t) for t in range(0, 300, 10)]
    samples += [row(t, state=1, flags=4) for t in range(300, 400, 10)]
    pkg = package(samples, config={"rfid_enabled": True, "rfid_storage": "REPLAY01"}, events=[
        {"up": 40, "type": "rfid_auth"},
        {"up": 120, "type": "rfid_deauth"},   # plugged in: a tap
        {"up": 150, "type": "rfid_auth"},
        {"up": 310, "type": "rfid_deauth"},   # unplugged: not a tap
    ])
    taps = [e["time"] for e in events_of(replay.package_to_scenario(pkg).scenario) if "rfid" in e]
    assert taps == [40, 120, 150]


def test_plug_events_and_ev_pausing_itself():
    samples = ([row(t, state=1, flags=4) for t in range(0, 60, 10)] +
               [row(t) for t in range(60, 200, 10)] +
               [row(t, state=2) for t in range(200, 300, 10)] +   # offered 32 A, not drawing
               [row(t) for t in range(300, 400, 10)])
    r = replay.package_to_scenario(package(samples))
    ev = events_of(r.scenario)
    assert {"time": 60, "vehicle": True} in ev
    assert {"time": 200, "request_current": False} in ev
    assert {"time": 290, "request_current": True} in ev
    assert r.scenario["peers"][0]["initial"]["vehicle"] is False
    rows = run_scenario_doc(r.scenario)
    result = replay.compare(r, rows)
    assert result.agreement >= 0.9, result.divergences


def test_ev_limited_rate_is_learnt():
    pkg = package([row(t, pilot=32, amps=16) for t in range(0, 300, 10)])
    ev = replay.package_to_scenario(pkg).scenario["peers"][0]["ev"]
    assert ev["max_charge_rate_kw"] == pytest.approx(16 * 240 / 1000)


def test_soc_and_capacity_are_learnt():
    samples = []
    for i, t in enumerate(range(0, 3600, 10)):
        r = row(t, soc=40 + i // 36)
        r[7] = i * 20  # session_wh: 7.2 kWh/h
        samples.append(r)
    ev = replay.package_to_scenario(package(samples)).scenario["peers"][0]["ev"]
    assert ev["initial_soc"] == 40 and ev["report_soc"] is True
    assert 60 <= ev["battery_capacity_kwh"] <= 90


def test_no_clock_is_flagged():
    r = replay.package_to_scenario(package([row(t) for t in range(0, 100, 10)], boot_epoch=None))
    assert any("wall-clock" in w for w in r.warnings)
    assert r.scenario["simulation"]["start_time"] == "2024-01-01T00:00:00Z"


def test_rejects_other_files():
    bad = copy.deepcopy(load())
    bad["format"] = "something-else"
    p = ROOT / "output" / "not_a_package.json"
    p.parent.mkdir(exist_ok=True)
    p.write_text(json.dumps(bad))
    with pytest.raises(ValueError):
        replay.load_package(str(p))
