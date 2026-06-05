#!/usr/bin/env python3
"""HTTP server for interactive scenario execution and viewer assets."""

from __future__ import annotations

import http.server
import json
import os
import socketserver
from pathlib import Path

from run_simulations import build_index


ROOT = Path(__file__).resolve().parent
PORT = 8000

# Always serve files relative to divert_sim/, regardless of where server.py is launched.
os.chdir(ROOT)


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


class SimRequestHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/":
            self.path = "/view.html"
        return http.server.SimpleHTTPRequestHandler.do_GET(self)

    def do_POST(self) -> None:  # noqa: N802
        if self.path not in ("/simulation", "/simulation/"):
            self.send_error(404, "Unknown endpoint")
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)

        try:
            overrides = json.loads(raw.decode("utf-8")) if raw else {}
            if not isinstance(overrides, dict):
                overrides = {}
        except ValueError:
            overrides = {}

        index = build_index(
            config_overrides=overrides,
            profile_suffix="interactive",
            index_name="interactive.json",
        )

        payload = json.dumps(index).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


if __name__ == "__main__":
    with ReusableTCPServer(("", PORT), SimRequestHandler) as server:
        print(f"Server started at http://localhost:{PORT}")
        server.serve_forever()
