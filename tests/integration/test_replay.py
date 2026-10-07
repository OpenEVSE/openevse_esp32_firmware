"""Integration tests for /debug/replay, the replay package download.

Runs against the paired emulator + native firmware fixture from conftest.py.
The package is what divert_sim/replay.py feeds back through the simulator, so
these pin down the parts it relies on: the shape, the recorded claims, and
that the configuration only carries allowlisted keys.
"""

import re
import time
from pathlib import Path

import requests

REQUEST_TIMEOUT = 15
HEADERS = {"X-Requested-With": "OpenEVSE"}
MANUAL_CLIENT = 0x10001
REDACT_SOURCE = Path(__file__).resolve().parents[2] / "src" / "replay_redact.cpp"


def get_package(native_url, **params):
    r = requests.get(f"{native_url}/debug/replay", params=params, timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"{r.status_code}: {r.text[:200]}"
    return r


def allowlist():
    src = REDACT_SOURCE.read_text()
    start = src.index("ALLOWED[] = {")
    return set(re.findall(r'"([a-zA-Z0-9_]+)"', src[start:src.index("};", start)]))


class TestReplayPackage:
    def test_package_shape(self, evse_instance):
        pkg = get_package(evse_instance["native_url"]).json()
        assert pkg["format"] == "openevse-replay"
        assert pkg["version"] == 1
        for key in ("firmware", "uptime", "time_zone", "hardware", "config",
                    "schedule", "state", "samples", "events"):
            assert key in pkg, key
        cols = pkg["samples"]["columns"]
        assert cols[0] == "up" and "pilot" in cols and "state" in cols
        assert all(len(row) == len(cols) for row in pkg["samples"]["data"])
        assert pkg["events"]["data"][0]["type"] == "boot"

    def test_download_names_the_file(self, evse_instance):
        r = get_package(evse_instance["native_url"], download=1)
        disposition = r.headers.get("Content-Disposition", "")
        assert disposition.startswith("attachment;")
        assert "openevse-replay-" in disposition and disposition.endswith('.json"')
        assert "no-store" in r.headers.get("Cache-Control", "")

    def test_config_is_allowlisted(self, evse_instance):
        native_url = evse_instance["native_url"]
        # Put a credential in the config to prove it stays out.
        r = requests.post(f"{native_url}/config", json={"mqtt_pass": "hunter2-replay"},
                          headers=HEADERS, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200, r.text
        r = get_package(native_url)
        assert "hunter2-replay" not in r.text
        assert set(r.json()["config"]) <= allowlist() | {"rfid_storage"}

    def test_manual_override_is_recorded(self, evse_instance):
        native_url = evse_instance["native_url"]
        r = requests.post(f"{native_url}/override", json={"state": "disabled"},
                          headers=HEADERS, timeout=REQUEST_TIMEOUT)
        assert r.status_code in (200, 201), r.text
        # The recorder polls once a second.
        deadline = time.time() + 10
        claims = []
        while time.time() < deadline:
            events = get_package(native_url).json()["events"]["data"]
            claims = [e for e in events if e["type"] == "claim" and e["client"] == MANUAL_CLIENT]
            if claims:
                break
            time.sleep(1)
        assert claims, "manual override claim not recorded"
        assert claims[-1]["state"] == "disabled"
        assert claims[-1]["priority"] == 1000

        requests.delete(f"{native_url}/override", headers=HEADERS, timeout=REQUEST_TIMEOUT)
        deadline = time.time() + 10
        released = []
        while time.time() < deadline and not released:
            events = get_package(native_url).json()["events"]["data"]
            released = [e for e in events if e["type"] == "release" and e["client"] == MANUAL_CLIENT]
            time.sleep(1)
        assert released, "override release not recorded"

    def test_samples_accumulate(self, evse_instance):
        native_url = evse_instance["native_url"]
        first = len(get_package(native_url).json()["samples"]["data"])
        time.sleep(12)
        second = len(get_package(native_url).json()["samples"]["data"])
        assert second > first

    def test_only_get_is_allowed(self, evse_instance):
        r = requests.post(f"{evse_instance['native_url']}/debug/replay", json={},
                          headers=HEADERS, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 405
