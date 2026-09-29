#!/usr/bin/env python3
"""Documentation coverage report.

Extracts the feature inventory from the source of truth in code and checks
each item is mentioned somewhere in the documentation:

- config options   -> ConfigOptDefinition entries in src/app_config.cpp
- web UI routes    -> gui-nightshift/src/lib/routes.js + src/lib/config/pages.js
- HTTP endpoints   -> top-level paths in api.yml

An item counts as documented if it appears verbatim in any markdown file under
docs/ (or AGENTS.md/readme.md), or is covered by a wildcard family reference
like `mqtt_*` (used by docs/ai/feature-map.md).

Usage:
    python scripts/docs_coverage.py             # report, always exit 0
    python scripts/docs_coverage.py --strict    # exit 1 if anything is missing
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Internal plumbing, not user-facing features.
IGNORED_OPTIONS = {"flags", "flags_changed"}


def extract_config_options():
    src = (ROOT / "src" / "app_config.cpp").read_text(encoding="utf-8", errors="replace")
    opts = set(re.findall(
        r'ConfigOptDefinition<[^>]+>\s*\([^,]+,[^,]+(?:\([^)]*\))?[^,]*,\s*"([^"]+)"', src))
    return sorted(opts - IGNORED_OPTIONS)


def extract_ui_routes():
    routes = set()
    routes_js = ROOT / "gui-nightshift" / "src" / "lib" / "routes.js"
    pages_js = ROOT / "gui-nightshift" / "src" / "lib" / "config" / "pages.js"
    if routes_js.exists():
        text = routes_js.read_text(encoding="utf-8")
        # Legacy redirect entries are aliases, not screens — don't require docs.
        text = text.split("LEGACY_ROUTES")[0]
        routes.update(re.findall(r"^\s*'(/[^']*)':", text, re.M))
        routes.update(re.findall(r"routes\['(/[^']+)'\]", text))
    if pages_js.exists():
        text = pages_js.read_text(encoding="utf-8")
        routes.update(re.findall(r"route:\s*'(/[^']+)'", text))
    return sorted(routes)


def extract_api_paths():
    api = (ROOT / "api.yml").read_text(encoding="utf-8", errors="replace")
    return sorted(set(re.findall(r"^  (/[a-z][^:\s]*):", api, re.M)))


def load_docs_corpus():
    parts = []
    for base in [ROOT / "AGENTS.md", ROOT / "readme.md"]:
        if base.exists():
            parts.append(base.read_text(encoding="utf-8", errors="replace"))
    for md in (ROOT / "docs").rglob("*.md"):
        if "superpowers" in md.parts:
            continue
        parts.append(md.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def main() -> int:
    strict = "--strict" in sys.argv
    corpus = load_docs_corpus()
    # Wildcard family references like `mqtt_*` cover every option with that prefix.
    families = set(re.findall(r"`(\w+)_\*`", corpus))

    sections = {
        "config options": (
            extract_config_options(),
            lambda o: o in corpus or any(o.startswith(f + "_") for f in families),
        ),
        "UI routes": (extract_ui_routes(), lambda r: r in corpus),
        "HTTP API paths": (extract_api_paths(), lambda p: f"`{p}`" in corpus or f"{p}:" in corpus or f"({p})" in corpus or f" {p} " in corpus),
    }

    missing_total = 0
    for name, (items, is_covered) in sections.items():
        missing = [i for i in items if not is_covered(i)]
        covered = len(items) - len(missing)
        print(f"{name}: {covered}/{len(items)} documented")
        for i in missing:
            print(f"  MISSING: {i}")
        missing_total += len(missing)

    if missing_total:
        print(f"\n{missing_total} item(s) lack documentation coverage.")
        print("Add them to a docs/user or docs/developer page and, for features,")
        print("a row in docs/ai/feature-map.md.")
    else:
        print("\nFull documentation coverage.")
    return 1 if (strict and missing_total) else 0


if __name__ == "__main__":
    sys.exit(main())
