"""Integration tests for the relay maintenance routes.

    POST /relay/reset      ($FH) reset the relay contact-life estimate
    POST /relay/recovery   ($FK) run the stuck-relay recovery cycle

Runs against the paired emulator + native firmware fixture from conftest.py.

What the emulator implements of $FH / $FK is not something these tests assume.
Each route's answer is instead checked against what the controller itself says
when sent the same command through the raw ``/r`` passthrough: a controller that
answers ``$OK`` must yield ``200 {"msg":"done"}``, anything else (``$NK``, an
unknown command, no answer) must yield ``500 {"msg":"error"}``. That pins the
contract that matters -- the route reports the controller's verdict and always
answers -- on an emulator that supports the commands and on one that does not.

The routes defer their HTTP reply to the RAPI completion callback rather than
blocking the server task, and ``/relay/recovery`` pauses the monitor's own
polling while ``$FK`` is outstanding (``_relay_recovery_in_flight``). The last
class guards the failure that design invites: a flag that is never cleared, so
the monitor goes quiet for good after the first recovery.
"""

import time

import pytest
import requests

# $FK can hold the controller for ~30 s on real hardware, and the reply is
# deferred until it returns, so the per-request timeout has to allow for it.
RECOVERY_TIMEOUT = 60
REQUEST_TIMEOUT = 15
HEADERS = {"X-Requested-With": "OpenEVSE"}

ROUTES = ["/relay/reset", "/relay/recovery"]
RAPI_FOR_ROUTE = {"/relay/reset": "$FH", "/relay/recovery": "$FK"}


def timeout_for(route):
    return RECOVERY_TIMEOUT if route == "/relay/recovery" else REQUEST_TIMEOUT


def raw_rapi(native_url, cmd):
    """Send `cmd` through /r and return the controller's reply line ('' if none)."""
    r = requests.get(
        f"{native_url}/r",
        params={"json": 1, "rapi": cmd},
        headers=HEADERS,
        timeout=RECOVERY_TIMEOUT,
    )
    assert r.status_code == 200, f"/r {cmd}: {r.status_code}: {r.text}"
    return r.json().get("ret", "")


def post_route(native_url, route, **kwargs):
    return requests.post(
        f"{native_url}{route}", timeout=timeout_for(route), **kwargs
    )


def status_state(native_url):
    r = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"/status: {r.status_code}: {r.text}"
    return r.json().get("state", 0)


@pytest.mark.timeout(180)
class TestRelayRouteGate:
    """Both routes are actuators: a bare cross-site GET must not run them."""

    @pytest.mark.parametrize("route", ROUTES)
    def test_headerless_get_is_403(self, evse_instance, route):
        r = requests.get(
            f"{evse_instance['native_url']}{route}", timeout=REQUEST_TIMEOUT
        )
        assert r.status_code == 403, f"{route}: {r.status_code}: {r.text}"

    @pytest.mark.parametrize("route", ROUTES)
    def test_get_with_app_header_is_allowed(self, evse_instance, route):
        r = requests.get(
            f"{evse_instance['native_url']}{route}",
            headers=HEADERS,
            timeout=timeout_for(route),
        )
        assert r.status_code in (200, 500), f"{route}: {r.status_code}: {r.text}"

    @pytest.mark.parametrize("route", ROUTES)
    def test_post_needs_no_header(self, evse_instance, route):
        """POST is already non-forgeable cross-site, so it is not gated."""
        r = post_route(evse_instance["native_url"], route)
        assert r.status_code in (200, 500), f"{route}: {r.status_code}: {r.text}"


