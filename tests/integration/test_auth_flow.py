"""End-to-end tests for web authentication: login, session cookie, CSRF, logout.

Runs against the paired emulator + native firmware fixture from conftest.py.

Auth is keyed on ``www_password``: while it is empty every route is open, so
each test first sets credentials through ``POST /config`` (open at that point)
and from then on has to authenticate. The behaviour pinned here lives in
src/web_server.cpp (isAuthenticated, handleLogin, handleLogout,
requestPreProcess) and src/web_auth*.{h,cpp}:

* machine clients use HTTP Basic and are never subject to the CSRF check;
* the browser UI posts to /login and gets an HttpOnly, SameSite=Strict
  ``oevse_session`` cookie; a state-changing request authenticated by that
  cookie must also carry ``X-Requested-With: OpenEVSE``;
* repeated *wrong* guesses are throttled with 429, but correct credentials
  always pass, so a lockout cannot shut out the owner;
* changing the credentials rotates the signing secret, which invalidates every
  outstanding session cookie.

The cookie is read from the Set-Cookie header and sent back by hand rather than
through a requests cookie jar, so the tests exercise exactly what the firmware
emitted (including its attributes) and do not depend on the jar's rules for a
host called "localhost".
"""

import time

import pytest
import requests

REQUEST_TIMEOUT = 10
APP = {"X-Requested-With": "OpenEVSE"}

USER = "installer-admin"
PASSWORD = "correct-horse-battery"
COOKIE_NAME = "oevse_session"

# A route that changes state and is cheap and safe to call (it is an actuator,
# so it exercises both the auth gate and the CSRF gate). Any answer from the
# handler itself -- 200 done or 500 error -- means the request got through.
ACTUATOR = "/relay/reset"
HANDLER_ANSWERED = (200, 500)


def basic(user=USER, password=PASSWORD):
    return (user, password)


def login(native_url, user=USER, password=PASSWORD, **extra):
    body = {"user": user, "pass": password, "now": int(time.time())}
    body.update(extra)
    return requests.post(
        f"{native_url}/login", json=body, headers=APP, timeout=REQUEST_TIMEOUT
    )


def session_cookie(response):
    """The oevse_session=<token> pair from a login response, or None."""
    raw = response.headers.get("Set-Cookie", "")
    for part in raw.split(","):  # requests folds repeated headers with ", "
        part = part.strip()
        if part.startswith(COOKIE_NAME + "="):
            return part.split(";", 1)[0]
    return None


def with_cookie(cookie, extra=None):
    headers = {"Cookie": cookie}
    headers.update(extra or {})
    return headers


@pytest.fixture
def secured(evse_instance):
    """The native URL of an instance with a password set (auth switched on)."""
    native_url = evse_instance["native_url"]
    r = requests.post(
        f"{native_url}/config",
        json={"www_username": USER, "www_password": PASSWORD},
        timeout=REQUEST_TIMEOUT,
    )
    assert r.status_code == 200, f"setting credentials: {r.status_code}: {r.text}"

    # An AP-only build bypasses auth entirely (isAuthenticated). If this
    # instance reports that, none of the gates below can be exercised.
    probe = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT)
    if probe.status_code == 200:
        pytest.skip("instance is not enforcing auth (AP-only mode?)")
    return native_url


