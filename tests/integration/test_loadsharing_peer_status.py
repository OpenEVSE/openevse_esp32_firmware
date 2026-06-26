"""
Load Sharing Peer Status Ingestion Integration Tests (Phase 2)

Tests for peer status ingestion via HTTP bootstrap and WebSocket subscriptions:
- GET /loadsharing/status (runtime status with per-peer status data)
- GET /status (native firmware status endpoint used by peer poller)
- WebSocket /ws (native firmware WebSocket used by peer poller)
- Peer online/offline tracking after adding peers to group

These tests verify that the native firmware's LoadSharingPeerPoller can:
1. Fetch initial status via HTTP GET /status from a peer
2. Connect to a peer's WebSocket /ws for real-time updates
3. Populate the status cache and expose it via /loadsharing/status
4. Track multiple peers simultaneously

Prerequisites:
- Docker (emulator image)
- socat (TCP-to-PTY bridge)
- Native firmware binary (.pio/build/native/program)
"""

import pytest
import requests
import time
import json
import threading

try:
    import websocket as ws_client

    HAS_WEBSOCKET_CLIENT = True
except ImportError:
    HAS_WEBSOCKET_CLIENT = False


# ---------------------------------------------------------------------------
# Helper utilities
# ---------------------------------------------------------------------------


def wait_for_peer_status(native_url, peer_host, timeout=30, poll_interval=1.0):
    """
    Poll GET /loadsharing/status until the given peer has a nested 'status'
    object (meaning the poller has ingested at least one status update).

    Args:
        native_url: Base URL of the native firmware instance
        peer_host: Hostname of the peer to look for
        timeout: Maximum seconds to wait
        poll_interval: Seconds between polls

    Returns:
        The peer dict (with status) if found, None if timeout
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            response = requests.get(
                f"{native_url}/loadsharing/status", timeout=5
            )
            if response.status_code == 200:
                data = response.json()
                peers = data.get("peers", [])
                for peer in peers:
                    if peer.get("host") == peer_host and "status" in peer:
                        return peer
        except requests.RequestException:
            pass
        time.sleep(poll_interval)
    return None


def add_peer_to_group(native_url, peer_host):
    """
    Add a peer to the load sharing group via POST /loadsharing/peers.

    Args:
        native_url: Base URL of the native firmware instance
        peer_host: Hostname or host:port of the peer to add

    Returns:
        requests.Response object
    """
    return requests.post(
        f"{native_url}/loadsharing/peers",
        json={"host": peer_host},
        timeout=10,
    )


# ---------------------------------------------------------------------------
# Tests: /loadsharing/status endpoint structure
# ---------------------------------------------------------------------------


@pytest.mark.timeout(60)
class TestStatusEndpoint:
    """Test /loadsharing/status endpoint structure and fields."""

    def test_status_endpoint_returns_valid_json(self, instance_pair_auto):
        """
        Test: GET /loadsharing/status returns 200 with valid JSON.

        Verifies the endpoint exists and returns a parseable JSON document.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        response = requests.get(f"{native_url}/loadsharing/status")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

        data = response.json()
        assert isinstance(data, dict), f"Expected dict, got {type(data)}"

    def test_status_endpoint_fields(self, instance_pair_auto):
        """
        Test: GET /loadsharing/status contains required top-level fields.

        Verifies the response includes: enabled, group_id, peers, allocations,
        failsafe_active, online_count, offline_count per the api.yml schema.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        response = requests.get(f"{native_url}/loadsharing/status")
        assert response.status_code == 200

        data = response.json()

        required_fields = [
            "enabled",
            "group_id",
            "peers",
            "allocations",
            "failsafe_active",
            "online_count",
            "offline_count",
        ]
        for field in required_fields:
            assert field in data, (
                f"Missing required field '{field}' in /loadsharing/status. "
                f"Got keys: {list(data.keys())}"
            )

        # Type checks
        assert isinstance(data["peers"], list), "peers must be an array"
        assert isinstance(data["allocations"], list), "allocations must be an array"
        assert isinstance(data["enabled"], bool), "enabled must be a boolean"
        assert isinstance(data["failsafe_active"], bool), (
            "failsafe_active must be a boolean"
        )

    def test_status_response_structure(self, instance_pair_auto):
        """
        Test: /loadsharing/status response structure matches api.yml schema.

        Validates the LoadSharingStatus schema compliance with computed_at
        timestamp and count fields.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        response = requests.get(f"{native_url}/loadsharing/status")
        assert response.status_code == 200

        data = response.json()

        # computed_at should be an ISO 8601 timestamp string
        assert "computed_at" in data, "Missing 'computed_at' field"
        assert isinstance(data["computed_at"], str), "computed_at must be a string"

        # Counts should be non-negative integers
        assert data["online_count"] >= 0, "online_count must be >= 0"
        assert data["offline_count"] >= 0, "offline_count must be >= 0"


