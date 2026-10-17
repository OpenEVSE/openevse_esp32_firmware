#!/usr/bin/env python3
"""End-to-end test coverage report.

Every web UI route listed in docs/ai/feature-map.md should have at least one
Gherkin scenario in tests/e2e/features/ tagged ``@route:<path>`` (the feature
tag covers all its scenarios). Routes that are not covered yet are listed in
tests/e2e/coverage-exemptions.txt; the list may only shrink: a new route fails
the check unless it has a scenario, an exempted route that gains a scenario
must be removed from the list, and with --base a route may not be added to the
list compared with the base revision.

Usage:
    python scripts/e2e_coverage.py                          # report, always exit 0
    python scripts/e2e_coverage.py --strict                 # exit 1 on any gap (CI)
    python scripts/e2e_coverage.py --strict --base REF      # also reject exemptions added since REF
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FEATURE_MAP = ROOT / "docs" / "ai" / "feature-map.md"
FEATURES = ROOT / "tests" / "e2e" / "features"
EXEMPTIONS = ROOT / "tests" / "e2e" / "coverage-exemptions.txt"

UI_ROUTE_COLUMN = 2  # Feature | Firmware source | UI route | ...


def mapped_routes():
    routes = set()
    for line in FEATURE_MAP.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|") or line.startswith("|---"):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) <= UI_ROUTE_COLUMN or cells[0] == "Feature":
            continue
        routes.update(re.findall(r"`(/[^`\s]*)`", cells[UI_ROUTE_COLUMN]))
    return routes


def tested_routes():
    routes = set()
    for feature in FEATURES.rglob("*.feature"):
        for line in feature.read_text(encoding="utf-8").splitlines():
            # Only tag lines count: a comment mentioning a tag is not coverage.
            if line.lstrip().startswith("@"):
                routes.update(re.findall(r"@route:(/\S*)", line))
    return routes


def parse_exemptions(text):
    lines = (l.split("#")[0].strip() for l in text.splitlines())
    return {l for l in lines if l}


def exempted_routes():
    if not EXEMPTIONS.exists():
        return set()
    return parse_exemptions(EXEMPTIONS.read_text(encoding="utf-8"))


def base_exempted_routes(ref):
    """Exemptions at ref, or None when the list does not exist there yet."""
    relative = EXEMPTIONS.relative_to(ROOT).as_posix()
    listed = subprocess.run(
        ["git", "ls-tree", "--name-only", ref, "--", relative],
        cwd=ROOT, capture_output=True, text=True, check=True,
    ).stdout.strip()
    if not listed:
        return None
    shown = subprocess.run(
        ["git", "show", f"{ref}:{relative}"],
        cwd=ROOT, capture_output=True, text=True, check=True,
    )
    return parse_exemptions(shown.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--strict", action="store_true", help="exit 1 on any gap")
    parser.add_argument("--base", metavar="REF",
                        help="git revision whose exemption list new entries are compared against")
    args = parser.parse_args()

    mapped, tested, exempt = mapped_routes(), tested_routes(), exempted_routes()

    uncovered = sorted(mapped - tested - exempt)
    stale = sorted(exempt & tested)
    unknown_tags = sorted(tested - mapped)
    unknown_exempt = sorted(exempt - mapped)
    base_exempt = base_exempted_routes(args.base) if args.base else None
    # A base without the list (the PR that introduces it) has nothing to compare.
    added = sorted(exempt - base_exempt) if base_exempt is not None else []

    print(f"UI routes in feature map: {len(mapped)}")
    print(f"  with e2e scenarios:     {len(mapped & tested)}")
    print(f"  exempted (not yet):     {len((mapped & exempt) - tested)}")

    problems = 0
    for title, items, hint in [
        ("UI routes with no e2e scenario", uncovered,
         "add a feature in tests/e2e/features/ tagged @route:<path> (see AGENTS.md)"),
        ("Routes exempted but now covered", stale,
         "remove them from tests/e2e/coverage-exemptions.txt"),
        ("Routes added to the exemption list", added,
         "the list may only shrink; add a feature tagged @route:<path> instead"),
        ("@route tags not in docs/ai/feature-map.md", unknown_tags,
         "fix the tag or add the feature-map row"),
        ("Exempted routes not in docs/ai/feature-map.md", unknown_exempt,
         "remove them from tests/e2e/coverage-exemptions.txt"),
    ]:
        if items:
            problems += len(items)
            print(f"\n{title} ({len(items)}) — {hint}:")
            for item in items:
                print(f"  {item}")

    return 1 if (problems and args.strict) else 0


if __name__ == "__main__":
    sys.exit(main())
