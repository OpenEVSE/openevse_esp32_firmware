#!/usr/bin/env python3
"""Check that every relative link and image in the documentation resolves.

Scans AGENTS.md, readme.md, and docs/**/*.md (excluding docs/superpowers).
External http(s) links are not fetched. Exits 1 on any broken target.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)\)|<img src=\"([^\"]+)\"")


def md_files():
    for base in [ROOT / "AGENTS.md", ROOT / "readme.md"]:
        if base.exists():
            yield base
    for md in (ROOT / "docs").rglob("*.md"):
        if "superpowers" not in md.parts:
            yield md


def main() -> int:
    broken = 0
    for md in md_files():
        text = md.read_text(encoding="utf-8", errors="replace")
        for m in LINK_RE.finditer(text):
            href = m.group(1) or m.group(2)
            if href.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path = href.split("#")[0]
            if path and not (md.parent / path).resolve().exists():
                print(f"{md.relative_to(ROOT)}: broken link -> {href}")
                broken += 1
    print(f"{broken} broken link(s)" if broken else "all relative links OK")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
