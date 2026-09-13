#!/usr/bin/env python3
"""
Check a build's flash usage against the app partition budget, and record the
result for the flash-size PR report.

The used/max byte counts are parsed straight out of PlatformIO's own build
output (the "Flash: [====] NN.N% (used X bytes from Y bytes)" line it prints
after linking) rather than recomputed from firmware.bin and the partition
table -- that's the number PlatformIO itself already relies on to decide
whether a build fits, so this stays in lockstep with it by construction.

Known limitation: this line is an ELF-section total. It does not include any
padding esptool's elf2image step adds when writing the final firmware.bin,
so in principle a build could report fine here while its on-disk firmware.bin
is somewhat larger. Measuring that gap conclusively (and whether it matters
for OTA) needs inspecting the actual built artifact, which wasn't practical
to pin down here -- see the discussion on the PR that introduced this script.

Exit status:
    0  the env is exempt (native, or its build didn't succeed) and has no
       "Flash:" line to check
    0  the env is exempt but its log has a "Flash:" line anyway (e.g. linking
       succeeded before a later step failed) -- skipped, not recorded
    0  usage is below the warning threshold
    0  usage is within the warning threshold (a ::warning:: is emitted)
    1  a non-exempt build succeeded but printed no "Flash:" line -- something
       unexpected happened (e.g. a PlatformIO output format change) and this
       build is otherwise invisible to the flash-size report (a ::error:: is
       emitted)
    1  usage exceeds the partition size (a ::error:: is emitted)

Usage:
    python check_flash_size.py --env NAME --log pio-build.log --out flash-size-NAME.json \
        [--build-outcome success|failure|...] [--native]
"""
import argparse
import json
import re
import sys

WARN_THRESHOLD_PCT = 95.0

FLASH_LINE_RE = re.compile(
    r"^Flash:.*?([\d.]+)%\s*\(used (\d+) bytes from (\d+) bytes\)", re.MULTILINE
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", required=True, help="PlatformIO environment name")
    parser.add_argument("--log", required=True, help="Captured `pio run` output")
    parser.add_argument("--out", required=True, help="Path to write the JSON size record")
    parser.add_argument("--build-outcome", default="success",
                         help="Outcome of the 'Run PlatformIO' step (e.g. success/failure)")
    parser.add_argument("--native", action="store_true",
                         help="This env doesn't produce a flashable image (e.g. native_openevse)")
    args = parser.parse_args()
    exempt = args.native or args.build_outcome != "success"

    try:
        with open(args.log, errors="replace") as f:
            log = f.read()
    except FileNotFoundError:
        print(f"{args.env}: no build log at {args.log} -- skipping the flash size check")
        return 0

    match = FLASH_LINE_RE.search(log)
    if not match:
        if not exempt:
            print(f"::error::{args.env}: build succeeded but no 'Flash:' usage line was found "
                  "in its output -- this build is invisible to the flash-size check and report; "
                  "investigate the build log directly")
            return 1
        print(f"{args.env}: no 'Flash:' usage line in the PlatformIO build output "
              "-- skipping the flash size check (native env, or the build didn't succeed)")
        return 0

    if exempt:
        print(f"{args.env}: found a 'Flash:' usage line, but skipping it -- native env, "
              "or the build didn't succeed, so this isn't a deliverable firmware image")
        return 0

    percent, size, max_size = float(match.group(1)), int(match.group(2)), int(match.group(3))

    record = {
        "env": args.env,
        "size": size,
        "max_size": max_size,
        "percent": percent,
    }
    with open(args.out, "w") as f:
        json.dump(record, f, indent=2)

    print(f"{args.env}: {size} / {max_size} bytes ({percent:.1f}%) -- from PlatformIO's own report")

    if size > max_size:
        over = size - max_size
        print(f"::error::{args.env}: firmware ({size} bytes) exceeds the app "
              f"partition ({max_size} bytes) by {over} bytes ({percent:.1f}% full)")
        return 1

    if percent >= WARN_THRESHOLD_PCT:
        print(f"::warning::{args.env}: firmware is at {percent:.1f}% of the app "
              f"partition ({size}/{max_size} bytes) -- within "
              f"{100 - WARN_THRESHOLD_PCT:.0f}% of the flash limit")

    return 0


if __name__ == "__main__":
    sys.exit(main())
