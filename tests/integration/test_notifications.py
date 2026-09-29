"""Integration tests for the /notifications REST contract.

Runs against the paired emulator + native firmware fixtures from conftest.py
(same harness as test_boost.py, which uses the ``evse_instance`` fixture and
its ``native_url`` / ``emulator_url`` entries).

The test that matters most here is ``test_ack_round_trip``: the ack endpoint
reads its id with ArduinoMongoose's ``getParam()``, which consults the query
string only for GET and the request body for every other method, so the
documented ``POST /notifications/ack?id=...`` with an empty body silently 400'd
for the whole of this feature's life before review caught it.  That is exactly
the shape of bug an integration test exists to catch, so the round trip below
deliberately uses the documented URL form and sends no body.

The advisory it raises is ``fault.gfci_tripped``, driven by tripping the GFCI
on the emulator's own error API.  The safety-check advisories would be the
more obvious choice, but the emulator implements no ``$FF`` (feature
enable/disable), so a safety check can never be switched off there and
``safety.*`` can never raise against it.
"""

import time

import pytest
import requests

REQUEST_TIMEOUT = 10

# The advisory engine runs on a 5 s MicroTask (EVSE_NOTIFICATIONS_LOOP_TIME),
# and the fault counters behind fault.* are re-read from the controller ($GF)
# while it reports an error, so a trip takes a poll plus one loop to appear.
ADVISORY_SETTLE_TIMEOUT = 20

SEVERITIES = {"info", "warning", "critical"}
CATEGORIES = {"safety", "fault", "wear", "thermal"}

# Raised while the controller's GFCI trip counter is non-zero, tokened on the
# count so a later trip re-raises it (src/notifications_rules.cpp).
TRIP_ADVISORY_ID = "fault.gfci_tripped"


