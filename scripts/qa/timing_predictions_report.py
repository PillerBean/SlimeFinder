#!/usr/bin/env python3
"""Summarize SlimeFinder timing predictions vs actual runtimes.

Usage:
  python scripts/qa/timing_predictions_report.py [path/to/timing_predictions.tsv]

The TSV is written by the GUI next to config.json after each confirmed
whole-map search. It contains one row per completed run.
"""
from __future__ import annotations

import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def main() -> int:
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("timing_predictions.tsv")
    if not path.exists():
        print(f"missing: {path}")
        return 1
    rows = []
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            try:
                pred = float(row["predicted_s"])
                actual = float(row["actual_s"])
            except (KeyError, ValueError):
                continue
            if pred <= 0 or actual <= 0:
                continue
            ratio = actual / pred
            rows.append((row, pred, actual, ratio))
    if not rows:
        print("no valid rows")
        return 1

    def group_key(row):
        return (
            row.get("mode", "?"),
            row.get("algorithm", "?"),
            "gpu" if row.get("gpu_used") == "1" else "cpu",
            "cache" if row.get("cache") == "1" else "nocache",
            "stop" if row.get("stop") == "1" else "full",
        )

    groups = defaultdict(list)
    for row, pred, actual, ratio in rows:
        groups[group_key(row)].append((row, pred, actual, ratio))

    print(f"file: {path}")
    print(f"rows: {len(rows)}")
    print()
    print("group\tcount\tmedian_ratio\tp90_abs_log_err\tmax_ratio")
    for key, items in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        ratios = [x[3] for x in items]
        logs = [abs(math.log(r)) for r in ratios]
        logs_sorted = sorted(logs)
        p90 = logs_sorted[min(len(logs_sorted) - 1, int(len(logs_sorted) * 0.9))]
        print(
            f"{'/'.join(key)}\t{len(items)}\t{statistics.median(ratios):.3f}\t"
            f"{p90:.3f}\t{max(ratios):.3f}"
        )

    print()
    print("worst 10 by |log(actual/predicted)|")
    for row, pred, actual, ratio in sorted(rows, key=lambda x: -abs(math.log(x[3])))[:10]:
        print(
            f"  ratio={ratio:8.3f} pred={pred:10.3f}s actual={actual:10.3f}s "
            f"gpu={row.get('gpu_used')} cache={row.get('cache')} "
            f"stop={row.get('stop')} mode={row.get('mode')} alg={row.get('algorithm')}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
