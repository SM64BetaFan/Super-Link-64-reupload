#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Super Link 64 contributors

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_release_tree", PROJECT / "tools/check_release_tree.py"
)
assert SPEC is not None and SPEC.loader is not None
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


class ReleaseTreeTests(unittest.TestCase):
    def make_tree(self, root: Path, names: list[str]) -> None:
        listed = sorted([CHECK.ALLOWLIST_NAME, *names])
        (root / CHECK.ALLOWLIST_NAME).write_text(
            "\n".join(listed) + "\n", encoding="utf-8"
        )
        for name in names:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("source\n", encoding="utf-8")

    def test_clean_allowlisted_tree_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, ["source.txt"])
            self.assertEqual(CHECK.check_tree(root), [])

    def test_unexpected_source_file_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, ["source.txt"])
            extra = root / "vendor/upstream.c"
            extra.parent.mkdir()
            extra.write_text("source\n", encoding="utf-8")
            self.assertTrue(any("unexpected release file" in failure
                                for failure in CHECK.check_tree(root)))

    def test_missing_and_forbidden_allowlisted_files_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, ["game.z64"])
            failures = CHECK.check_tree(root)
            self.assertTrue(any("forbidden release file" in failure
                                for failure in failures))
            (root / "game.z64").unlink()
            self.assertTrue(any("missing release file" in failure
                                for failure in CHECK.check_tree(root)))

    def test_symlink_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, ["target.txt"])
            link = root / "link.txt"
            try:
                link.symlink_to("target.txt")
            except OSError:
                self.skipTest("symlinks are unavailable")
            lines = (root / CHECK.ALLOWLIST_NAME).read_text().splitlines()
            lines.append("link.txt")
            (root / CHECK.ALLOWLIST_NAME).write_text(
                "\n".join(sorted(lines)) + "\n", encoding="utf-8"
            )
            self.assertTrue(any("not a regular file" in failure
                                for failure in CHECK.check_tree(root)))

    def test_git_metadata_is_not_release_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, ["source.txt"])
            metadata = root / ".git/objects/example"
            metadata.parent.mkdir(parents=True)
            metadata.write_bytes(b"\x00git data")
            self.assertEqual(CHECK.check_tree(root), [])


if __name__ == "__main__":
    unittest.main()
