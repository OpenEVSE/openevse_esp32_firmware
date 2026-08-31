"""Integration tests for the /boost REST contract.

Runs against the paired emulator + native firmware fixtures from conftest.py
(same harness as test_charging.py, which uses the ``evse_instance`` fixture and
its ``native_url`` entry).
"""

import time

import pytest
import requests

REQUEST_TIMEOUT = 10

# EvseManager_Priority_Boost, src/evse_man.h
BOOST_PRIORITY = 200

# app_config.h enum vehicle_data_src: VEHICLE_DATA_SRC_HTTP = 3
VEHICLE_DATA_SRC_HTTP = 3


def boost_claims(native_url):
    claims = requests.get(f"{native_url}/claims", timeout=REQUEST_TIMEOUT).json()
    return [c for c in claims if c.get("priority") == BOOST_PRIORITY]


class TestBoostRest:
    def test_get_idle_returns_empty_object(self, evse_instance):
        """GET /boost with no boost active: 200 + {} (capability probe)."""
        native_url = evse_instance["native_url"]
        r = requests.get(f"{native_url}/boost", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        assert r.json() == {}

    def test_delete_idle_is_404(self, evse_instance):
        native_url = evse_instance["native_url"]
        r = requests.delete(f"{native_url}/boost", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 404

    def test_post_malformed_is_400(self, evse_instance):
        native_url = evse_instance["native_url"]
        r = requests.post(f"{native_url}/boost", data="not json", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 400
        r = requests.post(f"{native_url}/boost", json={"type": "time"}, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 400
        r = requests.post(f"{native_url}/boost", json={"type": "nope", "value": 10}, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 400
        r = requests.post(f"{native_url}/boost", json={"type": "time", "value": 0}, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 400

    def test_post_soc_without_vehicle_data_is_422(self, evse_instance):
        """A soc boost is 422 while the EVSE has no vehicle SoC source.

        The firmware's vehicle-validity bits are set-once — nothing in the HTTP
        API clears them again — so this test cannot reset the harness into the
        no-vehicle-data state.  Instead it probes /status first: ``battery_level``
        is only emitted once EvseManager::isVehicleStateOfChargeValid() is true
        (src/web_server.cpp buildStatus).  If an earlier test in the session
        pushed SoC data (test_charging.py::test_status_post_vehicle_data does),
        the 422 precondition no longer holds and we skip rather than fail on a
        collection-order accident.
        """
        native_url = evse_instance["native_url"]
        status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()
        if "battery_level" in status:
            pytest.skip(
                "EVSE already has vehicle SoC data (battery_level="
                f"{status['battery_level']}); vehicle validity cannot be reset "
                "via the API, so the 422 precondition is unavailable in this "
                "session. Run test_boost.py on its own to cover it."
            )

        r = requests.post(f"{native_url}/boost", json={"type": "soc", "value": 80}, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 422, (
            f"Expected 422 with no vehicle SoC data, got {r.status_code}: {r.text}"
        )

    def test_time_boost_lifecycle(self, evse_instance):
        native_url = evse_instance["native_url"]

        # Arm
        r = requests.post(f"{native_url}/boost", json={"type": "time", "value": 3600}, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 201

        # Reported active with a counting-down remaining
        r = requests.get(f"{native_url}/boost", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        body = r.json()
        assert body["type"] == "time"
        assert body["value"] == 3600
        assert 0 < body["remaining"] <= 3600
        assert body["started"].endswith("Z")

        # /status reflects it
        status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()
        assert status["boost"] is True
        first_version = status["boost_version"]

        # Claim exists at the Boost priority
        claims = boost_claims(native_url)
        assert len(claims) == 1
        assert claims[0]["state"] == "active"

        # Re-POST replaces (version bumps)
        r = requests.post(f"{native_url}/boost", json={"type": "time", "value": 60}, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 201
        status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()
        assert status["boost_version"] != first_version

        # Cancel
        r = requests.delete(f"{native_url}/boost", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        r = requests.get(f"{native_url}/boost", timeout=REQUEST_TIMEOUT)
        assert r.json() == {}
        status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()
        assert status["boost"] is False
        assert not boost_claims(native_url)

    def test_time_value_clamped_to_seven_days(self, evse_instance):
        native_url = evse_instance["native_url"]
        r = requests.post(
            f"{native_url}/boost",
            json={"type": "time", "value": 100 * 24 * 3600},
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 201
        body = requests.get(f"{native_url}/boost", timeout=REQUEST_TIMEOUT).json()
        assert body["value"] == 7 * 24 * 3600
        assert requests.delete(f"{native_url}/boost", timeout=REQUEST_TIMEOUT).status_code == 200

    def test_already_met_soc_target_arms_as_a_noop(self, evse_instance):
        """An already-met soc target must not take the claim, even for a tick.

        Spec: "already met => release immediately, arm is a no-op".  The POST
        still succeeds (201) and the version still bumps so surfaces stay in
        step, but no boost is left active and no priority-200 claim is taken.

        Kept last in the class: it pushes vehicle SoC data into the session-
        scoped EVSE, which would break the 422 precondition above.
        """
        native_url = evse_instance["native_url"]
        config_url = f"{native_url}/config"

        original_src = requests.get(config_url, timeout=REQUEST_TIMEOUT).json().get("vehicle_data_src")
        assert requests.post(
            config_url, json={"vehicle_data_src": VEHICLE_DATA_SRC_HTTP}, timeout=REQUEST_TIMEOUT
        ).status_code == 200
        try:
            assert requests.post(
                f"{native_url}/status", json={"battery_level": 90}, timeout=REQUEST_TIMEOUT
            ).status_code == 200

            deadline = time.time() + 10
            while time.time() < deadline:
                status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()
                if status.get("battery_level") == 90:
                    break
                time.sleep(0.2)
            assert status.get("battery_level") == 90, "vehicle SoC never reached the EVSE"

            before_version = status["boost_version"]

            r = requests.post(
                f"{native_url}/boost", json={"type": "soc", "value": 50}, timeout=REQUEST_TIMEOUT
            )
            assert r.status_code == 201, f"Expected 201, got {r.status_code}: {r.text}"

            assert requests.get(f"{native_url}/boost", timeout=REQUEST_TIMEOUT).json() == {}
            status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()
            assert status["boost"] is False
            assert status["boost_version"] != before_version
            assert not boost_claims(native_url)

            # And nothing is left to cancel.
            assert requests.delete(f"{native_url}/boost", timeout=REQUEST_TIMEOUT).status_code == 404
        finally:
            if original_src is not None and original_src != VEHICLE_DATA_SRC_HTTP:
                requests.post(config_url, json={"vehicle_data_src": original_src}, timeout=REQUEST_TIMEOUT)
