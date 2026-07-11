"""
Basic Charging Integration Tests

Tests for the core EVSE charging features via HTTP API:
- GET /status  – EVSE status reporting
- GET /config  – configuration endpoint
- POST/DELETE /override – manual enable/disable of charging
- GET /claims  – active charge-manager claims
"""

import pytest
import requests
import time

REQUEST_TIMEOUT = 5
# Matches app_config.h enum vehicle_data_src: VEHICLE_DATA_SRC_HTTP = 3.
VEHICLE_DATA_SRC_HTTP = 3


def api_get(url):
    return requests.get(url, timeout=REQUEST_TIMEOUT, allow_redirects=False)


def api_post(url, json):
    return requests.post(url, json=json, timeout=REQUEST_TIMEOUT, allow_redirects=False)


def api_delete(url):
    return requests.delete(url, timeout=REQUEST_TIMEOUT, allow_redirects=False)


def wait_for_state(native_url, predicate, timeout=10, poll_interval=0.2):
    """Poll GET /status until ``predicate(state)`` is True or timeout elapses.

    Returns the last observed state (or None if /status never responded).
    """
    last_state = None
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            data = requests.get(
                f"{native_url}/status", timeout=2, allow_redirects=False
            ).json()
            last_state = data.get("state")
            if predicate(last_state):
                return last_state
        except (requests.RequestException, ValueError):
            pass
        time.sleep(poll_interval)
    return last_state


def wait_for_status_field(native_url, field, predicate, timeout=10, poll_interval=0.2):
    """Poll GET /status until ``predicate(status[field])`` is True."""
    last_value = None
    last_error = None
    start = time.monotonic()
    while time.monotonic() - start < timeout:
        try:
            data = api_get(f"{native_url}/status").json()
            last_value = data.get(field)
            if predicate(last_value):
                return last_value
        except (requests.RequestException, ValueError) as exc:
            last_error = exc
        time.sleep(poll_interval)
    if last_error is not None:
        print(f"wait_for_status_field({field}) last polling error: {last_error}")
    return last_value


@pytest.mark.timeout(120)
class TestMongooseRouting:
    """Coverage for HTTP route binding through ArduinoMongoose."""

    def test_core_api_routes_bind_without_legacy_dollar_suffix(self, evse_instance):
        routes = [
            "/status",
            "/config",
            "/override",
            "/claims",
            "/claims/target",
        ]
        for route in routes:
            response = api_get(f"{evse_instance['native_url']}{route}")
            assert response.status_code == 200, (
                f"{route}: Expected 200, got {response.status_code}; "
                f"Location={response.headers.get('Location')}, body={response.text[:200]}"
            )


