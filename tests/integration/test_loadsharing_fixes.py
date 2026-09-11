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


SHAPER_CLIENT = 65548  # EvseClient_OpenEVSE_Shaper = EVC(vendor 1, 0x000C) = (1 << 16) | 12; Manual is 65537 in Drill B's output, confirming the encoding
HEARTBEAT_TIMEOUT_S = 10
MARGIN_S = 15


def add_peer(native_url, host):
    return requests.post(
        f"{native_url}/loadsharing/peers",
        json={"host": host, "reciprocal": False},
        timeout=10,
    )


def wait_for_failsafe(native_url, timeout_s):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        r = requests.get(f"{native_url}/loadsharing/status", timeout=5)
        if r.status_code == 200 and r.json().get("failsafe_active"):
            return True
        time.sleep(1.0)
    return False


def find_shaper_claim(native_url):
    claims = requests.get(f"{native_url}/claims", timeout=5).json()
    for c in claims:
        if c.get("client") == SHAPER_CLIENT:
            return c
    return None


def wait_for_shaper_claim(native_url, timeout_s=10):
    """The shaper applies the load sharing limit from its own task loop
    (2 s cadence), so the claim lands asynchronously after failsafe_active
    flips — poll for it instead of asserting immediately."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        claim = find_shaper_claim(native_url)
        if claim is not None:
            return claim
        time.sleep(0.5)
    return None


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


@pytest.mark.timeout(120)
def test_member_failsafe_places_claim_and_caps_override(instance_pair):
    """Finding 1 (Drills A+B flipped): an islanded member must actually
    enforce its failsafe current — via the shaper's load sharing limit,
    which places a Safety-priority Shaper claim — so a manual override
    cannot exceed it."""
    pair = instance_pair(port_offset=0)
    native_url = pair["native_url"]

    r = requests.post(f"{native_url}/config", json={
        "loadsharing_enabled": True,
        "loadsharing_role": "member",
        "loadsharing_controller_host": "localhost:59999",
        "loadsharing_heartbeat_timeout": HEARTBEAT_TIMEOUT_S,
        "loadsharing_failsafe_mode": "safe_current",
        "loadsharing_failsafe_safe_current": 6.0,
        "loadsharing_group_max_current": 32.0,
    }, timeout=10)
    assert r.status_code == 200, r.text

    assert wait_for_failsafe(native_url, HEARTBEAT_TIMEOUT_S + MARGIN_S)

    claim = wait_for_shaper_claim(native_url)
    assert claim is not None, "failsafe engaged but no Shaper claim placed"
    assert claim.get("max_current") == 6, f"claim should cap at 6 A: {claim}"

    # Manual override must not exceed the failsafe cap.
    r = requests.post(f"{native_url}/override",
                      json={"state": "active", "charge_current": 32},
                      timeout=10)
    assert r.status_code in (200, 201), r.text
    time.sleep(3)
    claim = find_shaper_claim(native_url)
    assert claim is not None, "override displaced the failsafe claim"

    # Effective limit: the Safety-priority (5000) Shaper claim outranks the
    # Manual (1000) override, so the resolved target max_current stays 6 A even
    # though the override asked for 32 A. Verify against the effective target
    # (/claims/target), whose shape was recorded in Drill B: it reports the
    # winning EvseProperties in `properties` and the owning client per field in
    # `claims`.
    target = requests.get(f"{native_url}/claims/target", timeout=5).json()
    props = target.get("properties", {})
    owners = target.get("claims", {})
    assert props.get("max_current") == 6, (
        f"override must not raise the effective cap above 6 A: {target}"
    )
    assert owners.get("max_current") == SHAPER_CLIENT, (
        f"the failsafe (Shaper) claim must own the effective max_current: {target}"
    )


@pytest.mark.timeout(60)
def test_member_rejects_local_loadsharing_writes(instance_pair):
    pair = instance_pair(port_offset=0)
    native_url = pair["native_url"]
    configured = requests.post(f"{native_url}/config", json={
        "loadsharing_enabled": True,
        "loadsharing_role": "member",
        "loadsharing_controller_host": "localhost:59999",
        "loadsharing_group_max_current": 32.0,
    }, timeout=10)
    assert configured.status_code == 200, configured.text

    config_write = requests.post(f"{native_url}/config", json={
        "loadsharing_group_max_current": 40.0,
    }, timeout=10)
    assert config_write.status_code == 403, config_write.text

    add = requests.post(f"{native_url}/loadsharing/peers", json={
        "host": "peer.local",
    }, timeout=10)
    assert add.status_code == 403, add.text

    delete = requests.delete(
        f"{native_url}/loadsharing/peers/peer.local", timeout=10)
    assert delete.status_code == 403, delete.text
