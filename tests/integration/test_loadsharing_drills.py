"""
Load-sharing failure-mode drills (bench for OpenEVSE issue #940).

Drill A: a member whose controller is unreachable must report failsafe
         within loadsharing_heartbeat_timeout (+ margin). Measures whether
         and when the member-side failsafe engages, and whether that
         failsafe is actually *enforced* on the local EVSE (via a
         LoadSharing claim) or is merely a status flag.

Drill B: a manual override on a member is compared against its
         load-sharing allocation/claim -- documents the claim-priority
         layering. Upstream places the LoadSharing claim at
         EvseManager_Priority_Limit (1100), so a user override could be
         outbid; the drill RECORDS which one actually wins at the claim
         layer (it does not assert a winner).

Drill C: a config whose per-member failsafe_safe_current (32 A) exceeds
         the whole group_max_current (10 A) -- records whether the
         firmware validates Sigma(failsafe) <= group budget at config time.

Endpoint/key shapes verified against this branch's firmware:
  POST /config          flat JSON of loadsharing_* keys (web_server_config.cpp);
                        role=="member" + loadsharing_controller_host triggers
                        becomeMember(); returns 200 {config_version,msg}.
  GET  /config          flat JSON of all config keys.
  GET  /loadsharing/status  has boolean failsafe_active (web_server_loadsharing.cpp).
  POST /override        EvseProperties body: {"state":"active|disabled",
                        "charge_current":<A>} -> 201 on claim (web_server.cpp).
  GET  /claims          all client claims (web_server_claims.cpp).
  GET  /claims/target   effective/winning EvseProperties.
  GET  /status          has amp, pilot, state, voltage, vehicle.
"""

import time

import pytest
import requests

HEARTBEAT_TIMEOUT_S = 30  # loadsharing_heartbeat_timeout (valid range 5..600)
MARGIN_S = 15
UNREACHABLE_CONTROLLER = "localhost:59999"  # nothing listens here


def set_config(native_url, payload):
    r = requests.post(f"{native_url}/config", json=payload, timeout=10)
    assert r.status_code in (200, 201), f"POST /config -> {r.status_code}: {r.text}"
    return r


def get_config(native_url):
    r = requests.get(f"{native_url}/config", timeout=5)
    assert r.status_code == 200, r.text
    return r.json()


def get_loadsharing_status(native_url):
    r = requests.get(f"{native_url}/loadsharing/status", timeout=5)
    assert r.status_code == 200, r.text
    return r.json()


def get_claims(native_url):
    r = requests.get(f"{native_url}/claims", timeout=5)
    try:
        body = r.json()
    except Exception:
        body = r.text
    return r.status_code, body


def make_member_config(**overrides):
    cfg = {
        "loadsharing_enabled": True,
        "loadsharing_role": "member",
        # Controller unreachable from t=0: nothing listens on this port.
        "loadsharing_controller_host": UNREACHABLE_CONTROLLER,
        "loadsharing_heartbeat_timeout": HEARTBEAT_TIMEOUT_S,
        "loadsharing_failsafe_mode": "safe_current",
        "loadsharing_failsafe_safe_current": 6.0,
    }
    cfg.update(overrides)
    return cfg


@pytest.mark.timeout(120)
def test_member_failsafe_when_controller_unreachable(instance_pair):
    """Drill A: member with a dead controller host engages failsafe."""
    pair = instance_pair(port_offset=0)
    native_url = pair["native_url"]

    set_config(native_url, make_member_config())

    start = time.time()
    deadline = start + HEARTBEAT_TIMEOUT_S + MARGIN_S
    engaged_at = None
    status = None
    while time.time() < deadline:
        status = get_loadsharing_status(native_url)
        if status.get("failsafe_active"):
            engaged_at = time.time()
            break
        time.sleep(1.0)

    latency = None if engaged_at is None else round(engaged_at - start, 1)
    print(f"DRILL A: failsafe_active reached in ~{latency}s "
          f"(poll granularity 1s, heartbeat_timeout={HEARTBEAT_TIMEOUT_S}s)")

    # Is the failsafe actually ENFORCED locally, or is it only a status flag?
    # A real enforcement would show up as an OpenEVSE_LoadSharing claim on
    # the local EVSE limiting pilot to loadsharing_failsafe_safe_current.
    claims_code, claims = get_claims(native_url)
    ls_status = get_loadsharing_status(native_url)
    print(f"DRILL A: /claims status={claims_code} body={claims}")
    print(f"DRILL A: /loadsharing/status failsafe_active="
          f"{ls_status.get('failsafe_active')} allocations={ls_status.get('allocations')}")

    assert engaged_at is not None, (
        f"failsafe_active never became true within "
        f"{HEARTBEAT_TIMEOUT_S + MARGIN_S}s: {status}"
    )


