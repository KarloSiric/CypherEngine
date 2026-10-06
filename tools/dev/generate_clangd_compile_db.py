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
import os
from pathlib import Path
import tempfile


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Publish an existing CMake compile database for clangd."
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument(
        "--preset",
        help="Use out/build/<preset> (default: debug).",
    )
    source.add_argument(
        "--build-dir",
        type=Path,
        help="Use an existing build directory, relative to the repository or absolute.",
    )
    return parser.parse_args()


def native_database_path(arguments: argparse.Namespace, root_dir: Path) -> Path:
    build_dir = arguments.build_dir
    if build_dir is None:
        build_dir = root_dir / "out" / "build" / (arguments.preset or "debug")
    elif not build_dir.is_absolute():
        build_dir = root_dir / build_dir
    return build_dir.resolve() / "compile_commands.json"


def publish_database(native_db_path: Path, clangd_db_path: Path) -> tuple[str, int]:
    # Validate before replacing the active database. A missing/deleted build
    # must not destroy the last working editor configuration.
    native_entries = json.loads(native_db_path.read_text(encoding="utf-8"))
    if not isinstance(native_entries, list) or not native_entries:
        raise ValueError("compile database must be a nonempty array")
    for entry in native_entries:
        if not isinstance(entry, dict) or not all(
            isinstance(entry.get(key), str) and entry[key]
            for key in ("directory", "file")
        ):
            raise ValueError("compile database entries need directory and file")
        command = entry.get("command")
        arguments = entry.get("arguments")
        has_command = isinstance(command, str) and bool(command)
        has_arguments = isinstance(arguments, list) and bool(arguments) and all(
            isinstance(argument, str) for argument in arguments
        )
        if not has_command and not has_arguments:
            raise ValueError("compile database entries need command or arguments")

    if native_db_path.resolve() == clangd_db_path.resolve():
        # An existing link to this build already follows every CMake refresh.
        return "linked" if clangd_db_path.is_symlink() else "unchanged", len(native_entries)

    clangd_db_dir = clangd_db_path.parent
    clangd_db_dir.mkdir(parents=True, exist_ok=True)
    database_mode = "linked"
    try:
        link_target = Path(os.path.relpath(native_db_path, clangd_db_dir))
    except ValueError:
        # Windows build and source directories may be on different drives.
        link_target = native_db_path.resolve()
    with tempfile.TemporaryDirectory(prefix=".compile-db-", dir=clangd_db_dir) as temporary:
        temporary_db_path = Path(temporary) / "compile_commands.json"
        try:
            temporary_db_path.symlink_to(link_target)
        except OSError:
            # Some Windows configurations do not permit user-created symlinks.
            database_mode = "copied"
            temporary_db_path.write_text(
                json.dumps(native_entries, indent=2) + "\n", encoding="utf-8"
            )
        temporary_db_path.replace(clangd_db_path)
    return database_mode, len(native_entries)


def main() -> int:
    arguments = parse_arguments()
    root_dir = Path(__file__).resolve().parents[2]
    native_db_path = native_database_path(arguments, root_dir)
    clangd_db_path = root_dir / "build-clangd" / "compile_commands.json"
    try:
        database_mode, entry_count = publish_database(native_db_path, clangd_db_path)
    except (OSError, ValueError) as error:
        raise SystemExit(
            f"cannot publish {native_db_path}: {error}\n"
            "Configure the build with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first."
        ) from error

    print(f"{database_mode}: {clangd_db_path}")
    print(f"source: {native_db_path}")
    print(f"entries: {entry_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
