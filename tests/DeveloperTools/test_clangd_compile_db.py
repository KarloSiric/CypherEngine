"""Regression coverage for publishing existing CMake compile databases."""

import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


REPOSITORY = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "generate_clangd_compile_db",
    REPOSITORY / "tools" / "dev" / "generate_clangd_compile_db.py",
)
compile_db = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compile_db)


class CompileDatabaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cypher compile db ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / "build-clangd" / "compile_commands.json"

    def database(self, build_name, compiler="clang++"):
        path = self.root / build_name / "compile_commands.json"
        path.parent.mkdir(parents=True, exist_ok=True)
        entries = [{
            "directory": str(path.parent),
            "file": str(self.root / "source file.cpp"),
            "arguments": [compiler, "-std=c++20", "-c", "source file.cpp"],
        }]
        path.write_text(json.dumps(entries), encoding="utf-8")
        return path, entries

    def test_link_follows_cmake_regeneration(self):
        native, entries = self.database("build first")
        mode, count = compile_db.publish_database(native, self.output)
        self.assertEqual(count, 1)
        if mode != "linked":
            self.skipTest("User-created symlinks are unavailable on this platform")
        self.assertTrue(self.output.is_symlink())
        entries[0]["arguments"].append("-DUPDATED=1")
        # CMake may replace the native file instead of updating it in place.
        replacement = native.with_suffix(".new")
        replacement.write_text(json.dumps(entries), encoding="utf-8")
        replacement.replace(native)
        self.assertEqual(json.loads(self.output.read_text()), entries)
        self.assertEqual(compile_db.publish_database(native, self.output), ("linked", 1))

    def test_switching_builds_publishes_one_complete_database(self):
        first, _ = self.database("build first")
        second, entries = self.database("build second", "clang++-new")
        mode, _ = compile_db.publish_database(first, self.output)
        if mode != "linked":
            self.skipTest("User-created symlinks are unavailable on this platform")
        original_replace = Path.replace
        observed = []

        def observe_replace(source, destination):
            observed.append((source.parent, Path(destination)))
            self.assertEqual(self.output.resolve(), first.resolve())
            return original_replace(source, destination)

        with mock.patch.object(Path, "replace", observe_replace):
            self.assertEqual(compile_db.publish_database(second, self.output), ("linked", 1))
        self.assertEqual(len(observed), 1)
        self.assertEqual(observed[0][0].parent, self.output.parent)
        self.assertEqual(observed[0][1], self.output)
        self.assertEqual(self.output.resolve(), second.resolve())
        self.assertEqual(json.loads(self.output.read_text()), entries)
        self.assertEqual(list(self.output.parent.glob(".compile-db-*")), [])

    def test_invalid_source_preserves_previous_publication(self):
        first, entries = self.database("working build")
        compile_db.publish_database(first, self.output)
        original_resolved_path = self.output.resolve()
        bad = self.root / "bad.json"
        invalid = ["not json", "{}", "[]", '[{"file":"x.cpp"}]',
                   '[{"directory":"/tmp","file":"x.cpp","arguments":[]}]']
        for contents in invalid:
            with self.subTest(contents=contents):
                bad.write_text(contents, encoding="utf-8")
                with self.assertRaises(ValueError):
                    compile_db.publish_database(bad, self.output)
                self.assertEqual(self.output.resolve(), original_resolved_path)
                self.assertEqual(json.loads(self.output.read_text()), entries)
        with self.assertRaises(FileNotFoundError):
            compile_db.publish_database(self.root / "missing.json", self.output)
        self.assertEqual(self.output.resolve(), original_resolved_path)
        self.assertEqual(json.loads(self.output.read_text()), entries)

    def test_copy_fallback_when_symlinks_are_unavailable(self):
        native, entries = self.database("external build with spaces")
        with mock.patch.object(Path, "symlink_to", side_effect=OSError("not permitted")):
            self.assertEqual(compile_db.publish_database(native, self.output), ("copied", 1))
        self.assertFalse(self.output.is_symlink())
        self.assertEqual(json.loads(self.output.read_text()), entries)

    def test_build_selection_is_relative_to_repository(self):
        repository = self.root / "repository with spaces"
        external = self.root / "external build with spaces"
        cases = [
            (None, None, repository / "out/build/debug"),
            ("release", None, repository / "out/build/release"),
            (None, Path("build custom"), repository / "build custom"),
            (None, external, external),
        ]
        for preset, build_dir, expected in cases:
            with self.subTest(preset=preset, build_dir=build_dir):
                arguments = argparse.Namespace(preset=preset, build_dir=build_dir)
                self.assertEqual(
                    compile_db.native_database_path(arguments, repository),
                    expected.resolve() / "compile_commands.json",
                )

    def test_cli_rejects_two_sources(self):
        argv = ["generate_clangd_compile_db.py", "--preset", "debug",
                "--build-dir", "build custom"]
        with mock.patch("sys.argv", argv), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as failure:
                compile_db.parse_arguments()
        self.assertEqual(failure.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