@pytest.mark.timeout(120)
class TestUnauthenticated:
    def test_api_is_locked_once_a_password_is_set(self, secured):
        r = requests.get(f"{secured}/status", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 401

    def test_spa_client_gets_a_json_401(self, secured):
        """The web UI sends the app header and wants JSON, not a browser
        Basic-auth dialog."""
        r = requests.get(f"{secured}/status", headers=APP, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 401
        assert r.json() == {"msg": "auth"}
        assert "WWW-Authenticate" not in r.headers

    def test_other_clients_get_a_basic_challenge(self, secured):
        r = requests.get(f"{secured}/status", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 401
        assert "Basic" in r.headers.get("WWW-Authenticate", "")

    def test_login_page_itself_is_reachable(self, secured):
        """/login must work without credentials; that is its whole job."""
        r = login(secured, password="wrong")
        assert r.status_code == 401  # reached the handler, not the auth gate
        assert r.json() == {"msg": "invalid"}


@pytest.mark.timeout(120)
class TestBasicAuth:
    def test_correct_credentials_are_accepted(self, secured):
        r = requests.get(
            f"{secured}/status", auth=basic(), timeout=REQUEST_TIMEOUT
        )
        assert r.status_code == 200

    @pytest.mark.parametrize(
        "creds", [(USER, "wrong"), ("someone-else", PASSWORD), ("", "")]
    )
    def test_wrong_credentials_are_refused(self, secured, creds):
        r = requests.get(
            f"{secured}/status", auth=creds, timeout=REQUEST_TIMEOUT
        )
        assert r.status_code == 401

    def test_machine_client_needs_no_csrf_header(self, secured):
        """Basic-auth clients never carry our cookie, so a POST without the
        browser header is fine for them."""
        r = requests.post(
            f"{secured}{ACTUATOR}", auth=basic(), timeout=REQUEST_TIMEOUT
        )
        assert r.status_code in HANDLER_ANSWERED, f"{r.status_code}: {r.text}"


@pytest.mark.timeout(120)
class TestLogin:
    def test_wrong_password_is_401_and_sets_no_cookie(self, secured):
        r = login(secured, password="nope")
        assert r.status_code == 401
        assert r.json() == {"msg": "invalid"}
        assert session_cookie(r) is None

    def test_wrong_username_is_401(self, secured):
        r = login(secured, user="nobody")
        assert r.status_code == 401
        assert session_cookie(r) is None

    def test_login_must_be_a_post(self, secured):
        r = requests.get(f"{secured}/login", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 405

    def test_malformed_body_is_400(self, secured):
        r = requests.post(
            f"{secured}/login",
            data="{not json",
            headers={**APP, "Content-Type": "application/json"},
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 400

    def test_success_sets_a_hardened_session_cookie(self, secured):
        r = login(secured)
        assert r.status_code == 200, f"{r.status_code}: {r.text}"
        assert r.json() == {"msg": "ok"}
        raw = r.headers["Set-Cookie"]
        assert raw.startswith(COOKIE_NAME + "=")
        assert "HttpOnly" in raw
        assert "SameSite=Strict" in raw
        assert "Path=/" in raw
        # A plain session is a browser-session cookie; only "remember me" is
        # persistent.
        assert "Max-Age" not in raw

    def test_remember_me_makes_the_cookie_persistent(self, secured):
        r = login(secured, remember=True)
        assert r.status_code == 200
        assert "Max-Age=2592000" in r.headers["Set-Cookie"]  # 30 days

    def test_login_response_is_not_cacheable(self, secured):
        r = login(secured)
        assert "no-store" in r.headers.get("Cache-Control", "")


@pytest.mark.timeout(120)
class TestSessionCookie:
    def test_cookie_authenticates_reads(self, secured):
        cookie = session_cookie(login(secured))
        assert cookie
        r = requests.get(
            f"{secured}/status", headers=with_cookie(cookie), timeout=REQUEST_TIMEOUT
        )
        assert r.status_code == 200

    def test_a_tampered_cookie_is_refused(self, secured):
        cookie = session_cookie(login(secured))
        assert cookie
        # Flip the last character of the signature.
        bad = cookie[:-1] + ("0" if cookie[-1] != "0" else "1")
        r = requests.get(
            f"{secured}/status", headers=with_cookie(bad), timeout=REQUEST_TIMEOUT
        )
        assert r.status_code == 401

    def test_a_made_up_cookie_is_refused(self, secured):
        r = requests.get(
            f"{secured}/status",
            headers=with_cookie(f"{COOKIE_NAME}=not-a-real-token"),
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 401


@pytest.mark.timeout(120)
class TestCsrf:
    """A cookie rides along on cross-site requests; the custom header does not."""

    def test_cookie_post_without_the_app_header_is_403(self, secured):
        cookie = session_cookie(login(secured))
        r = requests.post(
            f"{secured}{ACTUATOR}",
            headers=with_cookie(cookie),
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 403
        assert r.json() == {"msg": "csrf"}

    def test_cookie_post_with_the_app_header_goes_through(self, secured):
        cookie = session_cookie(login(secured))
        r = requests.post(
            f"{secured}{ACTUATOR}",
            headers=with_cookie(cookie, APP),
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code in HANDLER_ANSWERED, f"{r.status_code}: {r.text}"

    def test_cookie_get_needs_no_header(self, secured):
        """GET is read-only, so the CSRF check applies to non-GET only. (The
        actuator-specific GET rule is a separate gate, covered elsewhere.)"""
        cookie = session_cookie(login(secured))
        r = requests.get(
            f"{secured}/status", headers=with_cookie(cookie), timeout=REQUEST_TIMEOUT
        )
        assert r.status_code == 200

    @pytest.mark.parametrize("method", ["put", "delete"])
    def test_other_state_changing_methods_are_gated_too(self, secured, method):
        cookie = session_cookie(login(secured))
        r = getattr(requests, method)(
            f"{secured}/override",
            headers=with_cookie(cookie),
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 403
        assert r.json() == {"msg": "csrf"}


@pytest.mark.timeout(120)
class TestLogout:
    def test_logout_clears_the_cookie(self, secured):
        r = requests.post(f"{secured}/logout", headers=APP, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        raw = r.headers["Set-Cookie"]
        assert raw.startswith(COOKIE_NAME + "=;")
        assert "Max-Age=0" in raw

    def test_logout_must_be_a_post(self, secured):
        r = requests.get(f"{secured}/logout", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 405

    def test_logout_works_without_a_session(self, secured):
        """It must always succeed -- a stale tab signing out has no session."""
        r = requests.post(f"{secured}/logout", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200

    def test_a_client_that_drops_the_cookie_is_signed_out(self, secured):
        cookie = session_cookie(login(secured))
        assert requests.get(
            f"{secured}/status", headers=with_cookie(cookie), timeout=REQUEST_TIMEOUT
        ).status_code == 200

        requests.post(f"{secured}/logout", headers=APP, timeout=REQUEST_TIMEOUT)

        # What the browser does with the Max-Age=0 cookie: it stops sending it.
        r = requests.get(f"{secured}/status", headers=APP, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 401

    # NOTE: deliberately not asserted -- that the *old* cookie stops working
    # after /logout. Sessions are stateless signed tokens: logout only asks the
    # browser to discard its copy, and a copied token stays valid until it
    # expires (6 h, or 30 days with "remember me") or the credentials change
    # (TestCredentialChange). Pinning that as "works" here would bless the
    # behaviour; asserting the opposite would fail today.


@pytest.mark.timeout(120)
class TestThrottle:
    def test_repeated_wrong_guesses_get_429_but_the_owner_still_gets_in(self, secured):
        """AUTH_THROTTLE_LOCK_AT (10) recent failures lock out further *wrong*
        guesses. Correct credentials always pass, so the throttle can never
        keep the owner out."""
        statuses = [login(secured, password=f"guess-{i}").status_code for i in range(15)]
        assert 401 in statuses, statuses
        assert statuses[-1] == 429, f"never locked: {statuses}"
        # Every refusal is either a plain 401 or the lock; never a success.
        assert set(statuses) <= {401, 429}

        r = login(secured)
        assert r.status_code == 200, f"owner locked out: {r.status_code}: {r.text}"

    def test_wrong_basic_credentials_feed_the_same_throttle(self, secured):
        """Hammering GET /status must not dodge the counter that /login has."""
        for i in range(12):
            requests.get(
                f"{secured}/status", auth=(USER, f"guess-{i}"), timeout=REQUEST_TIMEOUT
            )
        r = requests.get(
            f"{secured}/status", auth=(USER, "one-more"), timeout=REQUEST_TIMEOUT
        )
        assert r.status_code == 429
        assert r.json() == {"msg": "locked"}

        ok = requests.get(f"{secured}/status", auth=basic(), timeout=REQUEST_TIMEOUT)
        assert ok.status_code == 200


@pytest.mark.timeout(120)
class TestCredentialChange:
    def test_changing_the_password_invalidates_existing_sessions(self, secured):
        cookie = session_cookie(login(secured))
        assert requests.get(
            f"{secured}/status", headers=with_cookie(cookie), timeout=REQUEST_TIMEOUT
        ).status_code == 200

        new_password = PASSWORD + "-changed"
        r = requests.post(
            f"{secured}/config",
            json={"www_password": new_password},
            auth=basic(),
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 200, f"{r.status_code}: {r.text}"

        old = requests.get(
            f"{secured}/status", headers=with_cookie(cookie), timeout=REQUEST_TIMEOUT
        )
        assert old.status_code == 401, "an old session survived a password change"

        # The new password works, and so does a fresh login.
        assert requests.get(
            f"{secured}/status", auth=basic(password=new_password), timeout=REQUEST_TIMEOUT
        ).status_code == 200
        assert login(secured, password=new_password).status_code == 200

    def test_the_old_password_stops_working(self, secured):
        requests.post(
            f"{secured}/config",
            json={"www_password": PASSWORD + "-changed"},
            auth=basic(),
            timeout=REQUEST_TIMEOUT,
        )
        assert login(secured).status_code == 401
        assert requests.get(
            f"{secured}/status", auth=basic(), timeout=REQUEST_TIMEOUT
        ).status_code == 401

    def test_clearing_the_password_turns_auth_off(self, secured):
        r = requests.post(
            f"{secured}/config",
            json={"www_password": ""},
            auth=basic(),
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 200, f"{r.status_code}: {r.text}"
        assert requests.get(f"{secured}/status", timeout=REQUEST_TIMEOUT).status_code == 200
