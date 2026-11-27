"""Integration tests for the scheduler HTTP API (#1403).

- GET /schedule/plan must reach the plan handler. ArduinoMongoose matches the
  most recently registered route first, and "/schedule/#" used to be
  registered after it, so the plan was answered as event "plan" (id 0 = the
  whole event list) and the GUI's Active badge never had a current event.
  "/logs/export" had the same shadowing behind "/logs/#".
- Each event carries an ``enabled`` flag. A disabled event is kept and saved
  but left out of the weekly plan.
"""

import pytest
import requests

REQUEST_TIMEOUT = 5
EVERY_DAY = ["sunday", "monday", "tuesday", "wednesday",
             "thursday", "friday", "saturday"]
EVENT_ID = 41


def url(evse, path):
    return f"{evse['native_url']}{path}"


def post_event(evse, event):
    r = requests.post(url(evse, f"/schedule/{event['id']}"), json=event,
                      timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"POST /schedule/{event['id']}: {r.status_code}: {r.text}"


def get_event(evse, event_id):
    r = requests.get(url(evse, f"/schedule/{event_id}"), timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"GET /schedule/{event_id}: {r.status_code}: {r.text}"
    return r.json()


def plan_event_ids(evse):
    """Ids of every event in the weekly plan (all days)."""
    r = requests.get(url(evse, "/schedule/plan"), timeout=REQUEST_TIMEOUT)
    assert r.status_code == 200, f"/schedule/plan: {r.status_code}: {r.text}"
    plan = r.json()
    ids = set()
    # serializePlan writes one array of event instances per weekday name.
    for day in EVERY_DAY:
        for e in plan.get(day, []) or []:
            ids.add(e.get("id"))
    for key in ("current_event", "next_event"):
        if isinstance(plan.get(key), dict):
            ids.add(plan[key].get("id"))
    return ids


@pytest.mark.timeout(120)
class TestScheduleRoutes:
    def test_plan_route_returns_the_plan(self, evse_instance):
        r = requests.get(url(evse_instance, "/schedule/plan"), timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200, r.text
        plan = r.json()
        assert isinstance(plan, dict), f"/schedule/plan returned {type(plan).__name__}: {r.text[:200]}"
        assert "current_day" in plan and "current_event" in plan, r.text[:200]

    def test_event_id_route_still_works(self, evse_instance):
        post_event(evse_instance, {"id": EVENT_ID, "state": "active",
                                   "time": "23:00:00", "days": EVERY_DAY})
        try:
            assert get_event(evse_instance, EVENT_ID)["time"] == "23:00:00"
        finally:
            requests.delete(url(evse_instance, f"/schedule/{EVENT_ID}"), timeout=REQUEST_TIMEOUT)

    def test_logs_export_is_not_shadowed(self, evse_instance):
        r = requests.get(url(evse_instance, "/logs/export"), timeout=REQUEST_TIMEOUT)
        # Answered by "/logs/#" as page "export" this was a JSON 404. Require
        # the CSV export itself, not just "not that 404".
        assert r.status_code == 200, f"{r.status_code}: {r.text[:200]}"
        assert "session_history.csv" in r.headers.get("Content-Disposition", ""), r.headers
        assert r.text.startswith("Time,Type,State,"), r.text[:200]


# Each test gets its own emulator + firmware pair (evse_instance), so the
# event needs no cleanup: a teardown_method would run after the fixture has
# already stopped the firmware.
@pytest.mark.timeout(120)
class TestScheduleEnabled:
    def test_enabled_defaults_true_and_is_reported(self, evse_instance):
        post_event(evse_instance, {"id": EVENT_ID, "state": "active",
                                   "time": "23:00:00", "days": EVERY_DAY})
        assert get_event(evse_instance, EVENT_ID).get("enabled") is True

    def test_disabled_event_kept_but_left_out_of_plan(self, evse_instance):
        event = {"id": EVENT_ID, "state": "active", "time": "23:00:00",
                 "days": EVERY_DAY, "feature": "current", "feature_value": 20}
        post_event(evse_instance, event)
        assert EVENT_ID in plan_event_ids(evse_instance)

        post_event(evse_instance, {**event, "enabled": False})
        stored = get_event(evse_instance, EVENT_ID)
        assert stored.get("enabled") is False
        assert stored.get("feature_value") == 20, "disabling must keep the rule's settings"
        assert EVENT_ID not in plan_event_ids(evse_instance)

        post_event(evse_instance, {**event, "enabled": True})
        assert get_event(evse_instance, EVENT_ID).get("enabled") is True
        assert EVENT_ID in plan_event_ids(evse_instance)
