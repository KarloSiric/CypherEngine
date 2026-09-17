#!/usr/bin/env python3
"""Benchmark complete TileEditor dev-pack validation and clean cooking."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
import time


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def run(command: list[str], cwd: Path) -> float:
    started = time.perf_counter()
    completed = subprocess.run(command, cwd=cwd, capture_output=True, text=True, check=False)
    elapsed = time.perf_counter() - started
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return elapsed


def summary(samples: list[float], input_count: int) -> dict[str, float]:
    median = statistics.median(samples)
    return {
        "minimum_ms": min(samples) * 1000.0,
        "median_ms": median * 1000.0,
        "maximum_ms": max(samples) * 1000.0,
        "median_inputs_per_second": input_count / median,
    }


def main() -> int:
    root = repository_root()
    default_compiler = root / "out" / "build" / "tile-editor-debug" / "bin" / "CypherResourceCompiler"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=default_compiler)
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--json", action="store_true", help="write machine-readable output")
    arguments = parser.parse_args()
    if arguments.iterations < 1 or arguments.warmup < 0:
        parser.error("iterations must be positive and warmup must be non-negative")

    compiler = arguments.compiler.resolve()
    if not compiler.is_file():
        parser.error(f"compiler does not exist: {compiler}")
    manifest = root / "assets" / "tileeditor_dev_pack.rsp"
    input_count = sum(
        1
        for line in manifest.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    )
    common = [
        str(compiler),
        "-s", "assets",
        "--target", "host",
        "--profile", "development",
        "--color", "never",
        "--progress", "none",
    ]

    validate_samples: list[float] = []
    compile_samples: list[float] = []
    artifact_counts: list[int] = []
    total_runs = arguments.warmup + arguments.iterations
    for index in range(total_runs):
        validate_elapsed = run([common[0], "validate", *common[1:], "@assets/tileeditor_dev_pack.rsp"], root)
        with tempfile.TemporaryDirectory(prefix="cypher-dev-pack-benchmark-") as output:
            compile_elapsed = run(
                [common[0], "compile", *common[1:3], "-o", output, *common[3:], "@assets/tileeditor_dev_pack.rsp"],
                root,
            )
            artifact_count = sum(1 for path in Path(output).rglob("*") if path.is_file())
        if index >= arguments.warmup:
            validate_samples.append(validate_elapsed)
            compile_samples.append(compile_elapsed)
            artifact_counts.append(artifact_count)

    if any(count != input_count for count in artifact_counts):
        raise RuntimeError(f"artifact counts do not match {input_count}: {artifact_counts}")
    result = {
        "input_count": input_count,
        "iterations": arguments.iterations,
        "warmup": arguments.warmup,
        "validate": summary(validate_samples, input_count),
        "clean_compile": summary(compile_samples, input_count),
    }
    if arguments.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"TileEditor dev pack: {input_count} inputs, {arguments.iterations} measured iterations")
        for name in ("validate", "clean_compile"):
            values = result[name]
            print(
                f"{name:13s} median {values['median_ms']:9.3f} ms | "
                f"min {values['minimum_ms']:9.3f} ms | max {values['maximum_ms']:9.3f} ms | "
                f"{values['median_inputs_per_second']:7.2f} inputs/s"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
