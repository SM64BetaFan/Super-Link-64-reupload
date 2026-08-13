#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Super Link 64 contributors

"""Enforce the reviewed source-only release inventory and content policy."""

from __future__ import annotations

import argparse
from pathlib import Path
from pathlib import PurePosixPath
import re
import sys


FORBIDDEN_SUFFIXES = {
    ".z64", ".v64", ".n64", ".rom", ".wad", ".sav", ".sra", ".eep",
    ".fla", ".dll", ".dylib", ".so", ".exe", ".o", ".a", ".zip",
    ".7z", ".rar", ".tar", ".gz", ".bz2", ".xz", ".png", ".jpg",
    ".jpeg", ".webp", ".wav", ".mp3", ".ogg", ".flac",
}
ALLOWLIST_NAME = "release-files.txt"
TEXT_SUFFIXES = {
    "", ".c", ".h", ".lua", ".md", ".py", ".yml", ".yaml", ".toml",
    ".lock", ".txt", ".json", ".gitignore", ".mk",
}
PRIVATE_PATHS = (
    re.compile(rb"/Users/[A-Za-z0-9_.-]+/"),
    re.compile(rb"/home/[A-Za-z0-9_.-]+/"),
    re.compile(rb"[A-Za-z]:\\Users\\[A-Za-z0-9_.-]+\\"),
)


def read_allowlist(root: Path, failures: list[str]) -> set[str]:
    path = root / ALLOWLIST_NAME
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        failures.append(f"cannot read {ALLOWLIST_NAME}: {error}")
        return set()
    if not lines or lines != sorted(lines) or len(lines) != len(set(lines)):
        failures.append(f"{ALLOWLIST_NAME} must be nonempty, sorted, and unique")
    for name in lines:
        parsed = PurePosixPath(name)
        if (
            not name
            or "\\" in name
            or parsed.is_absolute()
            or any(part in {"", ".", ".."} for part in parsed.parts)
            or parsed.as_posix() != name
        ):
            failures.append(f"invalid release allowlist path: {name!r}")
    if ALLOWLIST_NAME not in lines:
        failures.append(f"{ALLOWLIST_NAME} must list itself")
    return set(lines)


def inventory(root: Path) -> dict[str, Path]:
    entries: dict[str, Path] = {}
    for path in root.rglob("*"):
        relative = path.relative_to(root)
        if ".git" in relative.parts:
            continue
        if path.is_dir() and not path.is_symlink():
            continue
        entries[relative.as_posix()] = path
    return entries


def check_tree(root: Path) -> list[str]:
    failures: list[str] = []
    allowed = read_allowlist(root, failures)
    actual = inventory(root)

    for name in sorted(set(actual) - allowed):
        failures.append(f"unexpected release file: {name}")
    for name in sorted(allowed - set(actual)):
        failures.append(f"missing release file: {name}")

    for name in sorted(allowed & set(actual)):
        path = actual[name]
        relative = Path(name)
        if path.is_symlink() or not path.is_file():
            failures.append(f"release entry is not a regular file: {name}")
            continue
        suffix = path.suffix.lower()
        if suffix in FORBIDDEN_SUFFIXES or path.name.endswith(".dSYM"):
            failures.append(f"forbidden release file: {relative}")
            continue
        if suffix not in TEXT_SUFFIXES and path.name not in {"LICENSE", "Makefile"}:
            failures.append(f"unclassified release file: {relative}")
            continue
        try:
            data = path.read_bytes()
        except OSError as error:
            failures.append(f"cannot read {relative}: {error}")
            continue
        if b"\x00" in data:
            failures.append(f"binary content in source tree: {relative}")
            continue
        for pattern in PRIVATE_PATHS:
            if pattern.search(data):
                failures.append(f"private filesystem path in {relative}")
                break

    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".")
    args = parser.parse_args()
    failures = check_tree(Path(args.root).resolve())
    if failures:
        for failure in failures:
            print(f"check-release-tree: {failure}", file=sys.stderr)
        return 1
    print("check-release-tree: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
