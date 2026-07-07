"""
Fix-verification tests for the #940 bench findings.

Each test here asserts the FIXED behavior (they are the flipped
counterparts of the outcome-recording drills in
test_loadsharing_drills.py, which are kept unchanged as the record of
what the branch did before these fixes).
"""

import time

import pytest
import requests


def add_peer(native_url, host):
    return requests.post(
        f"{native_url}/loadsharing/peers",
        json={"host": host, "reciprocal": False},
        timeout=10,
    )


@pytest.mark.timeout(60)
def test_duplicate_peer_add_is_idempotent(instance_pair):
    """Finding 5: adding a peer that is already in the group must be a
    200 no-op, not a 400 — mDNS auto-join followed by a manual add is a
    normal sequence, not an error."""
    pair = instance_pair(port_offset=0)
    native_url = pair["native_url"]

    first = add_peer(native_url, "peer-under-test.local")
    assert first.status_code == 200, first.text

    second = add_peer(native_url, "peer-under-test.local")
    assert second.status_code == 200, (
        f"duplicate add must be idempotent, got {second.status_code}: {second.text}"
    )
    assert "already" in second.json().get("msg", "").lower()

    # Peer list must contain the host exactly once.
    peers = requests.get(f"{native_url}/loadsharing/peers", timeout=5).json()
    matches = [p for p in peers if p.get("host") == "peer-under-test.local"]
    assert len(matches) == 1, f"expected exactly one entry, got: {peers}"


@pytest.mark.timeout(60)
def test_config_rejects_failsafe_exceeding_group_budget(instance_pair):
    """Finding 3 (Drill C flipped): a failsafe current larger than the
    whole group budget means one islanded member alone can exceed the
    group max — the config write must be rejected."""
    pair = instance_pair(port_offset=0)
    native_url = pair["native_url"]

    r = requests.post(f"{native_url}/config", json={
        "loadsharing_enabled": True,
        "loadsharing_role": "member",
        "loadsharing_controller_host": "localhost:59999",
        "loadsharing_group_max_current": 10.0,
        "loadsharing_failsafe_safe_current": 32.0,
    }, timeout=10)
    assert r.status_code == 400, (
        f"expected rejection of failsafe 32A > group_max 10A, got "
        f"{r.status_code}: {r.text}"
    )

    # A sane combination must still be accepted.
    ok = requests.post(f"{native_url}/config", json={
        "loadsharing_group_max_current": 10.0,
        "loadsharing_failsafe_safe_current": 6.0,
    }, timeout=10)
    assert ok.status_code == 200, ok.text

    # Cross-field check must also catch a later failsafe-only update.
    late = requests.post(f"{native_url}/config", json={
        "loadsharing_failsafe_safe_current": 12.0,
    }, timeout=10)
    assert late.status_code == 400, (
        f"failsafe-only update above stored group_max must be rejected, got "
        f"{late.status_code}: {late.text}"
    )
