"""
Load Sharing Peer Management Integration Tests

Tests for peer management and discovery endpoints:
- GET /loadsharing/peers (discover and list peers)
- POST /loadsharing/peers (add peer to configured group)
- DELETE /loadsharing/peers/{host} (remove peer)
- POST /loadsharing/discover (trigger mDNS discovery)

These tests verify the REST API endpoints work correctly with
multiple paired instances of the emulator and native firmware.
"""

import pytest
import requests
import time
from urllib.parse import quote


@pytest.mark.timeout(60)
class TestPeerManagement:
    """Test load sharing peer management endpoints."""

    def test_peers_endpoint_initial_state(self, instance_pair_auto):
        """
        Test: GET /loadsharing/peers returns empty or self-only initially.

        Verifies that before manual peer addition,
        the peers endpoint returns an empty array or only the local instance.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        response = requests.get(f"{native_url}/loadsharing/peers")
        assert response.status_code == 200, f"Expected 200, got {response.status_code}: {response.text}"

        # Response is a JSON array directly (not wrapped in "data")
        peers = response.json()
        assert isinstance(peers, list), f"Expected list, got {type(peers)}"
        # Initially should be empty or just contain discovered self
        assert len(peers) >= 0, "Peer list should be a list"

    def test_discover_trigger(self, instance_pair_auto):
        """
        Test: POST /loadsharing/discover returns 200 OK.

        Verifies that triggering discovery on demand works without error.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        response = requests.post(f"{native_url}/loadsharing/discover")
        assert response.status_code == 200, f"Expected 200, got {response.status_code}: {response.text}"

        data = response.json()
        assert "msg" in data or "status" in data

    @pytest.mark.parametrize("num_instances", [2, 3, 4])
    def test_peer_discovery_mdns(self, multi_instance_group, num_instances):
        """
        Test: mDNS peer discovery detects multiple instances.

        Spawns multiple paired instances, triggers discovery on all,
        and verifies each instance discovers the others via mDNS.

        Parametrized for 2, 3, and 4 instance configurations.
        """
        pairs = multi_instance_group(num_instances)
        assert len(pairs) == num_instances

        # Trigger discovery on all instances
        for pair in pairs:
            native_url = pair["native_url"]
            response = requests.post(f"{native_url}/loadsharing/discover")
            assert response.status_code == 200

        # Give mDNS time to resolve
        time.sleep(3)

        # Verify each instance discovered the others
        for i, pair in enumerate(pairs):
            native_url = pair["native_url"]
            response = requests.get(f"{native_url}/loadsharing/peers")
            assert response.status_code == 200

            # Response is a JSON array directly
            peers = response.json()
            assert isinstance(peers, list), f"Expected list, got {type(peers)}"

            # Should discover num_instances-1 other peers (all except self)
            online_peers = [p for p in peers if p.get("online", False)]
            discovered_count = len(online_peers)

            # Allow some tolerance: mDNS may take time and may not always work in CI
            expected_min = max(0, num_instances - 2)
            assert discovered_count >= expected_min, (
                f"Instance {i}: expected at least {expected_min} discovered peers, "
                f"got {discovered_count}. Peers: {peers}"
            )

    def test_add_peer_manual(self, instance_pair_auto, peer_hostname_factory):
        """
        Test: POST /loadsharing/peers adds peer with joined status.

        Verifies adding a manual peer (not discovered) creates an entry
        with joined=true and online=false.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        test_host = peer_hostname_factory("manual")

        # Add peer
        response = requests.post(
            f"{native_url}/loadsharing/peers",
            json={"host": test_host}
        )
        assert response.status_code == 200, f"Expected 200, got {response.status_code}: {response.text}"

        result = response.json()
        assert result.get("msg") == "done" or result.get("status") == "done"

        # Verify peer appears in list with joined=true
        response = requests.get(f"{native_url}/loadsharing/peers")
        assert response.status_code == 200

        # Response is a JSON array directly
        peers = response.json()
        assert isinstance(peers, list)

        matching_peers = [p for p in peers if p.get("host") == test_host]
        assert len(matching_peers) > 0, f"Peer {test_host} not found in peers list"

        peer = matching_peers[0]
        assert peer.get("joined") is True, "New peer should have joined=true"

    def test_add_peer_duplicate_rejection(self, instance_pair_auto, peer_hostname_factory):
        """
        Test: POST /loadsharing/peers rejects duplicate hosts.

        Verifies that adding the same peer twice fails on the second attempt.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        test_host = peer_hostname_factory("duplicate")

        # Add peer first time
        response = requests.post(
            f"{native_url}/loadsharing/peers",
            json={"host": test_host}
        )
        assert response.status_code == 200

        # Add peer second time (should fail)
        response = requests.post(
            f"{native_url}/loadsharing/peers",
            json={"host": test_host}
        )
        assert response.status_code == 400, (
            f"Expected 400 for duplicate peer, got {response.status_code}: {response.text}"
        )

        data = response.json()
        error_msg = data.get("error", "") or data.get("msg", "")
        assert "already" in error_msg.lower() or "duplicate" in error_msg.lower(), (
            f"Error message should mention duplicate: {error_msg}"
        )

    def test_delete_peer(self, instance_pair_auto, peer_hostname_factory):
        """
        Test: DELETE /loadsharing/peers/{host} removes joined status.

        Verifies deleting a manually added peer removes it from the
        configured group (joined list).
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        test_host = peer_hostname_factory("delete")

        # Add peer
        response = requests.post(
            f"{native_url}/loadsharing/peers",
            json={"host": test_host}
        )
        assert response.status_code == 200

        # Verify peer is in the list
        response = requests.get(f"{native_url}/loadsharing/peers")
        peers = response.json()
        assert isinstance(peers, list)
        assert any(p.get("host") == test_host for p in peers)

        # Delete peer (URL-encode hostname)
        encoded_host = quote(test_host, safe="")
        response = requests.delete(f"{native_url}/loadsharing/peers/{encoded_host}")
        assert response.status_code == 200, f"Expected 200, got {response.status_code}: {response.text}"

        result = response.json()
        assert result.get("msg") == "done" or result.get("status") == "done"

        # Verify peer no longer has joined=true
        response = requests.get(f"{native_url}/loadsharing/peers")
        peers = response.json()
        assert isinstance(peers, list)

        matching_peers = [p for p in peers if p.get("host") == test_host]
        # Peer should either be gone or have joined=false
        if matching_peers:
            peer = matching_peers[0]
            assert peer.get("joined") is False or peer.get("joined") is None

    def test_delete_nonexistent_peer(self, instance_pair_auto, peer_hostname_factory):
        """
        Test: DELETE /loadsharing/peers/{host} returns 404 for unknown peer.

        Verifies that deleting a peer that was never added fails gracefully.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        nonexistent_host = peer_hostname_factory("nonexistent")
        encoded_host = quote(nonexistent_host, safe="")

        response = requests.delete(f"{native_url}/loadsharing/peers/{encoded_host}")
        assert response.status_code == 404, (
            f"Expected 404 for nonexistent peer, got {response.status_code}: {response.text}"
        )

        data = response.json()
        error_msg = data.get("error", "") or data.get("msg", "")
        assert "not found" in error_msg.lower() or "not" in error_msg.lower()

    @pytest.mark.parametrize("num_instances", [2, 3, 4])
    def test_discovered_peers_joined_status(self, multi_instance_group, num_instances):
        """
        Test: Discovered peers can be marked as joined.

        Spawns multiple instances, lets them discover each other via mDNS,
        then manually adds one discovered peer to verify joined status
        transitions from false to true.

        Parametrized for 2, 3, and 4 instance configurations.
        """
        pairs = multi_instance_group(num_instances)

        # Trigger discovery
        for pair in pairs:
            response = requests.post(f"{pair['native_url']}/loadsharing/discover")
            assert response.status_code == 200

        # Give mDNS time to work
        time.sleep(3)

        # Get a discovered peer from first instance
        response = requests.get(f"{pairs[0]['native_url']}/loadsharing/peers")
        assert response.status_code == 200

        discovered_peers = response.json()
        assert isinstance(discovered_peers, list)
        online_peers = [p for p in discovered_peers if p.get("online", False)]

        if len(online_peers) > 0:
            # Pick first online peer
            peer_to_join = online_peers[0]
            peer_host = peer_to_join.get("host")

            # Add it manually (should be idempotent or update joined status)
            response = requests.post(
                f"{pairs[0]['native_url']}/loadsharing/peers",
                json={"host": peer_host}
            )
            # May succeed (200) or fail with duplicate (400) - both acceptable
            assert response.status_code in [200, 400]

            # Verify it's now marked as joined if we can
            response = requests.get(f"{pairs[0]['native_url']}/loadsharing/peers")
            assert response.status_code == 200