@pytest.mark.timeout(120)
class TestStatus:
    """Tests for the GET /status endpoint."""

    def test_status_returns_200(self, evse_instance):
        """Test: GET /status returns HTTP 200."""
        response = api_get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_status_is_json(self, evse_instance):
        """Test: GET /status response is valid JSON."""
        response = api_get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict), f"Expected dict, got {type(data)}"

    def test_status_contains_required_fields(self, evse_instance):
        """Test: GET /status response includes mandatory top-level fields."""
        response = api_get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()

        required_fields = ["state", "amp", "pilot", "elapsed"]
        for field in required_fields:
            assert field in data, (
                f"Field '{field}' missing from /status response. Got: {list(data.keys())}"
            )

    def test_status_state_is_valid(self, evse_instance):
        """Test: GET /status 'state' field is a recognised EVSE state integer."""
        response = api_get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()

        # EVSE states: 1=Not Connected, 2=Connected/Vehicle Detected,
        # 3=Charging, 4=Vent Required, 5=Diode Check Failed, 6=GFCI Fault,
        # 7=No Earth, 8=Stuck Relay, 9=GFCI Self Test Failure, 10=Over Temp,
        # 254=Sleeping, 255=Disabled
        assert "state" in data
        state = data["state"]
        assert isinstance(state, int), f"state must be int, got {type(state)}"
        assert state >= 1, f"state must be >= 1, got {state}"

    def test_status_pilot_is_non_negative(self, evse_instance):
        """Test: GET /status 'pilot' (configured current limit) is >= 0."""
        response = api_get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()
        assert data["pilot"] >= 0, f"pilot must be >= 0, got {data['pilot']}"

    def test_status_elapsed_is_non_negative(self, evse_instance):
        """Test: GET /status 'elapsed' session time in seconds is >= 0."""
        response = api_get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()
        assert data["elapsed"] >= 0, f"elapsed must be >= 0, got {data['elapsed']}"

    def test_status_post_vehicle_data(self, evse_instance):
        """Test: POST /status accepts vehicle state-of-charge data."""
        config_url = f"{evse_instance['native_url']}/config"
        original_config = api_get(config_url).json()
        original_src = original_config.get("vehicle_data_src")

        set_src_response = api_post(config_url, json={"vehicle_data_src": VEHICLE_DATA_SRC_HTTP})
        assert set_src_response.status_code == 200, (
            f"Expected 200, got {set_src_response.status_code}: {set_src_response.text}"
        )

        payload = {
            "battery_level": 80,
            "battery_range": 120,
            "time_to_full_charge": 3600,
        }
        try:
            response = api_post(
                f"{evse_instance['native_url']}/status",
                json=payload,
            )
            assert response.status_code == 200, (
                f"Expected 200, got {response.status_code}: {response.text}"
            )
            for field, expected in payload.items():
                value = wait_for_status_field(
                    evse_instance["native_url"], field, lambda v: v == expected
                )
                assert value == expected, (
                    f"Expected /status {field}={expected}, got {value}"
                )
        finally:
            if original_src is not None and original_src != VEHICLE_DATA_SRC_HTTP:
                restore_src_response = api_post(config_url, json={"vehicle_data_src": original_src})
                assert restore_src_response.status_code == 200, (
                    f"Expected 200 while restoring vehicle_data_src, got "
                    f"{restore_src_response.status_code}: {restore_src_response.text}"
                )

    def test_status_post_solar_input(self, evse_instance):
        """Test: POST /status accepts solar generation update (W)."""
        response = api_post(
            f"{evse_instance['native_url']}/status",
            json={"solar": 3000},
        )
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )
        solar = wait_for_status_field(
            evse_instance["native_url"], "solar", lambda v: v == 3000
        )
        assert solar == 3000, f"Expected /status solar=3000, got {solar}"

    def test_status_post_grid_ie(self, evse_instance):
        """Test: POST /status accepts grid import/export update (W, +import/-export)."""
        for grid_value in [1000, -500]:
            response = api_post(
                f"{evse_instance['native_url']}/status",
                json={"grid_ie": grid_value},
            )
            assert response.status_code == 200, (
                f"grid_ie={grid_value}: Expected 200, got "
                f"{response.status_code}: {response.text}"
            )
            observed = wait_for_status_field(
                evse_instance["native_url"], "grid_ie", lambda v: v == grid_value
            )
            assert observed == grid_value, (
                f"Expected /status grid_ie={grid_value}, got {observed}"
            )


@pytest.mark.timeout(120)
class TestOverride:
    """Tests for the manual override endpoint (POST/DELETE /override)."""

    def test_get_override_returns_200(self, evse_instance):
        """Test: GET /override returns HTTP 200."""
        response = api_get(f"{evse_instance['native_url']}/override")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_disable_charging(self, evse_instance):
        """Test: POST /override with state=disabled sets EVSE to disabled."""
        response = api_post(
            f"{evse_instance['native_url']}/override",
            json={"state": "disabled"},
        )
        assert response.status_code in (200, 201), (
            f"Expected 200 or 201, got {response.status_code}: {response.text}"
        )
        data = response.json()
        assert "msg" in data or "state" in data

    def test_enable_charging(self, evse_instance):
        """Test: POST /override with state=active sets EVSE to active."""
        response = api_post(
            f"{evse_instance['native_url']}/override",
            json={"state": "active"},
        )
        assert response.status_code in (200, 201), (
            f"Expected 200 or 201, got {response.status_code}: {response.text}"
        )
        data = response.json()
        assert "msg" in data or "state" in data

    def test_clear_override(self, evse_instance):
        """Test: DELETE /override clears the manual override."""
        # Set an override first
        set_resp = api_post(
            f"{evse_instance['native_url']}/override",
            json={"state": "disabled"},
        )
        assert set_resp.status_code in (200, 201), (
            f"Expected 200 or 201, got {set_resp.status_code}: {set_resp.text}"
        )

        # Clear it
        response = api_delete(f"{evse_instance['native_url']}/override")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_disable_reflected_in_status(self, evse_instance):
        """Test: disabling charging is reflected in /status state."""
        # Disable charging
        resp = api_post(
            f"{evse_instance['native_url']}/override",
            json={"state": "disabled"},
        )
        assert resp.status_code in (200, 201), (
            f"Expected 200 or 201, got {resp.status_code}: {resp.text}"
        )

        # The firmware pauses by sleeping the EVSE ($FS), so the reported
        # state is 254 (sleeping).
        state = wait_for_state(
            evse_instance["native_url"], lambda s: s == 254
        )
        assert state == 254, (
            f"Expected sleeping state (254) after override, got {state}"
        )

    def test_enable_after_disable(self, evse_instance):
        """Test: re-enabling after disabling restores EVSE to active state."""
        # Disable then re-enable
        disable_resp = api_post(
            f"{evse_instance['native_url']}/override",
            json={"state": "disabled"},
        )
        assert disable_resp.status_code in (200, 201), (
            f"Expected 200 or 201, got {disable_resp.status_code}: {disable_resp.text}"
        )
        state = wait_for_state(evse_instance["native_url"], lambda s: s == 254)
        assert state == 254, f"Expected sleeping state (254) after disable, got {state}"

        response = api_post(
            f"{evse_instance['native_url']}/override",
            json={"state": "active"},
        )
        assert response.status_code in (200, 201), (
            f"Expected 200 or 201, got {response.status_code}: {response.text}"
        )

        # Should no longer be in sleeping state (254)
        state = wait_for_state(
            evse_instance["native_url"], lambda s: s != 254
        )
        assert state != 254, (
            f"Expected non-sleeping state after re-enable, got {state}"
        )

    def test_override_with_charge_current(self, evse_instance):
        """Test: POST /override with a charge_current limit is accepted."""
        response = api_post(
            f"{evse_instance['native_url']}/override",
            json={"state": "active", "charge_current": 16},
        )
        assert response.status_code in (200, 201), (
            f"Expected 200 or 201, got {response.status_code}: {response.text}"
        )
        claims_target_response = api_get(f"{evse_instance['native_url']}/claims/target")
        assert claims_target_response.status_code == 200, (
            f"Expected 200, got {claims_target_response.status_code}: {claims_target_response.text}"
        )
        target = claims_target_response.json()
        charge_current = target.get("properties", {}).get("charge_current")
        assert charge_current == 16, (
            f"Expected /claims/target properties.charge_current=16, got {charge_current}"
        )


