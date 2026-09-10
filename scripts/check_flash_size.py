#!/usr/bin/env python3
"""
Check a build's flash usage against the app partition budget, and record the
result for the flash-size PR report.

The size that matters is the byte count of the file actually written to the
device during OTA: Update.begin(total) (src/http_update.cpp) uses the HTTP
Content-Length of the served image -- the full firmware.bin (or signed
variant) on disk, esptool padding and any OTA signature trailer included --
to reject an oversized binary before erasing the target partition.
PlatformIO's own "Flash: NN.N% (used X bytes from Y bytes)" line, printed
right after linking, is an ELF-section total instead: it does not include
the padding esptool's elf2image step adds to cover gaps between
non-contiguous ELF sections (which can be substantial -- over 100KB on some
chips, e.g. RISC-V esp32-c3). An image whose sections fit under budget can
still produce a firmware.bin that overflows the partition, so measuring the
real file -- as this script does -- is the only way to catch that in CI
instead of as a failed OTA update in the field.

The partition budget (max_size) is still read from that same "Flash:" line
rather than re-derived from the partition table, so this can't disagree with
PlatformIO about how big the partition is -- only about whether the actual
artifact fits in it.

Exit status:
    0  no "Flash:" line found (nothing to check -- e.g. a native env, or the
       build failed before reaching the size-check step)
    0  usage is below the warning threshold
    0  usage is within the warning threshold (a ::warning:: is emitted)
    1  usage exceeds the partition size, or firmware.bin is unexpectedly
       missing (a ::error:: is emitted)

Usage:
    python check_flash_size.py --env NAME --bin firmware.bin --log pio-build.log \
        --out flash-size-NAME.json
"""
import argparse
import json
import os
import re
import sys

WARN_THRESHOLD_PCT = 95.0

FLASH_LINE_RE = re.compile(
    r"^Flash:.*?([\d.]+)%\s*\(used (\d+) bytes from (\d+) bytes\)", re.MULTILINE
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", required=True, help="PlatformIO environment name")
    parser.add_argument("--bin", required=True, help="Path to the final firmware.bin (post-signing)")
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

    pio_percent, pio_size, max_size = float(match.group(1)), int(match.group(2)), int(match.group(3))

    if not os.path.exists(args.bin):
        if pio_size > max_size:
            # PlatformIO's own link-time check already aborted the build before
            # elf2image could run, so there's no firmware.bin -- but its
            # ELF-section total alone was already over budget, so the real
            # (larger, post-padding) image certainly would be too.
            print(f"::error::{args.env}: PlatformIO's own size check already reports "
                  f"{pio_size} bytes (ELF sections only, before esptool padding) over the "
                  f"app partition ({max_size} bytes); firmware.bin was never built")
            return 1
        print(f"::error::{args.env}: {args.bin} not found, but PlatformIO's own report "
              f"({pio_size}/{max_size} bytes, under budget) shows the build should have "
              "produced one -- investigate the CI step ordering rather than trusting this "
              "check silently")
        return 1

    size = os.path.getsize(args.bin)
    percent = (size / max_size) * 100

    record = {
        "env": args.env,
        "size": size,
        "max_size": max_size,
        "percent": round(percent, 2),
    }
    with open(args.out, "w") as f:
        json.dump(record, f, indent=2)

    print(f"{args.env}: {size} / {max_size} bytes ({percent:.1f}%) -- deployable firmware.bin "
          f"(PlatformIO's own ELF-section report was {pio_size} bytes, {pio_percent:.1f}%)")

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