def get_notifications(native_url):
    r = requests.get(f"{native_url}/notifications", timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"GET /notifications: {r.status_code}: {r.text}"
    return r.json()


def find_advisory(body, advisory_id):
    for n in body.get("notifications", []):
        if n.get("id") == advisory_id:
            return n
    return None


def wait_for_advisory(native_url, advisory_id, present=True):
    """Poll /notifications until `advisory_id` is (or is no longer) listed."""
    deadline = time.time() + ADVISORY_SETTLE_TIMEOUT
    body = None
    while time.time() < deadline:
        body = get_notifications(native_url)
        if (find_advisory(body, advisory_id) is not None) == present:
            return body
        time.sleep(0.5)
    pytest.fail(
        f"advisory {advisory_id} "
        f"{'never appeared' if present else 'never cleared'} within "
        f"{ADVISORY_SETTLE_TIMEOUT}s; last body: {body}"
    )


def stable_summary(native_url):
    """Read /status and /notifications until two consecutive passes agree.

    The two endpoints are independent snapshots and the notification task can
    change the set between them; comparing a single pair can fail while both
    are correct.
    """
    deadline = time.time() + ADVISORY_SETTLE_TIMEOUT
    last = None
    while time.time() < deadline:
        status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()
        body = get_notifications(native_url)
        current = (status["notifications"], body["count"], body["max_severity"])
        if current == last:
            return status["notifications"], body
        last = current
        time.sleep(0.5)
    pytest.fail(f"/status and /notifications never settled; last: {last}")


def trip_gfci(emulator_url):
    r = requests.post(
        f"{emulator_url}/api/errors/trigger",
        json={"error": "gfci"},
        timeout=REQUEST_TIMEOUT,
    )
    assert r.status_code == 200, f"emulator trigger: {r.status_code}: {r.text}"


def clear_errors(emulator_url):
    requests.post(f"{emulator_url}/api/errors/clear", timeout=REQUEST_TIMEOUT)


class TestNotificationsRest:
    def test_get_has_the_documented_shape(self, evse_instance):
        """GET /notifications parses, and every field is the documented type."""
        native_url = evse_instance["native_url"]
        body = get_notifications(native_url)

        assert isinstance(body["count"], int)
        assert body["count"] >= 0
        # A string, not an integer: /status, the websocket event and this
        # endpoint all have to agree, or a GUI merging them sees the field
        # change type under it.
        assert body["max_severity"] in SEVERITIES
        assert isinstance(body["notifications"], list)

        # count is the UNMUTED total, so it can only be <= the list length.
        assert body["count"] <= len(body["notifications"])

        for n in body["notifications"]:
            assert isinstance(n["id"], str) and "." in n["id"]
            assert n["category"] in CATEGORIES
            assert n["severity"] in SEVERITIES
            assert isinstance(n["sticky"], bool)
            assert isinstance(n["acked"], bool)
            # Epoch seconds, or 0 for "the clock had not synced".  Never
            # uptime seconds, which is what these used to carry.
            for field in ("first_seen", "last_seen"):
                assert isinstance(n[field], int)
                assert n[field] == 0 or n[field] > 1_600_000_000, (
                    f"{n['id']}.{field} = {n[field]} is neither 0 (unknown) "
                    "nor a plausible epoch time"
                )

    def test_status_summary_fields(self, evse_instance):
        """/status carries the two-field summary, with severity as a name."""
        native_url = evse_instance["native_url"]
        status = requests.get(f"{native_url}/status", timeout=REQUEST_TIMEOUT).json()

        assert "notifications" in status, "/status is missing the notifications object"
        summary = status["notifications"]
        assert isinstance(summary["count"], int)
        assert isinstance(summary["severity"], str), (
            f"severity must be a name, got {summary['severity']!r}"
        )
        assert summary["severity"] in SEVERITIES

        # The summary is the same view /notifications reports.
        summary, body = stable_summary(native_url)
        assert summary["count"] == body["count"]
        assert summary["severity"] == body["max_severity"]

    def test_ack_missing_id_is_400(self, evse_instance):
        native_url = evse_instance["native_url"]
        r = requests.post(
            f"{native_url}/notifications/ack",
            headers={"X-Requested-With": "OpenEVSE"},
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 400, f"Expected 400, got {r.status_code}: {r.text}"

    def test_ack_unknown_id_is_404(self, evse_instance):
        native_url = evse_instance["native_url"]
        r = requests.post(
            f"{native_url}/notifications/ack",
            params={"id": "safety.no_such_advisory"},
            headers={"X-Requested-With": "OpenEVSE"},
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 404, f"Expected 404, got {r.status_code}: {r.text}"

    def test_headerless_get_is_403(self, evse_instance):
        """A cross-site GET of the ack route is refused.

        actuatorMethodAllowed() (src/web_server.cpp) lets a GET through only
        when it carries X-Requested-With: OpenEVSE, which a cross-origin form
        or an <img> tag cannot set.  Without it the state change is refused.
        """
        native_url = evse_instance["native_url"]
        r = requests.get(
            f"{native_url}/notifications/ack",
            params={"id": TRIP_ADVISORY_ID},
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 403, f"Expected 403, got {r.status_code}: {r.text}"

    def test_ack_wrong_method_is_405(self, evse_instance):
        """Only POST and the guarded GET may change ack state."""
        native_url = evse_instance["native_url"]
        r = requests.put(
            f"{native_url}/notifications/ack",
            params={"id": TRIP_ADVISORY_ID},
            headers={"X-Requested-With": "OpenEVSE"},
            timeout=REQUEST_TIMEOUT,
        )
        assert r.status_code == 405, f"Expected 405, got {r.status_code}: {r.text}"

    def test_ack_round_trip(self, evse_instance):
        """Raise an advisory, ack it over the documented URL, see it dismissed.

        A GFCI trip on the emulator moves the controller's trip counter, which
        raises fault.gfci_tripped.  It is not sticky: acking dismisses it, so it
        stays in the list marked acked and drops out of `count`.  The ack goes
        out as POST /notifications/ack?id=... with NO body - the documented
        form, and the one that used to 400.  A second trip then moves the
        counter, and with it the token the ack was recorded against, so the
        advisory has to come back un-acked on its own.
        """
        native_url = evse_instance["native_url"]
        emulator_url = evse_instance["emulator_url"]
        headers = {"X-Requested-With": "OpenEVSE"}

        trip_gfci(emulator_url)
        try:
            body = wait_for_advisory(native_url, TRIP_ADVISORY_ID, present=True)
            raised = find_advisory(body, TRIP_ADVISORY_ID)
            assert raised["acked"] is False
            assert raised["sticky"] is False
            assert raised["category"] == "fault"
            assert raised["severity"] == "critical"
            count_before = body["count"]
            assert count_before >= 1

            # The documented URL form, empty body.
            r = requests.post(
                f"{native_url}/notifications/ack",
                params={"id": TRIP_ADVISORY_ID},
                headers=headers,
                timeout=REQUEST_TIMEOUT,
            )
            assert r.status_code == 200, (
                f"POST /notifications/ack?id={TRIP_ADVISORY_ID} (no body) "
                f"returned {r.status_code}: {r.text}"
            )

            body = get_notifications(native_url)
            acked = find_advisory(body, TRIP_ADVISORY_ID)
            assert acked is not None, "an acked advisory must stay listed"
            assert acked["acked"] is True
            assert body["count"] == count_before - 1, (
                "acking must drop the advisory out of the unmuted count"
            )

            # /status follows the same view.
            summary, body = stable_summary(native_url)
            assert summary["count"] == body["count"]

            # A second trip moves the counter and so the token; the stored ack
            # no longer matches and the advisory is live again.
            clear_errors(emulator_url)
            trip_gfci(emulator_url)
            deadline = time.time() + ADVISORY_SETTLE_TIMEOUT
            reraised = None
            while time.time() < deadline:
                reraised = find_advisory(get_notifications(native_url), TRIP_ADVISORY_ID)
                if reraised is not None and reraised["acked"] is False:
                    break
                time.sleep(0.5)
            assert reraised is not None and reraised["acked"] is False, (
                f"a second trip must re-raise the advisory un-acked; last: {reraised}"
            )
        finally:
            clear_errors(emulator_url)
