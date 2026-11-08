"""End-to-end tests for load sharing peer authentication (issue #1172).

Runs against the paired emulator + native firmware fixtures from conftest.py.

Before this existed, setting ``www_password`` silently broke load sharing: peers
sent no credential, so every bootstrap, config push and websocket was refused.
No test noticed, because nothing ran peers against an auth-enabled server.

Each test sets a password and a group key, then acts either as a peer of the
instance (signing by hand, so the wire format is pinned independently of the C++
client) or as the operator of a real two-instance group. The behaviour lives in
src/loadsharing_auth.*, src/loadsharing_peer_client.cpp,
src/web_server_loadsharing.cpp and handleConfigPost().
"""

import hashlib
import hmac
import time

import pytest
import requests

REQUEST_TIMEOUT = 10

USER = "installer-admin"
PASSWORD = "correct-horse-battery"
GROUP_KEY = "0123456789abcdef-group-key"
OTHER_KEY = "fedcba9876543210-other-key"


def nonce_for(url):
    r = requests.get(f"{url}/loadsharing/nonce", timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"nonce: {r.status_code}: {r.text}"
    return r.json()["nonce"]


def signature(key, method, path, nonce, body=""):
    body_hash = hashlib.sha256(body.encode()).hexdigest()
    message = f"oevse-loadsharing-v1\n{method}\n{path}\n{nonce}\n{body_hash}"
    mac = hmac.new(key.encode(), message.encode(), hashlib.sha256).hexdigest()
    return f"OpenEVSE-Peer v1.{nonce}.{mac}"


def signed(url, method, path, body="", key=GROUP_KEY):
    headers = {"Authorization": signature(key, method, path, nonce_for(url), body)}
    if body:
        headers["Content-Type"] = "application/json"
    return requests.request(method, f"{url}{path}", data=body, headers=headers,
                            timeout=REQUEST_TIMEOUT)


def secure(url, group_key=GROUP_KEY):
    r = requests.post(
        f"{url}/config",
        json={"www_username": USER, "www_password": PASSWORD,
              "loadsharing_group_key": group_key},
        timeout=REQUEST_TIMEOUT,
    )
    assert r.status_code == 200, f"securing: {r.status_code}: {r.text}"


@pytest.fixture
def secured(evse_instance):
    """The native URL of an instance with a password and a group key set."""
    native_url = evse_instance["native_url"]
    secure(native_url)

    # An AP-only build bypasses auth entirely (isAuthenticated). If this
    # instance reports that, none of the gates below can be exercised.
    probe = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT)
    if probe.status_code == 200:
        pytest.skip("instance is not enforcing auth (AP-only mode?)")
    return native_url


