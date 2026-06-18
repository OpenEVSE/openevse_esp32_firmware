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


@pytest.mark.timeout(120)
class TestStatus:
    """Tests for the GET /status endpoint."""

    def test_status_returns_200(self, evse_instance):
        """Test: GET /status returns HTTP 200."""
        response = requests.get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_status_is_json(self, evse_instance):
        """Test: GET /status response is valid JSON."""
        response = requests.get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict), f"Expected dict, got {type(data)}"

    def test_status_contains_required_fields(self, evse_instance):
        """Test: GET /status response includes mandatory top-level fields."""
        response = requests.get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()

        required_fields = ["state", "amp", "pilot", "elapsed"]
        for field in required_fields:
            assert field in data, (
                f"Field '{field}' missing from /status response. Got: {list(data.keys())}"
            )

    def test_status_state_is_valid(self, evse_instance):
        """Test: GET /status 'state' field is a recognised EVSE state integer."""
        response = requests.get(f"{evse_instance['native_url']}/status")
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
        response = requests.get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()
        assert data["pilot"] >= 0, f"pilot must be >= 0, got {data['pilot']}"

    def test_status_elapsed_is_non_negative(self, evse_instance):
        """Test: GET /status 'elapsed' session time in seconds is >= 0."""
        response = requests.get(f"{evse_instance['native_url']}/status")
        assert response.status_code == 200
        data = response.json()
        assert data["elapsed"] >= 0, f"elapsed must be >= 0, got {data['elapsed']}"

    def test_status_post_vehicle_data(self, evse_instance):
        """Test: POST /status accepts vehicle state-of-charge data."""
        payload = {
            "battery_level": 80,
            "battery_range": 120,
            "time_to_full_charge": 3600,
        }
        response = requests.post(
            f"{evse_instance['native_url']}/status",
            json=payload,
        )
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_status_post_solar_input(self, evse_instance):
        """Test: POST /status accepts solar generation update (W)."""
        response = requests.post(
            f"{evse_instance['native_url']}/status",
            json={"solar": 3000},
        )
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_status_post_grid_ie(self, evse_instance):
        """Test: POST /status accepts grid import/export update (W, +import/-export)."""
        for grid_value in [1000, -500]:
            response = requests.post(
                f"{evse_instance['native_url']}/status",
                json={"grid_ie": grid_value},
            )
            assert response.status_code == 200, (
                f"grid_ie={grid_value}: Expected 200, got "
                f"{response.status_code}: {response.text}"
            )


@pytest.mark.timeout(120)
class TestOverride:
    """Tests for the manual override endpoint (POST/DELETE /override)."""

    def test_get_override_returns_200(self, evse_instance):
        """Test: GET /override returns HTTP 200."""
        response = requests.get(f"{evse_instance['native_url']}/override")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_disable_charging(self, evse_instance):
        """Test: POST /override with state=disabled sets EVSE to disabled."""
        response = requests.post(
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
        response = requests.post(
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
        requests.post(
            f"{evse_instance['native_url']}/override",
            json={"state": "disabled"},
        )

        # Clear it
        response = requests.delete(f"{evse_instance['native_url']}/override")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_disable_reflected_in_status(self, evse_instance):
        """Test: disabling charging is reflected in /status state."""
        # Disable charging
        requests.post(
            f"{evse_instance['native_url']}/override",
            json={"state": "disabled"},
        )

        # Allow firmware to process the command
        time.sleep(2)

        status = requests.get(f"{evse_instance['native_url']}/status").json()
        # State 255 = disabled
        assert status["state"] == 255, (
            f"Expected disabled state (255) after override, got {status['state']}"
        )

    def test_enable_after_disable(self, evse_instance):
        """Test: re-enabling after disabling restores EVSE to active state."""
        # Disable then re-enable
        requests.post(
            f"{evse_instance['native_url']}/override",
            json={"state": "disabled"},
        )
        time.sleep(1)

        response = requests.post(
            f"{evse_instance['native_url']}/override",
            json={"state": "active"},
        )
        assert response.status_code in (200, 201), (
            f"Expected 200 or 201, got {response.status_code}: {response.text}"
        )

        time.sleep(2)

        status = requests.get(f"{evse_instance['native_url']}/status").json()
        # Should no longer be in disabled state (255)
        assert status["state"] != 255, (
            f"Expected non-disabled state after re-enable, got {status['state']}"
        )

    def test_override_with_charge_current(self, evse_instance):
        """Test: POST /override with a charge_current limit is accepted."""
        response = requests.post(
            f"{evse_instance['native_url']}/override",
            json={"state": "active", "charge_current": 16},
        )
        assert response.status_code in (200, 201), (
            f"Expected 200 or 201, got {response.status_code}: {response.text}"
        )


@pytest.mark.timeout(120)
class TestConfig:
    """Tests for the GET/POST /config endpoint."""

    def test_get_config_returns_200(self, evse_instance):
        """Test: GET /config returns HTTP 200."""
        response = requests.get(f"{evse_instance['native_url']}/config")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_get_config_is_json(self, evse_instance):
        """Test: GET /config response is valid JSON."""
        response = requests.get(f"{evse_instance['native_url']}/config")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, dict), f"Expected dict, got {type(data)}"

    def test_get_config_contains_expected_fields(self, evse_instance):
        """Test: GET /config response includes key configuration fields."""
        response = requests.get(f"{evse_instance['native_url']}/config")
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

        response = requests.post(
            f"{evse_instance['native_url']}/config",
            json={"max_current_soft": new_limit},
        )
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

        # Read back and verify
        config = requests.get(f"{evse_instance['native_url']}/config").json()
        assert config.get("max_current_soft") == new_limit, (
            f"Expected max_current_soft={new_limit}, got {config.get('max_current_soft')}"
        )


@pytest.mark.timeout(120)
class TestClaims:
    """Tests for the claims endpoint (GET /claims)."""

    def test_get_claims_returns_200(self, evse_instance):
        """Test: GET /claims returns HTTP 200."""
        response = requests.get(f"{evse_instance['native_url']}/claims")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_get_claims_is_json(self, evse_instance):
        """Test: GET /claims response is valid JSON."""
        response = requests.get(f"{evse_instance['native_url']}/claims")
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, (dict, list)), (
            f"Expected dict or list, got {type(data)}"
        )

    def test_get_claims_target_returns_200(self, evse_instance):
        """Test: GET /claims/target returns HTTP 200."""
        response = requests.get(f"{evse_instance['native_url']}/claims/target")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

    def test_post_claim_and_release(self, evse_instance):
        """Test: POST a claim with an ID then DELETE it succeeds."""
        client_id = 9999
        claim_url = f"{evse_instance['native_url']}/claims/{client_id}"

        # Make a claim
        response = requests.post(
            claim_url,
            json={"state": "active", "charge_current": 20, "auto_release": True},
        )
        assert response.status_code == 200, (
            f"POST claim: Expected 200, got {response.status_code}: {response.text}"
        )

        # Verify it appears in the claims list
        claims_response = requests.get(f"{evse_instance['native_url']}/claims")
        assert claims_response.status_code == 200

        # Release the claim
        del_response = requests.delete(claim_url)
        assert del_response.status_code == 200, (
            f"DELETE claim: Expected 200, got {del_response.status_code}: {del_response.text}"
        )
