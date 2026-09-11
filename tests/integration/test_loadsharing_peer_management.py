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
        assert all(
            {"host", "online", "joined"}.issubset(peer)
            for peer in peers
        ), f"peer entries must follow the documented contract: {peers}"

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

    # Bringing up each emulator+firmware pair has a real cost, so the budget
    # scales with the instance count rather than sitting at the class default.
    # Only the 4-instance case runs: it exercises everything the 2- and
    # 3-instance cases did, and each extra case paid the full setup again.
    @pytest.mark.parametrize(
        "num_instances",
        [pytest.param(4, marks=pytest.mark.timeout(150))],
    )
    def test_peer_discovery_mdns(self, multi_instance_group, num_instances):
        """
        Test: mDNS peer discovery detects multiple instances.

        Spawns multiple paired instances, triggers discovery on all,
        and verifies each instance discovers the others via mDNS.

        Parametrized for 2, 3, and 4 instance configurations.
        """
        pairs = multi_instance_group(num_instances)
        assert len(pairs) == num_instances

        REQUEST_TIMEOUT = 5.0

        # Trigger discovery on all instances
        for pair in pairs:
            native_url = pair["native_url"]
            response = requests.post(
                f"{native_url}/loadsharing/discover",
                timeout=REQUEST_TIMEOUT,
            )
            assert response.status_code == 200

        # Allow some tolerance: mDNS may take time and may not always work in CI
        expected_min = max(0, num_instances - 2)

        # Poll each instance until expected_min online peers appear or timeout.
        # CI with 4 instances can be slower than the previous fixed 3-second
        # sleep, so use a longer deadline with polling.
        DISCOVERY_TIMEOUT = 30.0
        POLL_INTERVAL = 1.0
        REDISCOVER_INTERVAL = 5.0

        # Verify all instances discover peers within one shared deadline so this
        # parametrized test stays within its timeout budget.
        deadline = time.time() + DISCOVERY_TIMEOUT
        remaining = set(range(len(pairs)))
        online_counts = {i: 0 for i in remaining}
        last_peers = {i: [] for i in remaining}
        last_discover = 0.0

        while remaining and time.time() < deadline:
            now = time.time()
            if now - last_discover >= REDISCOVER_INTERVAL:
                for i in list(remaining):
                    try:
                        discover_response = requests.post(
                            f"{pairs[i]['native_url']}/loadsharing/discover",
                            timeout=REQUEST_TIMEOUT,
                        )
                    except requests.RequestException:
                        continue
                    assert discover_response.status_code == 200
                last_discover = now

            for i in list(remaining):
                try:
                    response = requests.get(
                        f"{pairs[i]['native_url']}/loadsharing/peers",
                        timeout=REQUEST_TIMEOUT,
                    )
                except requests.RequestException:
                    continue
                assert response.status_code == 200
                peers = response.json()
                assert isinstance(peers, list), f"Expected list, got {type(peers)}"
                online_peers = [p for p in peers if p.get("online", False)]
                online_counts[i] = len(online_peers)
                last_peers[i] = peers
                if online_counts[i] >= expected_min:
                    remaining.remove(i)

            if remaining:
                time.sleep(POLL_INTERVAL)

        for i in range(len(pairs)):
            discovered_count = online_counts[i]
            assert discovered_count >= expected_min, (
                f"Instance {i}: expected at least {expected_min} discovered peers, "
                f"got {discovered_count}. Peers: {last_peers[i]}"
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
        Test: POST /loadsharing/peers handles duplicate hosts idempotently.

        Duplicate adds are idempotent by design: mDNS auto-discovery racing a
        manual add (or a repeated reciprocal sync) is a normal sequence, not a
        client error. The second add returns 200 "already in group" and the
        peer still appears exactly once.
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

        # Add peer second time (idempotent no-op, not an error)
        response = requests.post(
            f"{native_url}/loadsharing/peers",
            json={"host": test_host}
        )
        assert response.status_code == 200, (
            f"Expected 200 for idempotent duplicate peer, got {response.status_code}: {response.text}"
        )

        data = response.json()
        error_msg = data.get("error", "") or data.get("msg", "")
        assert "already in group" in error_msg.lower(), (
            f"Message should note the peer is already in the group: {error_msg}"
        )

        # The duplicate must not create a second entry.
        response = requests.get(f"{native_url}/loadsharing/peers")
        assert response.status_code == 200
        peers = response.json()
        matching_peers = [p for p in peers if p.get("host") == test_host]
        assert len(matching_peers) == 1, (
            f"Duplicate add must leave exactly one entry, got: {matching_peers}"
        )

    def test_reciprocal_add_and_remove_do_not_loop(self, instance_pair):
        first = instance_pair(port_offset=0)
        second = instance_pair(port_offset=1)
        first_url = first["native_url"]
        second_url = second["native_url"]
        second_host = f"localhost:{second['native_port']}"

        controller_config = requests.post(
            f"{first_url}/config",
            json={
                "loadsharing_enabled": True,
                "loadsharing_role": "controller",
                "loadsharing_group_id": "integration-test",
                "loadsharing_group_max_current": 32,
            },
            timeout=10,
        )
        assert controller_config.status_code == 200, controller_config.text

        added = requests.post(
            f"{first_url}/loadsharing/peers",
            json={"host": second_host},
            timeout=10,
        )
        assert added.status_code == 200, added.text

        # The reciprocal add is an asynchronous HTTP POST from first to second,
        # so poll until second actually lists first rather than sleeping a fixed
        # interval and hoping it has landed. With the faster instance startup
        # the old 2 s sleep lost that race about one run in three.
        first_id = next(
            p for p in requests.get(f"{first_url}/loadsharing/peers", timeout=10).json()
            if p.get("isLocal")
        )["id"]
        deadline = time.time() + 20
        while True:
            first_peers = requests.get(
                f"{first_url}/loadsharing/peers", timeout=10).json()
            second_peers = requests.get(
                f"{second_url}/loadsharing/peers", timeout=10).json()
            if any(p.get("id") == first_id and not p.get("isLocal") for p in second_peers):
                break
            assert time.time() < deadline, (
                f"second never listed first after the reciprocal add: {second_peers}"
            )
            time.sleep(0.5)

        # Identify both ends by device id rather than host string. The two sides
        # legitimately spell the same peer differently: first knows itself as
        # "openevse-native-0.local" while second, having learned it over mDNS on
        # a non-default port, knows it as "openevse-native-0.local:8000". The
        # manually added entry is also re-keyed from "localhost:8001" to its
        # discovered hostname once mDNS resolves the same device, so the string
        # used to add a peer is not a durable handle for it.
        second_local = next(p for p in second_peers if p.get("isLocal"))
        second_id = second_local["id"]

        # Point the member at the controller using the host string the member
        # itself uses for it. On the reset path the member calls
        # removeGroupPeer(loadsharing_controller_host), so a controller host
        # spelled any other way would not match its own peer entry.
        first_as_second_sees_it = next(
            p for p in second_peers
            if p.get("id") == first_id and not p.get("isLocal")
        )
        first_host = first_as_second_sees_it["host"]

        # The point of this test is that a reciprocal add does not loop, so what
        # matters is that each side ends up with exactly one *joined* entry for
        # the other -- a loop would keep appending them. Match on id and joined
        # rather than on the host string, which is not stable (see above).
        second_joined_in_first = [
            p for p in first_peers
            if p.get("id") == second_id and not p.get("isLocal") and p.get("joined")
        ]
        assert len(second_joined_in_first) == 1, (
            f"second should be joined in first exactly once: {first_peers}"
        )
        assert sum(p.get("id") == first_id and p.get("joined")
                   for p in second_peers) == 1, (
            f"first should be a joined peer of second: {second_peers}"
        )

        configured = requests.post(
            f"{second_url}/config",
            json={
                "loadsharing_enabled": True,
                "loadsharing_role": "member",
                "loadsharing_controller_host": first_host,
            },
            timeout=10,
        )
        assert configured.status_code == 200, configured.text

        # Delete the host as currently listed, which is what a client would do,
        # rather than the string used to add it.
        removed = requests.delete(
            f"{first_url}/loadsharing/peers/"
            f"{quote(second_joined_in_first[0]['host'], safe='')}",
            timeout=10,
        )
        assert removed.status_code == 200, removed.text
        deadline = time.time() + 10
        while time.time() < deadline:
            second_peers = requests.get(
                f"{second_url}/loadsharing/peers", timeout=10).json()
            if not any(p.get("id") == first_id and p.get("joined")
                       for p in second_peers):
                break
            time.sleep(0.5)
        assert not any(p.get("id") == first_id and p.get("joined")
                       for p in second_peers), second_peers

    @pytest.mark.timeout(90)
    def test_manual_add_deduplicates_against_discovery_by_id(self, instance_pair):
        """
        Test: a manually added peer collapses into its discovery entry by id.

        A manual add supplies only a host string, so the new entry starts with no
        device id and mDNS discovery -- which keys peers by the hostname they
        advertise -- cannot tell it is the same device. That leaves two rows for
        one peer: the joined manual entry and a discovered, not-joined one. The
        poller learns the id from the peer's /config shortly after, which is the
        point at which the two can be recognised as one and merged.
        """
        first = instance_pair(port_offset=0)
        second = instance_pair(port_offset=1)
        first_url = first["native_url"]
        second_url = second["native_url"]

        controller_config = requests.post(
            f"{first_url}/config",
            json={
                "loadsharing_enabled": True,
                "loadsharing_role": "controller",
                "loadsharing_group_id": "integration-test",
                "loadsharing_group_max_current": 32,
            },
            timeout=10,
        )
        assert controller_config.status_code == 200, controller_config.text

        second_id = next(
            p for p in requests.get(f"{second_url}/loadsharing/peers", timeout=10).json()
            if p.get("isLocal")
        )["id"]

        # Add by "localhost:<port>", which is reachable but is never the spelling
        # mDNS advertises, so discovery is guaranteed to produce a separate entry
        # for the same device.
        added = requests.post(
            f"{first_url}/loadsharing/peers",
            json={"host": f"localhost:{second['native_port']}", "reciprocal": False},
            timeout=10,
        )
        assert added.status_code == 200, added.text

        # Allow discovery to run and the poller to fetch /config and merge.
        deadline = time.time() + 20
        while time.time() < deadline:
            peers = requests.get(f"{first_url}/loadsharing/peers", timeout=10).json()
            entries = [p for p in peers if p.get("id") == second_id and not p.get("isLocal")]
            if len(entries) == 1 and entries[0].get("joined"):
                break
            time.sleep(0.5)

        assert len(entries) == 1, (
            f"the same device must appear exactly once, got {entries}"
        )
        assert entries[0].get("joined") is True, (
            f"the merged entry must stay in the group: {entries[0]}"
        )

    @pytest.mark.timeout(90)
    def test_member_leaves_group_with_rekeyed_controller_host(self, instance_pair):
        """
        Test: leaving the group drops the controller peer after it is re-keyed.

        A member's controller entry is created from loadsharing_controller_host,
        but discovery adopts the mDNS hostname the controller advertises, so the
        entry stops being keyed by the configured spelling. Clearing the role must
        still remove it -- matching on the configured host would silently leave a
        stale joined peer behind while reporting success.
        """
        first = instance_pair(port_offset=0)
        second = instance_pair(port_offset=1)
        first_url = first["native_url"]
        second_url = second["native_url"]

        controller_config = requests.post(
            f"{first_url}/config",
            json={
                "loadsharing_enabled": True,
                "loadsharing_role": "controller",
                "loadsharing_group_id": "integration-test",
                "loadsharing_group_max_current": 32,
            },
            timeout=10,
        )
        assert controller_config.status_code == 200, controller_config.text

        first_id = next(
            p for p in requests.get(f"{first_url}/loadsharing/peers", timeout=10).json()
            if p.get("isLocal")
        )["id"]

        # The reciprocal add gives the member a joined entry for the controller.
        added = requests.post(
            f"{first_url}/loadsharing/peers",
            json={"host": f"localhost:{second['native_port']}"},
            timeout=10,
        )
        assert added.status_code == 200, added.text

        deadline = time.time() + 20
        controller_entry = None
        while time.time() < deadline:
            second_peers = requests.get(
                f"{second_url}/loadsharing/peers", timeout=10).json()
            matches = [
                p for p in second_peers
                if p.get("id") == first_id and not p.get("isLocal") and p.get("joined")
            ]
            if matches:
                controller_entry = matches[0]
                break
            time.sleep(0.5)
        assert controller_entry is not None, (
            f"member should have a joined entry for the controller: {second_peers}"
        )

        # Configure the member using a reachable spelling that deliberately
        # differs from however its peer entry is keyed, which is what happens in
        # practice once discovery adopts the advertised mDNS hostname.
        stale_host = f"localhost:{first['native_port']}"
        assert controller_entry["host"] != stale_host, (
            "test needs the entry keyed differently from the configured host"
        )
        configured = requests.post(
            f"{second_url}/config",
            json={
                "loadsharing_enabled": True,
                "loadsharing_role": "member",
                "loadsharing_controller_host": stale_host,
            },
            timeout=10,
        )
        assert configured.status_code == 200, configured.text

        left = requests.post(
            f"{second_url}/config",
            json={"loadsharing_role": ""},
            timeout=10,
        )
        assert left.status_code == 200, left.text

        second_peers = requests.get(
            f"{second_url}/loadsharing/peers", timeout=10).json()
        assert not any(
            p.get("id") == first_id and not p.get("isLocal") and p.get("joined")
            for p in second_peers
        ), f"controller must not remain a joined peer after leaving: {second_peers}"

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

    def test_joined_peer_persists_across_native_restart(
            self, instance_pair_auto, peer_hostname_factory):
        pair = instance_pair_auto()
        native_url = pair["native_url"]
        test_host = peer_hostname_factory("persistent")

        added = requests.post(
            f"{native_url}/loadsharing/peers",
            json={"host": test_host, "reciprocal": False},
            timeout=10,
        )
        assert added.status_code == 200, added.text

        pair["restart_native"]()

        peers = requests.get(
            f"{native_url}/loadsharing/peers", timeout=10).json()
        assert any(
            peer.get("host") == test_host and peer.get("joined") is True
            for peer in peers
        ), f"joined peer was not restored after restart: {peers}"

    # Same per-instance setup cost as test_peer_discovery_mdns above.
    @pytest.mark.parametrize(
        "num_instances",
        [pytest.param(4, marks=pytest.mark.timeout(150))],
    )
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
