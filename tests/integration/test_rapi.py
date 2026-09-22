"""Integration tests for /r, the raw RAPI passthrough.

Runs against the paired emulator + native firmware fixture from conftest.py.
The handler used to call rapiSender.sendCmdSync() on the server task, which
spun without polling Mongoose or feeding the watchdog; it now goes through the
async queue and defers the HTTP response to the completion callback, the same
way /relay/reset does. These tests pin down the contract that survives that:
the reply shape, the CSRF gate, and that a caller-supplied command cannot
break the JSON it is echoed back in.
"""

import json

import requests

REQUEST_TIMEOUT = 15
HEADERS = {"X-Requested-With": "OpenEVSE"}


class TestRapiPassthrough:
    def test_headerless_get_is_403(self, evse_instance):
        """A cross-site GET must not reach the controller (actuatorMethodAllowed)."""
        native_url = evse_instance["native_url"]
        r = requests.get(f"{native_url}/r", params={"json": 1, "rapi": "$GV"},
                         timeout=REQUEST_TIMEOUT)
        assert r.status_code == 403, f"Expected 403, got {r.status_code}: {r.text}"

    def test_page_without_command_needs_no_header(self, evse_instance):
        """The bare form page changes nothing, so it is not gated."""
        native_url = evse_instance["native_url"]
        r = requests.get(f"{native_url}/r", timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200
        assert "<form method='post'" in r.text

    def test_json_reply_shape(self, evse_instance):
        """GET with the app header: {"cmd": ..., "ret": "$OK ..."}."""
        native_url = evse_instance["native_url"]
        r = requests.get(f"{native_url}/r", params={"json": 1, "rapi": "$GV"},
                         headers=HEADERS, timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200, f"{r.status_code}: {r.text}"
        body = r.json()
        assert body["cmd"] == "$GV"
        assert body["ret"].startswith("$OK"), body

    def test_post_form_is_accepted(self, evse_instance):
        """The built-in HTML form posts; a POST needs no header."""
        native_url = evse_instance["native_url"]
        r = requests.post(f"{native_url}/r", data={"rapi": "$GV"},
                          timeout=REQUEST_TIMEOUT)
        assert r.status_code == 200, f"{r.status_code}: {r.text}"
        assert "$GV" in r.text and "$OK" in r.text

    def test_command_with_quote_is_still_valid_json(self, evse_instance):
        """The command is echoed back inside JSON; a quote in it must be escaped."""
        native_url = evse_instance["native_url"]
        cmd = '$GV"x'
        r = requests.get(f"{native_url}/r", params={"json": 1, "rapi": cmd},
                         headers=HEADERS, timeout=REQUEST_TIMEOUT)
        # Whatever the controller makes of it, the reply must parse.
        body = json.loads(r.text)
        assert body["cmd"] == cmd
        assert ("ret" in body) != ("error" in body)
