"""Integration tests for heartbeat supervision (the ``$SY`` interval/current).

The native build keeps heartbeat support compiled in, and the emulator models
the controller's supervision, so what the firmware pushes to the controller can
be read back from the emulator's ``/api/evse/status`` ``heartbeat`` block.

Covers the firmware sending the *configured* values at boot rather than the
compile-time defaults: a disabled heartbeat must stay disabled and a changed
current must survive a reboot.
"""

import time

import pytest
import requests

REQUEST_TIMEOUT = 15
SETTLE_TIMEOUT = 30


def hb_status(emulator_url):
    r = requests.get(f"{emulator_url}/api/evse/status", timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"/api/evse/status: {r.status_code}: {r.text}"
    return r.json()["heartbeat"]


def wait_for_hb(emulator_url, predicate, timeout=SETTLE_TIMEOUT):
    """Poll the controller's heartbeat state until predicate(hb) holds."""
    deadline = time.time() + timeout
    hb = None
    while time.time() < deadline:
        hb = hb_status(emulator_url)
        if predicate(hb):
            return hb
        time.sleep(0.5)
    pytest.fail(f"heartbeat state never satisfied the condition; last: {hb}")


def controller_set(native_url, cmd):
    """Send a raw RAPI command to the controller through the /r passthrough."""
    r = requests.get(
        f"{native_url}/r",
        params={"json": 1, "rapi": cmd},
        headers={"X-Requested-With": "OpenEVSE"},
        timeout=REQUEST_TIMEOUT,
    )
    assert r.status_code == 200, f"/r {cmd}: {r.status_code}: {r.text}"


def set_config(native_url, **values):
    r = requests.post(f"{native_url}/config", json=values, timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"/config: {r.status_code}: {r.text}"


@pytest.mark.timeout(180)
class TestHeartbeatBoot:
    def test_default_config_arms_supervision(self, heartbeat_instance):
        hb = wait_for_hb(
            heartbeat_instance["emulator_url"], lambda h: h["interval"] > 0
        )
        assert hb["current_limit"] > 0

    def test_firmware_keeps_pulsing_without_a_trip(self, heartbeat_instance):
        url = heartbeat_instance["emulator_url"]
        wait_for_hb(url, lambda h: h["interval"] > 0)
        first = wait_for_hb(url, lambda h: h["pulses"] > 0)["pulses"]
        hb = wait_for_hb(url, lambda h: h["pulses"] > first, timeout=60)
        assert hb["trips"] == 0

    def test_runtime_change_reaches_controller(self, heartbeat_instance):
        url = heartbeat_instance["emulator_url"]
        set_config(heartbeat_instance["native_url"], heartbeat_interval=120,
                   heartbeat_current=10)
        hb = wait_for_hb(url, lambda h: h["interval"] == 120)
        assert hb["current_limit"] == 10


@pytest.mark.timeout(240)
class TestHeartbeatSurvivesReboot:
    def test_disabled_heartbeat_stays_disabled(self, heartbeat_instance):
        url = heartbeat_instance["emulator_url"]
        set_config(heartbeat_instance["native_url"], heartbeat_interval=0)
        wait_for_hb(url, lambda h: h["interval"] == 0)

        # The emulator outlives the firmware restart, so leave the controller
        # armed behind the firmware's back: only a boot that sends an explicit
        # disable can bring it back to 0.
        controller_set(heartbeat_instance["native_url"], "$SY 120 10")
        wait_for_hb(url, lambda h: h["interval"] == 120)

        heartbeat_instance["restart_native"]()
        hb = wait_for_hb(url, lambda h: h["interval"] == 0)
        assert hb["interval"] == 0, f"disabled heartbeat was re-armed: {hb}"

    def test_changed_current_is_not_reset_to_default(self, heartbeat_instance):
        url = heartbeat_instance["emulator_url"]
        set_config(heartbeat_instance["native_url"], heartbeat_interval=120,
                   heartbeat_current=10)
        wait_for_hb(url, lambda h: h["interval"] == 120 and h["current_limit"] == 10)

        # Change the controller's own values so retained emulator state cannot
        # satisfy the assertion; boot has to push the configured ones back.
        controller_set(heartbeat_instance["native_url"], "$SY 60 24")
        wait_for_hb(url, lambda h: h["interval"] == 60 and h["current_limit"] == 24)

        heartbeat_instance["restart_native"]()
        hb = wait_for_hb(url, lambda h: h["interval"] == 120)
        assert hb["current_limit"] == 10, hb
