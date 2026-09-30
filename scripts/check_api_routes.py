#!/usr/bin/env python3
"""Fail when a route registered in src/web_server*.cpp has no path in api.yml.

Routes come from `server.on("<path>"...)`. The `$` end anchor is dropped, and a
trailing `/` (prefix route, e.g. "/loadsharing/peers/") is matched against any
api.yml path that starts with it (e.g. "/loadsharing/peers/{host}").
Routes that are intentionally undocumented go in EXCLUDED with the reason.

    python scripts/check_api_routes.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

EXCLUDED = {
    "/r": "alias of /rapi",
}


def source_routes():
    routes = {}
    for f in sorted((ROOT / "src").glob("web_server*.cpp")):
        for m in re.finditer(r'server\.on\(\s*"([^"]+)"', f.read_text(errors="replace")):
            routes.setdefault(m.group(1).rstrip("$"), f.name)
    return routes


def spec_paths():
    api = (ROOT / "api.yml").read_text(errors="replace")
    return set(re.findall(r"^  ['\"]?(/[^\s:'\"]*)['\"]?:", api, re.M))


def main():
    spec = spec_paths()
    missing = []
    for route, src in source_routes().items():
        if route in EXCLUDED:
            continue
        if route.endswith("/"):
            ok = any(p.startswith(route) for p in spec)
        else:
            ok = route in spec
        if not ok:
            missing.append(f"{route}  ({src})")
    if missing:
        print("Routes registered in src/ but missing from api.yml "
              "(document them or add to EXCLUDED with a reason):")
        for m in missing:
            print("  " + m)
        return 1
    print("All registered routes are documented in api.yml")
    return 0


if __name__ == "__main__":
    sys.exit(main())