@pytest.mark.timeout(120)
class TestNonce:
    def test_no_password_means_nothing_to_sign_for(self, evse_instance):
        native_url = evse_instance["native_url"]
        r = requests.post(f"{native_url}/config",
                          json={"loadsharing_group_key": GROUP_KEY},
                          timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        r = requests.get(f"{native_url}/loadsharing/nonce", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 404

    def test_needs_no_credential_once_secured(self, secured):
        assert len(nonce_for(secured)) == 32

    def test_is_different_every_time(self, secured):
        assert nonce_for(secured) != nonce_for(secured)


@pytest.mark.timeout(120)
class TestPeerRequests:
    @pytest.mark.parametrize("path", ["/status", "/config"])
    def test_signed_read_is_accepted(self, secured, path):
        assert signed(secured, "GET", path).status_code == 200

    def test_a_captured_request_cannot_be_replayed(self, secured):
        headers = {"Authorization": signature(GROUP_KEY, "GET", "/status", nonce_for(secured))}
        first = requests.get(f"{secured}/status", headers=headers, timeout=REQUEST_TIMEOUT)
        replay = requests.get(f"{secured}/status", headers=headers, timeout=REQUEST_TIMEOUT)
        assert first.status_code == 200
        assert replay.status_code == 401

    def test_the_wrong_key_is_refused(self, secured):
        assert signed(secured, "GET", "/status", key=OTHER_KEY).status_code == 401

    def test_a_made_up_nonce_is_refused(self, secured):
        nonce = "00112233445566778899aabbccddeeff"
        headers = {"Authorization": signature(GROUP_KEY, "GET", "/status", nonce)}
        r = requests.get(f"{secured}/status", headers=headers, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 401

    def test_a_tampered_body_is_refused(self, secured):
        nonce = nonce_for(secured)
        headers = {
            "Authorization": signature(GROUP_KEY, "POST", "/config", nonce,
                                       '{"loadsharing_group_id":"a"}'),
            "Content-Type": "application/json",
        }
        r = requests.post(f"{secured}/config", data='{"loadsharing_group_id":"b"}',
                          headers=headers, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 401

    def test_failed_peer_attempts_do_not_lock_out_the_owner(self, secured):
        for _ in range(12):
            signed(secured, "GET", "/status", key=OTHER_KEY)
        r = requests.get(f"{secured}/status", auth=(USER, PASSWORD), timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200


@pytest.mark.timeout(120)
class TestPeerScope:
    """The group key must not be as good as the admin password."""

    @pytest.mark.parametrize("method,path", [
        ("POST", "/restart"),
        ("GET", "/debug/crash/raw"),
        ("GET", "/loadsharing/peers"),
        ("GET", "/certificates"),
    ])
    def test_other_routes_stay_closed(self, secured, method, path):
        assert signed(secured, method, path).status_code == 401

    @pytest.mark.parametrize("body", [
        '{"www_password":"hijacked"}',
        '{"loadsharing_group_key":"attacker-chosen-group-key"}',
        '{"loadsharing_group_max_current":500}',
        '{"loadsharing_group_id":"ok","www_username":"hijacked"}',
    ])
    def test_config_outside_load_sharing_is_forbidden(self, secured, body):
        r = signed(secured, "POST", "/config", body)
        assert r.status_code == 403, r.text
        # The original credential still works, so nothing was applied.
        r = requests.get(f"{secured}/status", auth=(USER, PASSWORD), timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200

    def test_config_push_is_accepted(self, secured):
        assert signed(secured, "POST", "/config",
                      '{"loadsharing_group_id":"pushed"}').status_code == 200

    def test_factory_storage_is_forbidden(self, secured):
        body = '{"loadsharing_group_id":"factory"}'
        headers = {
            "Authorization": signature(GROUP_KEY, "POST", "/config", nonce_for(secured), body),
            "Content-Type": "application/json",
            "X-Storage": "factory",
        }
        r = requests.post(f"{secured}/config", data=body, headers=headers,
                          timeout=REQUEST_TIMEOUT)
        assert r.status_code == 403


@pytest.mark.timeout(120)
class TestGroupKeyConfig:
    def admin(self, url, **fields):
        return requests.post(f"{url}/config", json=fields, auth=(USER, PASSWORD),
                             timeout=REQUEST_TIMEOUT)

    def test_is_not_readable(self, secured):
        r = requests.get(f"{secured}/config", auth=(USER, PASSWORD), timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        assert GROUP_KEY not in r.text
        assert r.json()["loadsharing_group_key"] == "_DUMMY_PASSWORD"

    @pytest.mark.parametrize("key", ["short", "x" * 65])
    def test_rejects_a_weak_or_oversized_key(self, secured, key):
        assert self.admin(secured, loadsharing_group_key=key).status_code == 400

    def test_can_be_cleared(self, secured):
        assert self.admin(secured, loadsharing_group_key="").status_code == 200
        r = requests.get(f"{secured}/loadsharing/nonce", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 404

    def test_saving_the_form_back_keeps_the_key(self, secured):
        r = self.admin(secured, loadsharing_group_key="_DUMMY_PASSWORD")
        assert r.status_code == 200
        assert signed(secured, "GET", "/status").status_code == 200

    def test_stays_writable_on_a_member(self, secured):
        r = signed(secured, "POST", "/config",
                   '{"loadsharing_enabled":true,"loadsharing_role":true,'
                   '"loadsharing_controller_host":"controller.local"}')
        assert r.status_code == 200
        assert self.admin(secured, loadsharing_group_key=OTHER_KEY).status_code == 200
        assert signed(secured, "GET", "/status", key=OTHER_KEY).status_code == 200


def peer_row(controller_url, member_url):
    r = requests.get(f"{controller_url}/loadsharing/status", auth=(USER, PASSWORD),
                     timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200
    host = member_url.split("://", 1)[1]
    for peer in r.json().get("peers", []):
        if peer.get("host") == host:
            return peer
    return None


def wait_for_peer(controller_url, member_url, predicate, timeout=60):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = peer_row(controller_url, member_url)
        if last is not None and predicate(last):
            return last
        time.sleep(1)
    pytest.fail(f"peer never reached the expected state, last seen: {last}")


@pytest.mark.timeout(180)
class TestGroupWithPasswords:
    """The issue itself: a real controller and member, both password protected."""

    def form_group(self, multi_instance_group, controller_key, member_key):
        # Two pairs on distinct port offsets. instance_pair_auto() would reuse
        # the test's one offset, so its second call tears down the first
        # instance and the "group" would be a single device.
        controller_pair, member_pair = multi_instance_group(2)
        controller = controller_pair["native_url"]
        member = member_pair["native_url"]
        assert controller != member
        secure(controller, controller_key)
        secure(member, member_key)

        r = requests.post(f"{controller}/config", json={"loadsharing_enabled": True},
                          auth=(USER, PASSWORD), timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        r = requests.post(f"{controller}/loadsharing/peers",
                          json={"host": member.split("://", 1)[1]},
                          auth=(USER, PASSWORD), timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200, r.text
        return controller, member

    def test_matching_keys_bring_the_member_online(self, multi_instance_group):
        controller, member = self.form_group(multi_instance_group, GROUP_KEY, GROUP_KEY)
        wait_for_peer(controller, member, lambda p: p.get("online"))

        # Config push reached the member over a signed POST /config
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            config = requests.get(f"{member}/config", auth=(USER, PASSWORD),
                                  timeout=REQUEST_TIMEOUT).json()
            if config.get("loadsharing_role") is True:
                return
            time.sleep(1)
        pytest.fail("member never received the config push")

    def test_mismatched_keys_are_reported(self, multi_instance_group):
        controller, member = self.form_group(multi_instance_group, GROUP_KEY, OTHER_KEY)
        peer = wait_for_peer(controller, member, lambda p: p.get("error") == "auth_failed")
        assert not peer.get("online")
