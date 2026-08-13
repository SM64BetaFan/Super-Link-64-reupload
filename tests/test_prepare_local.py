#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Super Link 64 contributors

from __future__ import annotations

import io
import importlib.util
from pathlib import Path
from unittest import mock
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "prepare_local", PROJECT / "tools/prepare_local.py"
)
assert SPEC is not None and SPEC.loader is not None
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class PrepareLocalTests(unittest.TestCase):
    def test_automatic_stage_is_removed_after_validation_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory) / "super-link-64-generated"
            with mock.patch.object(
                PREPARE.tempfile, "mkdtemp", return_value=str(temporary)
            ), mock.patch.object(
                PREPARE.sys,
                "argv",
                [
                    "prepare_local.py",
                    "--coopdx",
                    str(Path(directory) / "not-coopdx"),
                    "--liboot-prefix",
                    str(Path(directory) / "not-liboot"),
                ],
            ), mock.patch.object(
                PREPARE.sys, "stderr", io.StringIO()
            ):
                self.assertEqual(PREPARE.main(), 1)
            self.assertFalse(temporary.exists())

    def test_liboot_prefix_requires_header_and_library(self):
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.validate_liboot(prefix)
            header = prefix / "include/liboot/liboot_engine.h"
            header.parent.mkdir(parents=True)
            header.write_text("/* test */\n", encoding="utf-8")
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.validate_liboot(prefix)
            shared = prefix / "lib/liboot.dylib"
            shared.parent.mkdir(parents=True)
            shared.write_bytes(b"test")
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.validate_liboot(prefix)
            library = prefix / "lib/liboot.a"
            library.write_bytes(b"test")
            PREPARE.validate_liboot(prefix)

    def test_copy_tree_replaces_only_the_exact_stage_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source"
            target = root / "target"
            source.mkdir()
            target.mkdir()
            (source / "fresh.c").write_text("fresh\n", encoding="utf-8")
            (target / "stale.c").write_text("stale\n", encoding="utf-8")
            PREPARE.copy_tree(source, target)
            self.assertEqual((target / "fresh.c").read_text(), "fresh\n")
            self.assertFalse((target / "stale.c").exists())

    def test_plan_contains_pin_and_no_upstream_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage = root / "stage"
            stage.mkdir()
            PREPARE.write_plan(stage)
            text = (stage / "LOCAL_INTEGRATION.txt").read_text(encoding="utf-8")
            self.assertIn(PREPARE.PINNED_COOPDX, text)
            self.assertIn("Do not redistribute", text)
            self.assertNotIn("diff --git", text)
            self.assertNotIn(str(root), text)

    def test_stage_refuses_nonempty_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory) / "super-link-64-stage"
            stage.mkdir()
            (stage / "stale.txt").write_text("stale\n", encoding="utf-8")
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.stage_original_files(PROJECT, stage)
            self.assertEqual((stage / "stale.txt").read_text(), "stale\n")

    def test_current_installed_paths_detects_new_file(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            expected = checkout / PREPARE.BRIDGE_DESTINATION / "native/known.c"
            unexpected = checkout / PREPARE.MOD_DESTINATION / "extra.lua"
            expected.parent.mkdir(parents=True)
            unexpected.parent.mkdir(parents=True)
            expected.write_text("known\n", encoding="utf-8")
            unexpected.write_text("extra\n", encoding="utf-8")
            self.assertEqual(
                PREPARE.current_installed_paths(checkout),
                {
                    expected.relative_to(checkout),
                    unexpected.relative_to(checkout),
                },
            )

    def test_uninstall_refuses_new_installed_file(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            known = checkout / PREPARE.BRIDGE_DESTINATION / "native/known.c"
            extra = checkout / PREPARE.MOD_DESTINATION / "extra.lua"
            known.parent.mkdir(parents=True)
            extra.parent.mkdir(parents=True)
            known.write_text("known\n", encoding="utf-8")
            extra.write_text("extra\n", encoding="utf-8")
            control = checkout / PREPARE.CONTROL_DIRECTORY
            control.mkdir()
            manifest = {
                "format": PREPARE.MANIFEST_FORMAT,
                "coopdx_commit": PREPARE.PINNED_COOPDX,
                "host_files": [],
                "installed_roots": [
                    str(PREPARE.BRIDGE_DESTINATION),
                    str(PREPARE.MOD_DESTINATION),
                ],
                "installed_hashes": {
                    str(known.relative_to(checkout)): PREPARE.digest_file(known)
                },
                "host_hashes": {},
                "backup_hashes": {},
            }
            (control / PREPARE.MANIFEST_NAME).write_text(
                __import__("json").dumps(manifest), encoding="utf-8"
            )
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.uninstall_local(checkout, False)
            self.assertTrue(known.exists())
            self.assertTrue(extra.exists())
            self.assertTrue(control.exists())
            PREPARE.uninstall_local(checkout, True)
            self.assertFalse((checkout / PREPARE.BRIDGE_DESTINATION).exists())
            self.assertFalse((checkout / PREPARE.MOD_DESTINATION).exists())
            self.assertFalse(control.exists())

    def test_uninstall_refuses_untracked_empty_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            known = checkout / PREPARE.MOD_DESTINATION / "main.lua"
            extra = checkout / PREPARE.MOD_DESTINATION / "local-notes"
            known.parent.mkdir(parents=True)
            extra.mkdir()
            known.write_text("known\n", encoding="utf-8")
            control = checkout / PREPARE.CONTROL_DIRECTORY
            control.mkdir()
            manifest = {
                "format": PREPARE.MANIFEST_FORMAT,
                "coopdx_commit": PREPARE.PINNED_COOPDX,
                "host_files": [],
                "installed_roots": [
                    str(PREPARE.BRIDGE_DESTINATION),
                    str(PREPARE.MOD_DESTINATION),
                ],
                "installed_hashes": {
                    str(known.relative_to(checkout)): PREPARE.digest_file(known)
                },
                "host_hashes": {},
                "backup_hashes": {},
            }
            (control / PREPARE.MANIFEST_NAME).write_text(
                __import__("json").dumps(manifest), encoding="utf-8"
            )
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.uninstall_local(checkout, False)
            self.assertTrue(extra.is_dir())

    def test_uninstall_requires_every_host_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            relative = PREPARE.host_insertions()[0][0]
            host = checkout / relative
            host.parent.mkdir(parents=True, exist_ok=True)
            host.write_text("patched host\n", encoding="utf-8")
            control = checkout / PREPARE.CONTROL_DIRECTORY
            control.mkdir()
            manifest = {
                "format": PREPARE.MANIFEST_FORMAT,
                "coopdx_commit": PREPARE.PINNED_COOPDX,
                "host_files": [str(relative)],
                "installed_roots": [
                    str(PREPARE.BRIDGE_DESTINATION),
                    str(PREPARE.MOD_DESTINATION),
                ],
                "installed_hashes": {},
                "host_hashes": {str(relative): PREPARE.digest_file(host)},
                "backup_hashes": {str(relative): "a" * 64},
            }
            (control / PREPARE.MANIFEST_NAME).write_text(
                __import__("json").dumps(manifest), encoding="utf-8"
            )
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.uninstall_local(checkout, False)
            self.assertEqual(host.read_text(encoding="utf-8"), "patched host\n")
            self.assertTrue(control.exists())

    def test_uninstall_refuses_changed_host_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            checkout = Path(directory)
            relative = PREPARE.host_insertions()[0][0]
            host = checkout / relative
            host.parent.mkdir(parents=True, exist_ok=True)
            host.write_text("patched host\n", encoding="utf-8")
            control = checkout / PREPARE.CONTROL_DIRECTORY
            backup = control / "backup" / relative
            backup.parent.mkdir(parents=True)
            backup.write_text("changed backup\n", encoding="utf-8")
            manifest = {
                "format": PREPARE.MANIFEST_FORMAT,
                "coopdx_commit": PREPARE.PINNED_COOPDX,
                "host_files": [str(relative)],
                "installed_roots": [
                    str(PREPARE.BRIDGE_DESTINATION),
                    str(PREPARE.MOD_DESTINATION),
                ],
                "installed_hashes": {},
                "host_hashes": {str(relative): PREPARE.digest_file(host)},
                "backup_hashes": {str(relative): "b" * 64},
            }
            (control / PREPARE.MANIFEST_NAME).write_text(
                __import__("json").dumps(manifest), encoding="utf-8"
            )
            with self.assertRaises(PREPARE.PrepareError):
                PREPARE.uninstall_local(checkout, False)
            self.assertEqual(host.read_text(encoding="utf-8"), "patched host\n")
            self.assertEqual(backup.read_text(encoding="utf-8"), "changed backup\n")

    def test_selector_fails_on_ambiguous_host_structure(self):
        with self.assertRaises(PREPARE.PrepareError):
            PREPARE.host_insertion_position(
                "# Source code files\n# Source code files\n",
                Path("Makefile"),
                "make-sources",
            )

    def test_selector_returns_a_character_boundary(self):
        text = "first\ngAudioApi->play(data, size);\nlast\n"
        position = PREPARE.host_insertion_position(
            text, Path("src/pc/pc_main.c"), "audio-play"
        )
        self.assertEqual(text[:position], "first\n")

    def test_stage_must_not_overlap_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / "project"
            checkout = root / "checkout"
            prefix = root / "prefix"
            for path in (project, checkout, prefix):
                path.mkdir()
            for stage in (
                project / "super-link-64-stage",
                checkout / "super-link-64-stage",
                prefix / "super-link-64-stage",
            ):
                with self.assertRaises(PREPARE.PrepareError):
                    PREPARE.validate_stage(stage, project, checkout, prefix)

    def test_manifest_rejects_paths_outside_checkout(self):
        digest = "0" * 64
        manifest = {
            "format": PREPARE.MANIFEST_FORMAT,
            "coopdx_commit": PREPARE.PINNED_COOPDX,
            "host_files": ["../../outside.c"],
            "installed_roots": [
                str(PREPARE.BRIDGE_DESTINATION),
                str(PREPARE.MOD_DESTINATION),
            ],
            "installed_hashes": {},
            "host_hashes": {"../../outside.c": digest},
            "backup_hashes": {"../../outside.c": digest},
        }
        with self.assertRaises(PREPARE.PrepareError):
            PREPARE.validate_manifest(manifest)

    def test_manifest_accepts_installer_owned_paths(self):
        digest = "a" * 64
        host_files = sorted({str(entry[0]) for entry in PREPARE.host_insertions()})
        manifest = {
            "format": PREPARE.MANIFEST_FORMAT,
            "coopdx_commit": PREPARE.PINNED_COOPDX,
            "host_files": host_files,
            "installed_roots": [
                str(PREPARE.BRIDGE_DESTINATION),
                str(PREPARE.MOD_DESTINATION),
            ],
            "installed_hashes": {
                "src/pc/super_link_64/native/sl64_runtime.c": digest,
                "mods/super-link-64/main.lua": digest,
            },
            "host_hashes": {name: digest for name in host_files},
            "backup_hashes": {name: digest for name in host_files},
        }
        validated_hosts, installed, host_hashes, backup_hashes = (
            PREPARE.validate_manifest(manifest)
        )
        self.assertEqual({str(path) for path in validated_hosts}, set(host_files))
        self.assertEqual(len(installed), 2)
        self.assertEqual(len(host_hashes), len(host_files))
        self.assertEqual(len(backup_hashes), len(host_files))


if __name__ == "__main__":
    unittest.main()
