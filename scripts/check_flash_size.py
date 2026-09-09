#!/usr/bin/env python3
"""
Check a build's flash usage against the app partition budget, and record the
result for the flash-size PR report.

The used/max byte counts are parsed straight out of PlatformIO's own build
output (the "Flash: [====] NN.N% (used X bytes from Y bytes)" line it prints
after linking), rather than recomputed from firmware.bin and the partition
table. firmware.bin is bigger than the code+data PlatformIO reports: esptool's
elf2image padding to cover gaps between non-contiguous ELF sections (worse on
some chips, e.g. RISC-V esp32-c3, than others) can add well over a hundred KB
that was never going to consume flash, which made a from-scratch recomputation
disagree with -- and be less correct than -- the number PlatformIO already
printed. Reading that line keeps this script in lockstep with what PlatformIO
itself reports and enforces at link time by construction.

Exit status:
    0  no "Flash:" line found (nothing to check -- e.g. a native env, or the
       build failed before reaching the size-check step)
    0  usage is below the warning threshold
    0  usage is within the warning threshold (a ::warning:: is emitted)
    1  usage exceeds the partition size (a ::error:: is emitted)

Usage:
    python check_flash_size.py --env NAME --log pio-build.log --out flash-size-NAME.json
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
    args = parser.parse_args()

    try:
        with open(args.log, errors="replace") as f:
            log = f.read()
    except FileNotFoundError:
        print(f"{args.env}: no build log at {args.log} -- skipping the flash size check")
        return 0

    match = FLASH_LINE_RE.search(log)
    if not match:
        print(f"{args.env}: no 'Flash:' usage line in the PlatformIO build output "
              "-- skipping the flash size check (native env, or the build didn't reach linking)")
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