@pytest.mark.timeout(180)
def test_manual_override_vs_allocation_claim(instance_pair):
    """Drill B (E4 evidence): with load sharing active on a member, apply a
    manual override for MORE amps than any allocation/failsafe cap and record
    which one wins at the EVSE claim layer. Records the outcome either way."""
    pair = instance_pair(port_offset=0)
    native_url = pair["native_url"]

    set_config(native_url, make_member_config())

    deadline = time.time() + HEARTBEAT_TIMEOUT_S + MARGIN_S
    while time.time() < deadline:
        if get_loadsharing_status(native_url).get("failsafe_active"):
            break
        time.sleep(1.0)
    else:
        pytest.skip("failsafe never engaged; Drill A must pass first")

    # Snapshot claims BEFORE override (is the LoadSharing failsafe even
    # placing a claim on the local EVSE?).
    before_code, before_claims = get_claims(native_url)
    print(f"DRILL B: /claims BEFORE override status={before_code} body={before_claims}")

    # Manual override: the "user taps charge-now at full amps" action.
    r = requests.post(f"{native_url}/override",
                      json={"state": "active", "charge_current": 32},
                      timeout=10)
    print(f"DRILL B: POST /override status={r.status_code} body={r.text[:200]}")
    assert r.status_code in (200, 201), r.text
    time.sleep(3)

    after_code, after_claims = get_claims(native_url)
    target = requests.get(f"{native_url}/claims/target", timeout=5)
    status = requests.get(f"{native_url}/status", timeout=5).json()
    pilot_fields = {k: v for k, v in status.items()
                    if 'current' in k or k in ('pilot', 'amp', 'state')}
    print(f"DRILL B: /claims AFTER override status={after_code} body={after_claims}")
    print(f"DRILL B: /claims/target status={target.status_code} body={target.text[:300]}")
    print(f"DRILL B: /status pilot-relevant fields={pilot_fields}")
    # The printed evidence (which client owns the effective target, and the
    # resulting pilot) is the deliverable. Test passes if endpoints respond.


@pytest.mark.timeout(60)
def test_config_accepts_failsafe_exceeding_group_budget(instance_pair):
    """Drill C: a failsafe safe-current (32 A) larger than the whole group
    budget (10 A) should arguably be rejected at config time (spec rule:
    sum of failsafe currents <= group max). Documents whether upstream
    validates this today."""
    pair = instance_pair(port_offset=0)
    native_url = pair["native_url"]

    r = requests.post(f"{native_url}/config", json={
        "loadsharing_enabled": True,
        "loadsharing_role": "member",
        "loadsharing_controller_host": UNREACHABLE_CONTROLLER,
        "loadsharing_group_max_current": 10.0,
        # One member's failsafe alone (32 A) exceeds the 10 A group budget:
        # if every member islands, the group draws well over budget.
        "loadsharing_failsafe_safe_current": 32.0,
    }, timeout=10)
    print(f"DRILL C: POST /config status={r.status_code} body={r.text[:200]}")

    stored = get_config(native_url)
    print(f"DRILL C: stored loadsharing_failsafe_safe_current="
          f"{stored.get('loadsharing_failsafe_safe_current')} "
          f"loadsharing_group_max_current={stored.get('loadsharing_group_max_current')}")
    # No assertion on acceptance/rejection -- the printed outcome is the
    # evidence for the Sigma(failsafe) <= budget findings item either way.
