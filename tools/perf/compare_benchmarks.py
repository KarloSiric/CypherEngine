#!/usr/bin/env python3
# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: tools/perf/compare_benchmarks.py
# //  Purpose: Provides performance tooling for compare benchmarks.
# //  Details: This tool supports repeatable performance inspection and benchmark
# //           reporting. It should stay scriptable so CI and local development can use
# //           the same path.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-07-01
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

"""Compare two Google Benchmark JSON files and report CPU-time deltas."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load_cpu_times(path: Path) -> dict[str, float]:
    data = json.loads(path.read_text(encoding="utf-8"))
    results: dict[str, float] = {}
    for item in data.get("benchmarks", []):
        name = item.get("name")
        cpu_time = item.get("cpu_time")
        if not name or cpu_time is None:
            continue
        if name.endswith("_stddev") or name.endswith("_cv"):
            continue
        results[name] = float(cpu_time)
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", help="Baseline Google Benchmark JSON file.")
    parser.add_argument("current", help="Current Google Benchmark JSON file.")
    parser.add_argument("--fail-threshold", type=float, default=0.15, help="Fail if regression exceeds this ratio.")
    args = parser.parse_args()

    baseline = load_cpu_times(Path(args.baseline))
    current = load_cpu_times(Path(args.current))

    failed = False
    print("| benchmark | baseline cpu | current cpu | delta |")
    print("| --- | ---: | ---: | ---: |")

    for name in sorted(current):
        if name not in baseline:
            print(f"| {name} | missing | {current[name]:.3f} | new |")
            continue

        old = baseline[name]
        new = current[name]
        if old == 0.0:
            delta = 0.0
        else:
            delta = (new - old) / old
        if delta > args.fail_threshold:
            failed = True
        print(f"| {name} | {old:.3f} | {new:.3f} | {delta:+.2%} |")

    for name in sorted(set(baseline) - set(current)):
        print(f"| {name} | {baseline[name]:.3f} | missing | removed |")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
