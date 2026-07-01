#!/usr/bin/env python3
"""Build and run Cypher benchmark binaries, saving Google Benchmark JSON."""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT_DIR = REPO_ROOT / "benchmarks" / "results"


def run_command(args: list[str]) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=REPO_ROOT, check=True)


def build_dir_for_preset(preset: str) -> Path:
    return REPO_ROOT / "out" / "build" / preset


def executable_suffix() -> str:
    return ".exe" if platform.system() == "Windows" else ""


def discover_benchmarks(build_dir: Path) -> list[Path]:
    bin_dir = build_dir / "bin"
    suffix = executable_suffix()
    if not bin_dir.exists():
        return []

    benchmarks = []
    for path in sorted(bin_dir.iterdir()):
        if not path.is_file():
            continue
        if suffix and path.suffix != suffix:
            continue
        stem = path.stem if suffix else path.name
        if stem.endswith("_bench") or stem.endswith("_benchmark"):
            benchmarks.append(path)
    return benchmarks


def write_manifest(out_dir: Path, preset: str, benchmarks: list[Path]) -> None:
    manifest = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "preset": preset,
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "processor": platform.processor(),
            "python": platform.python_version(),
        },
        "benchmarks": [path.name for path in benchmarks],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="bench-release", help="CMake configure/build preset to use.")
    parser.add_argument("--out-dir", default=str(DEFAULT_OUT_DIR), help="Directory for benchmark JSON output.")
    parser.add_argument("--filter", default=".*", help="Google Benchmark filter.")
    parser.add_argument("--min-time", default="0.2s", help="Google Benchmark minimum time.")
    parser.add_argument("--repetitions", default="5", help="Google Benchmark repetitions.")
    parser.add_argument("--aggregate-only", action="store_true", help="Report aggregate benchmark rows only.")
    parser.add_argument("--no-build", action="store_true", help="Skip configure/build and only run existing binaries.")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = REPO_ROOT / out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    if not args.no_build:
        run_command(["cmake", "--preset", args.preset])
        run_command(["cmake", "--build", "--preset", args.preset, "--parallel"])

    build_dir = build_dir_for_preset(args.preset)
    benchmarks = discover_benchmarks(build_dir)
    if not benchmarks:
        print(f"No benchmark binaries found under {build_dir / 'bin'}", file=sys.stderr)
        return 1

    write_manifest(out_dir, args.preset, benchmarks)

    for benchmark_path in benchmarks:
        out_file = out_dir / f"{benchmark_path.stem}.json"
        cmd = [
            str(benchmark_path),
            f"--benchmark_filter={args.filter}",
            f"--benchmark_min_time={args.min_time}",
            f"--benchmark_repetitions={args.repetitions}",
            "--benchmark_out_format=json",
            f"--benchmark_out={out_file}",
        ]
        if args.aggregate_only:
            cmd.append("--benchmark_report_aggregates_only=true")
        run_command(cmd)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
