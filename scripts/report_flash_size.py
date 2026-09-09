#!/usr/bin/env python3
"""
Build a Markdown flash-size report from the per-env JSON records written by
check_flash_size.py, comparing the current build against a baseline (usually
the base branch's most recent build) when one is available.

Usage:
    python report_flash_size.py --current-dir current [--baseline-dir baseline] --out report.md
"""
import argparse
import glob
import json
import os

WARN_THRESHOLD_PCT = 95.0


def load_records(directory):
    records = {}
    if not directory or not os.path.isdir(directory):
        return records
    for path in glob.glob(os.path.join(directory, "**", "*.json"), recursive=True):
        with open(path) as f:
            record = json.load(f)
        records[record["env"]] = record
    return records


def fmt_bytes(n):
    return f"{n:,}"


def fmt_delta(current, baseline):
    if baseline is None:
        return "_new_"
    delta = current - baseline
    if delta == 0:
        return "±0 B"
    sign = "+" if delta > 0 else ""
    return f"{sign}{delta:,} B"


def fmt_delta_pct(current, baseline):
    if baseline is None:
        return ""
    delta = current - baseline
    sign = "+" if delta > 0 else ""
    return f" ({sign}{delta:.2f} pp)"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current-dir", required=True)
    parser.add_argument("--baseline-dir", default=None)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    current = load_records(args.current_dir)
    baseline = load_records(args.baseline_dir)

    lines = []
    lines.append("### Flash usage")
    lines.append("")
    if not current:
        lines.append("No flash size data was collected for this build.")
        with open(args.out, "w") as f:
            f.write("\n".join(lines) + "\n")
        return 0

    lines.append("| Environment | Size | Flash used | Change vs base |")
    lines.append("| --- | ---: | ---: | ---: |")

    total_size = 0
    total_baseline = 0
    total_baseline_known = True

    for env in sorted(current):
        rec = current[env]
        base_rec = baseline.get(env)
        base_size = base_rec["size"] if base_rec else None

        total_size += rec["size"]
        if base_size is None:
            total_baseline_known = False
        else:
            total_baseline += base_size

        percent = rec["percent"]
        flag = ""
        if rec["size"] > rec["max_size"]:
            flag = " :rotating_light:"
        elif percent >= WARN_THRESHOLD_PCT:
            flag = " :warning:"

        delta = fmt_delta(rec["size"], base_size) + fmt_delta_pct(percent, base_rec["percent"] if base_rec else None)

        lines.append(
            f"| {env} | {fmt_bytes(rec['size'])} B | {percent:.1f}%{flag} | {delta} |"
        )

    total_delta = fmt_delta(total_size, total_baseline if total_baseline_known else None)
    lines.append(f"| **Total** | **{fmt_bytes(total_size)} B** | — | **{total_delta}** |")
    lines.append("")
    lines.append(
        f"Builds at or above {WARN_THRESHOLD_PCT:.0f}% of their app partition are flagged "
        ":warning:; builds that exceed it (:rotating_light:) fail the build. The Total row "
        "has no \"Flash used\" percent -- each env has a different app partition size, so a "
        "combined percentage wouldn't correspond to any real flash budget."
    )

    with open(args.out, "w") as f:
        f.write("\n".join(lines) + "\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
