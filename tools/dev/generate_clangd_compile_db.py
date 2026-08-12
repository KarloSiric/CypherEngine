#!/usr/bin/env python3
# //////////////////////////////////////////////////////////////////////////
# //
# //  CypherEngine Source Code
# //  Copyright (c) 2026 Karlo Siric. All rights reserved.
# //
# //  File: tools/dev/generate_clangd_compile_db.py
# //  Purpose: Provides developer tooling for generate clangd compile db.
# //  Details: This developer helper automates a repetitive local workflow. It should
# //           remain small, transparent, and easy to replace if the build model
# //           changes.
# //
# //  History:
# //  - Created by Karlo Siric on 2026-04-30
# //
# //  This file is proprietary and confidential. See LICENSE for details.
# //
# //////////////////////////////////////////////////////////////////////////

import argparse
import json
from pathlib import Path


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate clangd's compile database from a CMake preset."
    )
    parser.add_argument(
        "--preset",
        default="debug",
        help="CMake configure preset whose compile database should be used.",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root_dir = Path(__file__).resolve().parents[2]
    native_db_path = (
        root_dir / "out" / "build" / arguments.preset / "compile_commands.json"
    )
    clangd_db_dir = root_dir / "build-clangd"
    clangd_db_path = clangd_db_dir / "compile_commands.json"

    if not native_db_path.exists():
        raise SystemExit(f"missing native compile database: {native_db_path}")

    native_entries = json.loads(native_db_path.read_text())

    clangd_db_dir.mkdir(parents=True, exist_ok=True)
    if clangd_db_path.exists() or clangd_db_path.is_symlink():
        clangd_db_path.unlink()

    database_mode = "linked"
    relative_native_path = Path("..") / native_db_path.relative_to(root_dir)
    try:
        clangd_db_path.symlink_to(relative_native_path)
    except OSError:
        # Some Windows configurations do not permit user-created symlinks.
        # Keep an atomic copy fallback so the helper remains portable.
        database_mode = "copied"
        temporary_db_path = clangd_db_path.with_suffix(".json.tmp")
        temporary_db_path.write_text(json.dumps(native_entries, indent=2) + "\n")
        temporary_db_path.replace(clangd_db_path)

    print(f"{database_mode}: {clangd_db_path}")
    print(f"source: {native_db_path}")
    print(f"entries: {len(native_entries)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