@pytest.mark.timeout(120)
class TestConfig:
    """Tests for the GET/POST /config endpoint."""

    def test_get_config_returns_200(self, evse_instance):
        """Test: GET /config returns HTTP 200."""
        response = api_get(f"{evse_instance['native_url']}/config")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_get_config_is_json(self, evse_instance):
        """Test: GET /config response is valid JSON."""
        response = api_get(f"{evse_instance['native_url']}/config")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict), f"Expected dict, got {type(data)}"

    def test_get_config_contains_expected_fields(self, evse_instance):
        """Test: GET /config response includes key configuration fields."""
        response = api_get(f"{evse_instance['native_url']}/config")
        assert response.status_code == 200
        data = response.json()

        expected_fields = ["max_current_soft", "divert_type"]
        for field in expected_fields:
            assert field in data, (
                f"Field '{field}' missing from /config. Got: {list(data.keys())}"
            )

    def test_post_config_updates_max_current(self, evse_instance):
        """Test: POST /config with max_current_soft persists the new value."""
        new_limit = 24

        response = api_post(
            f"{evse_instance['native_url']}/config",
            json={"max_current_soft": new_limit},
        )
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

        # Read back and verify
        config = api_get(f"{evse_instance['native_url']}/config").json()
        assert config.get("max_current_soft") == new_limit, (
            f"Expected max_current_soft={new_limit}, got {config.get('max_current_soft')}"
        )


@pytest.mark.timeout(120)
class TestClaims:
    """Tests for the claims endpoint (GET /claims)."""

    def test_get_claims_returns_200(self, evse_instance):
        """Test: GET /claims returns HTTP 200."""
        response = api_get(f"{evse_instance['native_url']}/claims")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_get_claims_is_json(self, evse_instance):
        """Test: GET /claims response is valid JSON."""
        response = api_get(f"{evse_instance['native_url']}/claims")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, (dict, list)), (
            f"Expected dict or list, got {type(data)}"
        )

    def test_get_claims_target_returns_200(self, evse_instance):
        """Test: GET /claims/target returns HTTP 200."""
        response = api_get(f"{evse_instance['native_url']}/claims/target")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_post_claim_and_release(self, evse_instance):
        """Test: POST a claim with an ID then DELETE it succeeds."""
        client_id = 9999
        claim_url = f"{evse_instance['native_url']}/claims/{client_id}"

        # Make a claim
        response = api_post(
            claim_url,
            json={"state": "active", "charge_current": 20, "auto_release": True},
        )
        assert response.status_code == 200, (
            f"POST claim: Expected 200, got {response.status_code}: {response.text}"
        )

        # Verify it appears in the claims list
        claims_response = api_get(f"{evse_instance['native_url']}/claims")
        assert claims_response.status_code == 200

        # Release the claim
        del_response = api_delete(claim_url)
        assert del_response.status_code == 200, (
            f"DELETE claim: Expected 200, got {del_response.status_code}: {del_response.text}"
        )