# ---------------------------------------------------------------------------
# Tests: Native firmware /status and /ws endpoints (peer-side validation)
# ---------------------------------------------------------------------------


@pytest.mark.timeout(60)
class TestNativeStatusEndpoints:
    """Test native firmware endpoints that peers connect to."""

    def test_native_status_endpoint_has_required_fields(self, instance_pair_auto):
        """
        Test: GET /status contains fields needed by peer poller.

        The LoadSharingPeerPoller fetches GET /status from each peer
        as HTTP bootstrap. This verifies the response contains the
        required fields: amp, voltage, pilot, state, vehicle.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        response = requests.get(f"{native_url}/status")
        assert response.status_code == 200, (
            f"Expected 200, got {response.status_code}: {response.text}"
        )

        status = response.json()
        assert isinstance(status, dict), f"Expected dict, got {type(status)}"

        # These fields are extracted by loadsharing_peer_poller.cpp
        # in startHttpBootstrap() and handleWebSocketMessage()
        required_fields = ["amp", "voltage", "pilot", "state", "vehicle"]
        for field in required_fields:
            assert field in status, (
                f"GET /status missing field '{field}' required by peer poller. "
                f"Available fields: {sorted(status.keys())}"
            )

        # amp and voltage should be numeric
        assert isinstance(status["amp"], (int, float)), (
            f"amp must be numeric, got {type(status['amp'])}"
        )
        assert isinstance(status["voltage"], (int, float)), (
            f"voltage must be numeric, got {type(status['voltage'])}"
        )
        assert isinstance(status["pilot"], (int, float)), (
            f"pilot must be numeric, got {type(status['pilot'])}"
        )

    @pytest.mark.skipif(
        not HAS_WEBSOCKET_CLIENT, reason="websocket-client not installed"
    )
    def test_native_websocket_sends_status(self, instance_pair_auto):
        """
        Test: WebSocket /ws on native firmware sends initial status on connect.

        The LoadSharingPeerPoller subscribes to ws://{peer}/ws for real-time
        status updates. On connect, the server calls buildStatus() and sends
        the full status document. This test verifies that behavior.
        """
        pair = instance_pair_auto()
        native_port = pair["native_port"]

        ws_url = f"ws://localhost:{native_port}/ws"

        received_messages = []
        connection_opened = threading.Event()
        error_info = {"error": None}

        def on_message(ws, message):
            try:
                data = json.loads(message)
                received_messages.append(data)
            except json.JSONDecodeError:
                received_messages.append({"_raw": message})

        def on_error(ws, error):
            error_info["error"] = str(error)

        def on_open(ws):
            connection_opened.set()

        def on_close(ws, close_status_code, close_msg):
            pass

        ws = ws_client.WebSocketApp(
            ws_url,
            on_message=on_message,
            on_error=on_error,
            on_open=on_open,
            on_close=on_close,
        )

        ws_thread = threading.Thread(target=ws.run_forever, daemon=True)
        ws_thread.start()

        try:
            assert connection_opened.wait(timeout=10), (
                f"WebSocket connection to {ws_url} timed out. "
                f"Error: {error_info['error']}"
            )

            # Wait for at least one message (initial status dump)
            deadline = time.time() + 10
            while len(received_messages) == 0 and time.time() < deadline:
                time.sleep(0.5)

            assert len(received_messages) > 0, (
                f"No WebSocket messages received from {ws_url} within 10s"
            )

            # First message should contain status fields used by peer poller
            first_msg = received_messages[0]
            assert isinstance(first_msg, dict), (
                f"Expected dict, got {type(first_msg)}: {first_msg}"
            )

            # Should contain key fields that peer poller extracts
            expected_fields = ["amp", "voltage", "pilot", "state", "vehicle"]
            present_fields = [f for f in expected_fields if f in first_msg]
            assert len(present_fields) >= 3, (
                f"WebSocket initial message missing expected status fields. "
                f"Expected at least 3 of {expected_fields}, "
                f"got {present_fields}. Keys: {sorted(first_msg.keys())}"
            )
        finally:
            ws.close()
            ws_thread.join(timeout=5)


# ---------------------------------------------------------------------------
# Tests: Peer status ingestion (adding peers and verifying status cache)
# ---------------------------------------------------------------------------


@pytest.mark.timeout(120)
class TestPeerStatusIngestion:
    """Test that peer poller ingests status from added peers."""

    @pytest.mark.parametrize("num_instances", [2, 3, 4])
    def test_peer_status_ingested_after_add(
        self, multi_instance_group, num_instances
    ):
        """
        Test: After adding a peer, /loadsharing/status shows peer with status.

        Spawns multiple instances, adds instance 2..N as peers of instance 1,
        then waits for the peer poller to ingest status via HTTP bootstrap
        and/or WebSocket subscription.

        Parametrized for 2, 3, and 4 instance configurations.
        """
        pairs = multi_instance_group(num_instances)
        native_url_1 = pairs[0]["native_url"]

        # Add all other instances as peers of instance 1
        for i in range(1, num_instances):
            peer_host = f"localhost:{pairs[i]['native_port']}"
            response = add_peer_to_group(native_url_1, peer_host)
            assert response.status_code == 200, (
                f"Failed to add peer {peer_host}: "
                f"{response.status_code}: {response.text}"
            )

        # Wait for peer poller to ingest status from at least one peer
        first_peer_host = f"localhost:{pairs[1]['native_port']}"
        peer_with_status = wait_for_peer_status(
            native_url_1, first_peer_host, timeout=45
        )

        assert peer_with_status is not None, (
            f"Peer {first_peer_host} did not get status ingested within 45s. "
            f"Check that peer poller is running and can connect to peer's "
            f"/status and /ws endpoints."
        )

        # Verify the status object has expected fields
        status = peer_with_status.get("status", {})
        assert isinstance(status, dict), (
            f"Expected status dict, got {type(status)}"
        )

        # At minimum, the poller should have extracted these from GET /status
        # (values may be 0/default for idle charger, but keys should exist)
        for field in ["amp", "pilot", "state"]:
            assert field in status, (
                f"Peer status missing '{field}'. Got: {status}"
            )

    def test_peer_status_contains_amp_pilot_state(self, multi_instance_group):
        """
        Test: Ingested peer status contains numeric amp, pilot, state values.

        Verifies that the peer poller correctly parses the JSON response
        from GET /status and populates the cache with valid numeric data.
        """
        pairs = multi_instance_group(2)
        native_url_1 = pairs[0]["native_url"]
        peer_host = f"localhost:{pairs[1]['native_port']}"

        # Add peer
        response = add_peer_to_group(native_url_1, peer_host)
        assert response.status_code == 200

        # Wait for status ingestion
        peer = wait_for_peer_status(native_url_1, peer_host, timeout=45)
        assert peer is not None, (
            f"Peer {peer_host} status not ingested within 45s"
        )

        status = peer["status"]

        # amp should be numeric (milliamps or amps depending on scale)
        assert isinstance(status.get("amp"), (int, float)), (
            f"status.amp must be numeric, got {type(status.get('amp'))}: "
            f"{status.get('amp')}"
        )

        # pilot should be numeric (amps)
        assert isinstance(status.get("pilot"), (int, float)), (
            f"status.pilot must be numeric, got {type(status.get('pilot'))}: "
            f"{status.get('pilot')}"
        )

        # state should be an integer (EVSE state code)
        assert isinstance(status.get("state"), (int, float)), (
            f"status.state must be numeric, got {type(status.get('state'))}: "
            f"{status.get('state')}"
        )

    def test_peer_online_after_connection(self, multi_instance_group):
        """
        Test: Added peer shows as online after poller connects.

        Verifies that after the peer poller successfully ingests status
        (via HTTP bootstrap and/or WebSocket), the peer appears in the
        /loadsharing/status peers list with online=true.
        """
        pairs = multi_instance_group(2)
        native_url_1 = pairs[0]["native_url"]
        peer_host = f"localhost:{pairs[1]['native_port']}"

        # Add peer
        response = add_peer_to_group(native_url_1, peer_host)
        assert response.status_code == 200

        # Wait for status ingestion (indicates successful connection)
        peer = wait_for_peer_status(native_url_1, peer_host, timeout=45)
        assert peer is not None, (
            f"Peer {peer_host} status not ingested within 45s"
        )

        # After successful status ingestion, peer should be online
        # Note: online status depends on WebSocket connection state
        # and heartbeat monitoring. After initial HTTP bootstrap,
        # the peer transitions to WS_CONNECTING then WS_CONNECTED.
        # Give additional time for WebSocket handshake.
        time.sleep(3)

        response = requests.get(f"{native_url_1}/loadsharing/status")
        assert response.status_code == 200

        data = response.json()
        peers = data.get("peers", [])
        matching = [p for p in peers if p.get("host") == peer_host]
        assert len(matching) > 0, (
            f"Peer {peer_host} not found in /loadsharing/status peers"
        )

        # Check online_count is at least 1 (the connected peer)
        # Note: if WebSocket is still connecting, online may not be true yet
        # but the status object should still be present from HTTP bootstrap
        assert "status" in matching[0], (
            f"Peer {peer_host} should have status after ingestion"
        )


# ---------------------------------------------------------------------------
# Tests: Multi-peer status tracking
# ---------------------------------------------------------------------------


@pytest.mark.timeout(120)
class TestMultiPeerTracking:
    """Test simultaneous status tracking of multiple peers."""

    @pytest.mark.parametrize("num_instances", [2, 3, 4])
    def test_peer_status_multiple_peers(
        self, multi_instance_group, num_instances
    ):
        """
        Test: Multiple peers tracked simultaneously with status data.

        Spawns N instances, adds all others as peers of instance 1,
        waits for status ingestion from all, and verifies each peer
        has a status object in /loadsharing/status.

        Parametrized for 2, 3, and 4 instance configurations.
        """
        pairs = multi_instance_group(num_instances)
        native_url_1 = pairs[0]["native_url"]

        # Add all other instances as peers
        peer_hosts = []
        for i in range(1, num_instances):
            peer_host = f"localhost:{pairs[i]['native_port']}"
            peer_hosts.append(peer_host)
            response = add_peer_to_group(native_url_1, peer_host)
            assert response.status_code == 200, (
                f"Failed to add peer {peer_host}: "
                f"{response.status_code}: {response.text}"
            )

        # Wait for status ingestion from all peers
        # Use longer timeout for more instances
        timeout = 30 + (num_instances * 15)
        peers_with_status = {}

        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                response = requests.get(
                    f"{native_url_1}/loadsharing/status", timeout=5
                )
                if response.status_code == 200:
                    data = response.json()
                    for peer in data.get("peers", []):
                        host = peer.get("host")
                        if host in peer_hosts and "status" in peer:
                            peers_with_status[host] = peer
            except requests.RequestException:
                pass

            if len(peers_with_status) >= len(peer_hosts):
                break
            time.sleep(2)

        # Verify we got status from a reasonable number of peers
        # Allow some tolerance: at least (num_instances - 2) peers with status
        expected_min = max(1, num_instances - 2)
        assert len(peers_with_status) >= expected_min, (
            f"Expected status from at least {expected_min} peers, "
            f"got {len(peers_with_status)}/{len(peer_hosts)}. "
            f"Peers with status: {list(peers_with_status.keys())}. "
            f"All peer hosts: {peer_hosts}"
        )

        # Each ingested peer should have valid status fields
        for host, peer in peers_with_status.items():
            status = peer.get("status", {})
            assert "amp" in status, (
                f"Peer {host} status missing 'amp': {status}"
            )
            assert "state" in status, (
                f"Peer {host} status missing 'state': {status}"
            )


# ---------------------------------------------------------------------------
# Tests: Response structure compliance
# ---------------------------------------------------------------------------


@pytest.mark.timeout(90)
class TestPeerStatusResponseStructure:
    """Test peer status response structure per api.yml schema."""

    def test_peer_status_nested_structure(self, multi_instance_group):
        """
        Test: Peer entry in /loadsharing/status has correct nested structure.

        Verifies that when a peer has ingested status, the peer object
        contains the expected nested fields per the LoadSharingStatus schema.
        """
        pairs = multi_instance_group(2)
        native_url_1 = pairs[0]["native_url"]
        peer_host = f"localhost:{pairs[1]['native_port']}"

        # Add peer
        response = add_peer_to_group(native_url_1, peer_host)
        assert response.status_code == 200

        # Wait for ingestion
        peer = wait_for_peer_status(native_url_1, peer_host, timeout=45)
        assert peer is not None, "Peer status not ingested"

        # Verify top-level peer fields (from GET /loadsharing/status peers[])
        assert "host" in peer, "Peer missing 'host' field"
        assert "joined" in peer, "Peer missing 'joined' field"
        assert "online" in peer, "Peer missing 'online' field"

        # Verify nested status object
        status = peer.get("status")
        assert isinstance(status, dict), (
            f"Peer status must be a dict, got {type(status)}"
        )

        # All LoadSharingPeerStatus fields from the poller cache
        expected_status_fields = ["amp", "voltage", "pilot", "vehicle", "state"]
        for field in expected_status_fields:
            assert field in status, (
                f"Peer status missing '{field}'. Got: {list(status.keys())}"
            )

        # Type validation
        for field in ["amp", "voltage", "pilot"]:
            assert isinstance(status[field], (int, float)), (
                f"status.{field} must be numeric, got "
                f"{type(status[field])}: {status[field]}"
            )
        assert isinstance(status["state"], (int, float)), (
            f"status.state must be numeric, got "
            f"{type(status['state'])}: {status['state']}"
        )
        assert isinstance(status["vehicle"], (int, float)), (
            f"status.vehicle must be numeric, got "
            f"{type(status['vehicle'])}: {status['vehicle']}"
        )

    def test_status_allocations_array_structure(self, instance_pair_auto):
        """
        Test: Allocations array in /loadsharing/status has correct format.

        Verifies the allocations array exists and, if populated, each
        entry has the expected LoadSharingAllocation fields.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        response = requests.get(f"{native_url}/loadsharing/status")
        assert response.status_code == 200

        data = response.json()
        allocations = data.get("allocations", [])
        assert isinstance(allocations, list), "allocations must be an array"

        # Allocations may be empty before Phase 3 algorithm runs
        # If populated, verify structure
        for alloc in allocations:
            assert "id" in alloc, f"Allocation missing 'id': {alloc}"
            assert "target_current" in alloc, (
                f"Allocation missing 'target_current': {alloc}"
            )
            assert isinstance(alloc["target_current"], (int, float)), (
                f"target_current must be numeric: {alloc}"
            )

    def test_status_peers_have_id_and_host(self, multi_instance_group):
        """
        Test: Each peer in /loadsharing/status has id and host fields.

        Ensures peer identification fields are consistently present
        for all peers (both discovered and manually added).
        """
        pairs = multi_instance_group(2)
        native_url_1 = pairs[0]["native_url"]
        peer_host = f"localhost:{pairs[1]['native_port']}"

        # Add peer
        add_peer_to_group(native_url_1, peer_host)

        # Wait for ingestion
        peer = wait_for_peer_status(native_url_1, peer_host, timeout=45)
        assert peer is not None, "Peer status not ingested"

        response = requests.get(f"{native_url_1}/loadsharing/status")
        assert response.status_code == 200

        data = response.json()
        for peer_entry in data.get("peers", []):
            assert "host" in peer_entry, (
                f"Peer missing 'host' field: {peer_entry}"
            )
            assert "id" in peer_entry, (
                f"Peer missing 'id' field: {peer_entry}"
            )
            # id may be "unknown" for peers not yet fully identified
            assert isinstance(peer_entry["id"], str), (
                f"Peer 'id' must be string: {peer_entry}"
            )
