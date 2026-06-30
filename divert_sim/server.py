#!/usr/bin/env python3
"""HTTP server for interactive scenario execution and viewer assets."""

from __future__ import annotations

import http.server
import json
import os
import socketserver
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from run_simulations import build_index


ROOT = Path(__file__).resolve().parent
PORT = 8000
SCENARIO_DIR = ROOT / "data" / "scenarios"
DATA_DIR = ROOT / "data"

# Always serve files relative to divert_sim/, regardless of where server.py is launched.
os.chdir(ROOT)


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


def _json_response(handler: http.server.BaseHTTPRequestHandler, code: int, body: dict) -> None:
    payload = json.dumps(body).encode("utf-8")
    handler.send_response(code)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(payload)))
    handler.end_headers()
    handler.wfile.write(payload)


def _resolve_under_root(relative_path: str) -> Path:
    candidate = (ROOT / relative_path).resolve()
    if ROOT != candidate and ROOT not in candidate.parents:
        raise ValueError(f"Path escapes workspace root: {relative_path}")
    return candidate


def _scenario_entries() -> list[dict]:
    entries: list[dict] = []
    for path in sorted(SCENARIO_DIR.glob("*.json")):
        try:
            with path.open() as f:
                doc = json.load(f)
        except Exception:
            continue
        meta = doc.get("meta") if isinstance(doc, dict) else {}
        scenario_id = meta.get("id") or path.stem
        title = meta.get("title") or scenario_id
        category = meta.get("category") or "misc"
        profile = meta.get("profile") or "default"
        entries.append(
            {
                "name": path.name,
                "source": str(path.relative_to(ROOT)),
                "id": scenario_id,
                "title": title,
                "category": category,
                "profile": profile,
            }
        )
    return entries


def _feed_entries() -> list[str]:
    feeds: list[str] = []
    for path in sorted(DATA_DIR.glob("*.csv")):
        feeds.append(os.path.relpath(path, SCENARIO_DIR).replace("\\", "/"))
    return feeds


def _config_entries() -> list[str]:
    configs: list[str] = []
    for path in sorted(DATA_DIR.glob("config-*.json")):
        configs.append(os.path.relpath(path, SCENARIO_DIR).replace("\\", "/"))
    return configs


class SimRequestHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path == "/api/editor/options":
            _json_response(
                self,
                200,
                {
                    "scenarios": _scenario_entries(),
                    "feeds": _feed_entries(),
                    "config_includes": _config_entries(),
                },
            )
            return

        if parsed.path == "/api/scenario":
            query = parse_qs(parsed.query)
            source = (query.get("source") or [""])[0]
            if not source:
                _json_response(self, 400, {"error": "Missing source query parameter"})
                return
            try:
                path = _resolve_under_root(source)
                with path.open() as f:
                    doc = json.load(f)
                _json_response(
                    self,
                    200,
                    {
                        "source": str(path.relative_to(ROOT)),
                        "name": path.name,
                        "scenario": doc,
                    },
                )
            except Exception as err:
                _json_response(self, 400, {"error": str(err)})
            return

        if self.path == "/":
            self.path = "/view.html"
        return http.server.SimpleHTTPRequestHandler.do_GET(self)

    def do_POST(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)

        if parsed.path == "/api/scenario/save":
            length = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(length)
            try:
                payload = json.loads(raw.decode("utf-8")) if raw else {}
            except ValueError:
                payload = {}

            scenario_doc = payload.get("scenario") if isinstance(payload, dict) else None
            if not isinstance(scenario_doc, dict):
                _json_response(self, 400, {"error": "Missing scenario object"})
                return

            requested_name = payload.get("name") if isinstance(payload, dict) else None
            if isinstance(requested_name, str) and requested_name.strip():
                filename = requested_name.strip()
                if not filename.endswith(".json"):
                    filename += ".json"
            else:
                filename = "interactive_generated.json"

            save_path = (SCENARIO_DIR / filename).resolve()
            if SCENARIO_DIR != save_path.parent:
                _json_response(self, 400, {"error": "Scenario must be saved in data/scenarios"})
                return

            with save_path.open("w") as f:
                json.dump(scenario_doc, f, indent=2)

            _json_response(
                self,
                200,
                {
                    "name": save_path.name,
                    "source": str(save_path.relative_to(ROOT)),
                },
            )
            return

        if parsed.path not in ("/simulation", "/simulation/"):
            self.send_error(404, "Unknown endpoint")
            return

        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)

        overrides = {}
        scenario_ids = None
        scenario_sources = None
        scenario_doc = None
        scenario_save_name = "interactive_generated.json"
        try:
            payload = json.loads(raw.decode("utf-8")) if raw else {}
            if isinstance(payload, dict):
                if "overrides" in payload:
                    candidate_overrides = payload.get("overrides")
                    if isinstance(candidate_overrides, dict):
                        overrides = candidate_overrides
                else:
                    # Backward compatibility: legacy interactive endpoint posted raw overrides.
                    known_keys = {
                        "scenario_ids",
                        "scenario_sources",
                        "scenario_doc",
                        "scenario_name",
                    }
                    if not any(key in payload for key in known_keys):
                        overrides = payload

                candidate_scenarios = payload.get("scenario_ids")
                if isinstance(candidate_scenarios, list):
                    scenario_ids = [str(s) for s in candidate_scenarios if s]

                candidate_sources = payload.get("scenario_sources")
                if isinstance(candidate_sources, list):
                    scenario_sources = [str(s) for s in candidate_sources if s]

                if isinstance(payload.get("scenario_doc"), dict):
                    scenario_doc = payload.get("scenario_doc")

                if isinstance(payload.get("scenario_name"), str) and payload.get("scenario_name").strip():
                    scenario_save_name = payload.get("scenario_name").strip()
        except ValueError:
            overrides = {}

        if scenario_doc is not None:
            if not scenario_save_name.endswith(".json"):
                scenario_save_name += ".json"
            scenario_save_path = (SCENARIO_DIR / scenario_save_name).resolve()
            if SCENARIO_DIR != scenario_save_path.parent:
                _json_response(self, 400, {"error": "Scenario must be saved in data/scenarios"})
                return
            with scenario_save_path.open("w") as f:
                json.dump(scenario_doc, f, indent=2)
            scenario_sources = [str(scenario_save_path.relative_to(ROOT))]

        index = build_index(
            config_overrides=overrides,
            profile_suffix="interactive",
            index_name="interactive.json",
            scenario_ids=scenario_ids,
            scenario_sources=scenario_sources,
        )

        _json_response(self, 200, index)


if __name__ == "__main__":
    with ReusableTCPServer(("", PORT), SimRequestHandler) as server:
        print(f"Server started at http://localhost:{PORT}")
        server.serve_forever()
