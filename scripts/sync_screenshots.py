#!/usr/bin/env python3
"""Sync generated UI screenshots from the gui-nightshift submodule into the
user documentation.

The screenshots are generated inside the submodule (`npm run screenshots`,
see gui-nightshift/scripts/screenshots.config.js) but GitHub does not render
submodule paths from the parent repo, so the user guide embeds copies under
docs/user/screenshots/. This script keeps that copy exact: it mirrors
gui-nightshift/docs/screenshots/*.png (adds, updates, deletes).

Usage:
    python scripts/sync_screenshots.py           # sync
    python scripts/sync_screenshots.py --check   # exit 1 if out of sync (CI)
"""

import filecmp
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "gui-nightshift" / "docs" / "screenshots"
DST = ROOT / "docs" / "user" / "screenshots"


def main() -> int:
    check = "--check" in sys.argv
    if not SRC.is_dir():
        print(f"source not found: {SRC} (is the submodule initialised?)")
        return 1

    src_files = {p.name for p in SRC.glob("*.png")}
    DST.mkdir(parents=True, exist_ok=True)
    dst_files = {p.name for p in DST.glob("*.png")}

    stale = sorted(dst_files - src_files)
    missing = sorted(src_files - dst_files)
    changed = sorted(
        name
        for name in src_files & dst_files
        if not filecmp.cmp(SRC / name, DST / name, shallow=False)
    )

    if check:
        if stale or missing or changed:
            for name in missing:
                print(f"missing: {name}")
            for name in changed:
                print(f"differs: {name}")
            for name in stale:
                print(f"stale:   {name}")
            print("\ndocs/user/screenshots is out of sync — run: python scripts/sync_screenshots.py")
            return 1
        print("docs/user/screenshots is in sync")
        return 0

    for name in stale:
        (DST / name).unlink()
        print(f"removed {name}")
    for name in missing + changed:
        shutil.copyfile(SRC / name, DST / name)
        print(f"copied  {name}")
    if not (stale or missing or changed):
        print("already in sync")
    return 0


if __name__ == "__main__":
    sys.exit(main())