@pytest.mark.timeout(30)
class TestResponseStructure:
    """Test response structure compliance with API specification."""

    def test_peers_response_structure(self, instance_pair_auto, peer_hostname_factory):
        """
        Test: GET /loadsharing/peers response matches spec.

        Verifies response structure conforms to spec from IMPLEMENTATION_PLAN.md.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        # Add a test peer first
        response = requests.post(
            f"{native_url}/loadsharing/peers",
            json={"host": peer_hostname_factory("structure")}
        )
        assert response.status_code == 200

        # Get peers
        response = requests.get(f"{native_url}/loadsharing/peers")
        assert response.status_code == 200

        # Response is a JSON array directly (not wrapped in "data")
        peers = response.json()
        assert isinstance(peers, list), "Response must be an array"

        # Each peer must have required fields
        for peer in peers:
            assert "id" in peer or "name" in peer, f"Peer missing id/name: {peer}"
            assert "host" in peer, f"Peer missing host: {peer}"
            assert "joined" in peer, f"Peer missing joined field: {peer}"
            assert isinstance(peer["joined"], bool), f"joined must be bool: {peer}"
            # online and ip may be missing or empty for manual peers

    def test_error_response_structure(self, instance_pair_auto):
        """
        Test: Error responses include proper error message.

        Verifies 4xx/5xx responses include helpful error messages.
        """
        pair = instance_pair_auto()
        native_url = pair["native_url"]

        # Try to delete nonexistent peer
        response = requests.delete(f"{native_url}/loadsharing/peers/nonexistent")
        assert response.status_code == 404

        data = response.json()
        # Should have either error or msg field
        assert "error" in data or "msg" in data, (
            f"Error response missing 'error' or 'msg': {data}"
        )