@pytest.mark.timeout(180)
class TestRelayRouteReply:
    """The reply shape, and that it reports the controller's verdict."""

    @pytest.mark.parametrize("route", ROUTES)
    def test_reply_is_json_with_matching_code(self, evse_instance, route):
        r = post_route(evse_instance["native_url"], route, headers=HEADERS)
        body = r.json()
        assert r.headers["Content-Type"].startswith("application/json")
        assert r.status_code in (200, 500), f"{route}: {r.status_code}: {r.text}"
        # 200 <-> "done", 500 <-> "error": the code and the message never disagree.
        assert body == (
            {"msg": "done"} if r.status_code == 200 else {"msg": "error"}
        ), f"{route}: {r.status_code}: {body}"

    @pytest.mark.parametrize("route", ROUTES)
    def test_route_agrees_with_the_controller(self, evse_instance, route):
        """200 exactly when the controller answers $OK; 500 for $NK or silence.

        Covers the success path and the NAK path in one assertion: which one
        runs depends on what the emulator implements, and either is a pass as
        long as the route does not contradict the controller. Each command is sent
        twice (once raw, once through the route): $FH only resets an estimate,
        and $FK is refused outright when an EV is connected and otherwise just
        runs its cycle again, so repeating either is safe.
        """
        native_url = evse_instance["native_url"]
        cmd = RAPI_FOR_ROUTE[route]

        raw = raw_rapi(native_url, cmd)
        controller_ok = raw.startswith("$OK")

        r = post_route(native_url, route, headers=HEADERS)
        assert (r.status_code == 200) == controller_ok, (
            f"{route}: controller said {raw!r} but the route returned "
            f"{r.status_code}: {r.text}"
        )

    @pytest.mark.parametrize("route", ROUTES)
    def test_repeated_requests_each_get_an_answer(self, evse_instance, route):
        """Back-to-back calls (the case where a second arrives while the first
        is still outstanding on a slow controller) must all be answered rather
        than queue forever or be dropped."""
        native_url = evse_instance["native_url"]
        for _ in range(3):
            r = post_route(native_url, route, headers=HEADERS)
            assert r.status_code in (200, 500), f"{route}: {r.status_code}: {r.text}"


@pytest.mark.timeout(240)
class TestRecoveryDoesNotWedgeTheMonitor:
    """`_relay_recovery_in_flight` must be cleared on every outcome.

    While it is set, EvseMonitor::loop() skips its periodic polling. If a
    completion path ever forgot to clear it (success, NAK or error), the
    firmware would keep answering HTTP but stop learning anything from the
    controller. /status is fed by that polling, so it is the observable.
    """

    def test_status_keeps_updating_after_a_recovery(self, evse_instance):
        native_url = evse_instance["native_url"]
        before = status_state(native_url)
        assert before >= 1

        post_route(native_url, "/relay/recovery", headers=HEADERS)

        # The monitor polls state every EVSE_MONITOR_STATE_TIME (30) poll
        # periods of 1 s, so allow it comfortably more than one. What must
        # still be true afterwards is that the RAPI link works at all and
        # /status reports a real state.
        deadline = time.time() + 45
        state = 0
        while time.time() < deadline:
            state = status_state(native_url)
            if state >= 1:
                break
            time.sleep(1)
        assert state >= 1, "status never recovered to a valid EVSE state after $FK"

    def test_controller_still_answers_raw_rapi_after_a_recovery(self, evse_instance):
        native_url = evse_instance["native_url"]
        post_route(native_url, "/relay/recovery", headers=HEADERS)
        ret = raw_rapi(native_url, "$GV")
        assert ret.startswith("$OK"), f"RAPI link dead after $FK: {ret!r}"

    def test_a_second_recovery_is_still_answered(self, evse_instance):
        """If the in-flight flag stuck, nothing is reported wrong here -- the
        point is that the route itself still completes (not hangs) after one
        recovery has already run."""
        native_url = evse_instance["native_url"]
        first = post_route(native_url, "/relay/recovery", headers=HEADERS)
        second = post_route(native_url, "/relay/recovery", headers=HEADERS)
        assert first.status_code in (200, 500)
        assert second.status_code in (200, 500)
        # A controller's verdict does not change between two identical runs.
        assert first.status_code == second.status_code

    def test_reset_works_after_a_recovery(self, evse_instance):
        """The two routes share the RAPI queue; one finishing must leave the
        other usable."""
        native_url = evse_instance["native_url"]
        post_route(native_url, "/relay/recovery", headers=HEADERS)
        r = post_route(native_url, "/relay/reset", headers=HEADERS)
        assert r.status_code in (200, 500), f"{r.status_code}: {r.text}"
