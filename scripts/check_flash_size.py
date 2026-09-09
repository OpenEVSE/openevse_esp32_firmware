#!/usr/bin/env python3
"""
Check a built firmware image against the flash budget of the app partition
it will be flashed into, and record the result for the flash-size PR report.

The app partition size is read from the *compiled* partitions.bin PlatformIO
produces alongside firmware.bin, rather than re-parsing the source CSV, so
this always matches whatever partition table the env actually built with
(no need to know which of the CSVs in platformio.ini a given env selected).

Exit status:
    0  usage is below the warning threshold
    0  usage is within the warning threshold (a ::warning:: is emitted)
    1  usage exceeds the partition size (a ::error:: is emitted)

Usage:
    python check_flash_size.py --env NAME --bin firmware.bin \
        --partitions partitions.bin --out flash-size-NAME.json
"""
import argparse
import json
import struct
import sys

PARTITION_ENTRY_SIZE = 32
PARTITION_MAGIC = b"\xaa\x50"
PARTITION_END_MARKER = b"\xff" * PARTITION_ENTRY_SIZE
TYPE_APP = 0x00

WARN_THRESHOLD_PCT = 95.0


def app_partition_size(partitions_bin):
    with open(partitions_bin, "rb") as f:
        data = f.read()

    for offset in range(0, len(data), PARTITION_ENTRY_SIZE):
        entry = data[offset:offset + PARTITION_ENTRY_SIZE]
        if len(entry) < PARTITION_ENTRY_SIZE or entry == PARTITION_END_MARKER:
            break
        if entry[0:2] != PARTITION_MAGIC:
            continue
        ptype = entry[2]
        if ptype != TYPE_APP:
            continue
        size = struct.unpack_from("<I", entry, 8)[0]
        # Multiple app slots (ota_0/ota_1) are always sized identically in
        # this repo's partition tables, so the first one found is the budget.
        return size

    sys.exit(f"error: no app partition found in {partitions_bin}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--env", required=True, help="PlatformIO environment name")
    parser.add_argument("--bin", required=True, help="Path to the built firmware.bin")
    parser.add_argument("--partitions", required=True, help="Path to the built partitions.bin")
    parser.add_argument("--out", required=True, help="Path to write the JSON size record")
    args = parser.parse_args()

    with open(args.bin, "rb") as f:
        size = len(f.read())

    max_size = app_partition_size(args.partitions)
    percent = (size / max_size) * 100

    record = {
        "env": args.env,
        "size": size,
        "max_size": max_size,
        "percent": round(percent, 2),
    }
    with open(args.out, "w") as f:
        json.dump(record, f, indent=2)

    print(f"{args.env}: {size} / {max_size} bytes ({percent:.1f}%)")

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
