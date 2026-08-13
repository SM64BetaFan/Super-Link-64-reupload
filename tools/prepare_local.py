#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Super Link 64 contributors

"""Stage or reversibly install Super Link 64 into a local CoopDX checkout."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


PINNED_COOPDX = "8cd6e5977d9f920d51ca71f2c61801d019ed79c6"
BRIDGE_DESTINATION = Path("src/pc/super_link_64")
MOD_DESTINATION = Path("mods/super-link-64")
CONTROL_DIRECTORY = Path(".super-link-64-local")
MANIFEST_NAME = "manifest.json"
MANIFEST_FORMAT = 2
HOOK_MARKER = "SUPER LINK 64 LOCAL HOOK"


class PrepareError(RuntimeError):
    pass


def git_output(checkout: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(checkout), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise PrepareError(result.stderr.strip() or "git command failed")
    return result.stdout.strip()


def validate_coopdx(checkout: Path, allow_dirty: bool, allow_revision: bool) -> None:
    if not (checkout / "Makefile").is_file() or not (checkout / "src/pc/lua/smlua.c").is_file():
        raise PrepareError("--coopdx is not an SM64CoopDX source checkout")
    revision = git_output(checkout, "rev-parse", "HEAD")
    if revision != PINNED_COOPDX and not allow_revision:
        raise PrepareError(
            f"SM64CoopDX revision {revision} is not the tested pin {PINNED_COOPDX}"
        )
    if git_output(checkout, "status", "--porcelain") and not allow_dirty:
        raise PrepareError("SM64CoopDX checkout is dirty; use --allow-dirty only after review")


def validate_liboot(prefix: Path) -> None:
    header = prefix / "include/liboot/liboot_engine.h"
    library = prefix / "lib/liboot.a"
    if not header.is_file():
        raise PrepareError("liboot_engine.h is missing from --liboot-prefix")
    if not library.is_file():
        raise PrepareError("--liboot-prefix must contain the static library lib/liboot.a")


def copy_tree(source: Path, destination: Path) -> None:
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination)


def write_plan(stage: Path) -> None:
    plan = stage / "LOCAL_INTEGRATION.txt"
    plan.write_text(
        "\n".join(
            (
                "Super Link 64 local integration plan",
                f"Required CoopDX commit: {PINNED_COOPDX}",
                "The overlay contains only original Super Link 64 files.",
                "Run prepare_local.py again with --install to copy the overlay and add the pinned hooks.",
                "Build in the CoopDX checkout with SUPER_LINK_64=1 and LIBOOT_PREFIX set.",
                "Set SL64_ROM_PATH only in the launch environment.",
                "Use --uninstall to restore the backed-up host files.",
                "Do not redistribute the resulting host tree or executable.",
                "",
            )
        ),
        encoding="utf-8",
    )


def stage_original_files(project: Path, stage: Path) -> None:
    if stage.is_symlink() or (stage.exists() and not stage.is_dir()):
        raise PrepareError("--stage must name a new or empty ordinary directory")
    if stage.exists() and any(stage.iterdir()):
        raise PrepareError("--stage directory is not empty")
    stage.mkdir(parents=True, exist_ok=True)
    overlay = stage / "overlay"
    copy_tree(project / "native", overlay / BRIDGE_DESTINATION / "native")
    copy_tree(project / "core", overlay / BRIDGE_DESTINATION / "core")
    copy_tree(project / "mod/super-link-64", overlay / MOD_DESTINATION)
    write_plan(stage)


def unique_line(
    lines: list[str], relative: Path, selector: str, predicate
) -> int:
    matches = [index for index, line in enumerate(lines) if predicate(line)]
    if len(matches) != 1:
        raise PrepareError(
            f"pinned hook selector {selector!r} matched {len(matches)} lines "
            f"in {relative}; refusing to guess"
        )
    return matches[0]


def host_insertion_position(text: str, relative: Path, selector: str) -> int:
    """Locate a pinned hook without embedding the host's implementation text."""

    lines = text.splitlines(keepends=True)
    predicates = {
        "make-sources": lambda line: line.strip() == "# Source code files",
        "include-fmem": lambda line: "pc/fs/fmem.h" in line,
        "smlua-bind": lambda line: "smlua_init_require_system()" in line,
        "smlua-shutdown": lambda line: line.strip().startswith(
            "void smlua_shutdown(void)"
        ),
        "include-lua-hooks": lambda line: "pc/lua/smlua_hooks.h" in line,
        "surface-hook": lambda line: (
            "HOOK_ON_ADD_SURFACE" in line and "smlua_call_event_hooks" in line
        ),
        "include-djui-hud": lambda line: "pc/djui/djui_hud_utils.h" in line,
        "object-update-end": lambda line: "update_non_terrain_objects()" in line,
        "include-hardcoded": lambda line: line.strip().endswith('"hardcoded.h"'),
        "render-object": lambda line: line.strip() == (
            "static void geo_process_object(struct Object *node) {"
        ),
        "render-camera-close": lambda line: (
            line.strip() == "gCurGraphNodeCamera = NULL;"
        ),
        "include-math-util": lambda line: "engine/math_util.h" in line,
        "audio-play": lambda line: "gAudioApi->play(" in line,
        "make-link": lambda line: line.strip() == "LDFLAGS += -latomic",
        "object-dynamic-clear": lambda line: (
            line.strip() == "clear_dynamic_surfaces();"
        ),
    }
    if selector not in predicates:
        raise PrepareError(f"unknown pinned hook selector: {selector}")

    if selector == "object-dynamic-clear":
        matches = [
            index
            for index, line in enumerate(lines)
            if predicates[selector](line)
            and any("cycleCounts[1]" in prior for prior in lines[max(0, index - 3):index])
        ]
        if len(matches) != 1:
            raise PrepareError(
                f"pinned hook selector {selector!r} matched {len(matches)} lines "
                f"in {relative}; refusing to guess"
            )
        index = matches[0]
    else:
        index = unique_line(lines, relative, selector, predicates[selector])

    insert_after = selector not in {
        "make-sources",
        "render-camera-close",
        "audio-play",
    }
    if selector == "make-link":
        closing = next(
            (
                candidate
                for candidate in range(index + 1, len(lines))
                if lines[candidate].startswith("endif")
            ),
            None,
        )
        if closing is None:
            raise PrepareError(f"cannot find Makefile block end after {selector!r}")
        index = closing
    elif selector == "surface-hook":
        closing = next(
            (
                candidate
                for candidate in range(index + 1, len(lines))
                if lines[candidate].startswith("    }")
            ),
            None,
        )
        if closing is None:
            raise PrepareError(f"cannot find surface-hook block end in {relative}")
        index = closing

    offset = sum(len(line) for line in lines[:index])
    if insert_after:
        offset += len(lines[index])
    return offset


def host_insertions() -> tuple[tuple[Path, str, str], ...]:
    include = '#ifdef SUPER_LINK_64\n#include "pc/super_link_64/native/sl64_native.h"\n#endif\n'
    return (
        (
            Path("Makefile"),
            "make-sources",
            "# SUPER LINK 64 LOCAL HOOK: original local source integration\n"
            "ifeq ($(SUPER_LINK_64),1)\n"
            "  SL64_MAKE_PHASE := sources\n"
            "  include src/pc/super_link_64/native/super_link_64.mk\n"
            "endif\n\n",
        ),
        (
            Path("Makefile"),
            "make-link",
            "\n# SUPER LINK 64 LOCAL HOOK: link the static bridge after platform flags\n"
            "ifeq ($(SUPER_LINK_64),1)\n"
            "  SL64_MAKE_PHASE := link\n"
            "  include src/pc/super_link_64/native/super_link_64.mk\n"
            "endif\n",
        ),
        (
            Path("src/pc/lua/smlua.c"),
            "include-fmem",
            include,
        ),
        (
            Path("src/pc/lua/smlua.c"),
            "smlua-bind",
            "#ifdef SUPER_LINK_64\n"
            "    /* SUPER LINK 64 LOCAL HOOK */\n"
            "    sl64_lua_bind(L);\n"
            "#endif\n",
        ),
        (
            Path("src/pc/lua/smlua.c"),
            "smlua-shutdown",
            "#ifdef SUPER_LINK_64\n"
            "    /* SUPER LINK 64 LOCAL HOOK: safe across Lua reloads. */\n"
            "    sl64_shutdown();\n"
            "#endif\n",
        ),
        (
            Path("src/engine/surface_load.c"),
            "include-lua-hooks",
            include,
        ),
        (
            Path("src/engine/surface_load.c"),
            "surface-hook",
            "#ifdef SUPER_LINK_64\n"
            "    /* SUPER LINK 64 LOCAL HOOK */\n"
            "    sl64_on_surface_added(surface, surface->poolType == SURFACE_POOL_DYNAMIC);\n"
            "#endif\n",
        ),
        (
            Path("src/game/object_list_processor.c"),
            "include-djui-hud",
            include,
        ),
        (
            Path("src/game/object_list_processor.c"),
            "object-dynamic-clear",
            "\n#ifdef SUPER_LINK_64\n"
            "    /* SUPER LINK 64 LOCAL HOOK */\n"
            "    bool sl64DynamicFrame = !(gTimeStopState & TIME_STOP_ACTIVE);\n"
            "    if (sl64DynamicFrame) { sl64_dynamic_begin_frame(); }\n"
            "#endif\n\n",
        ),
        (
            Path("src/game/object_list_processor.c"),
            "object-update-end",
            "#ifdef SUPER_LINK_64\n"
            "    /* SUPER LINK 64 LOCAL HOOK */\n"
            "    if (sl64DynamicFrame) { sl64_dynamic_end_frame(); }\n"
            "#endif\n",
        ),
        (
            Path("src/game/rendering_graph_node.c"),
            "include-hardcoded",
            include,
        ),
        (
            Path("src/game/rendering_graph_node.c"),
            "render-object",
            "#ifdef SUPER_LINK_64\n"
            "    /* SUPER LINK 64 LOCAL HOOK: keep the network proxy active. */\n"
            "    if (sl64_should_hide_mario_proxy(node)) { return; }\n"
            "#endif\n",
        ),
        (
            Path("src/game/rendering_graph_node.c"),
            "render-camera-close",
            "#ifdef SUPER_LINK_64\n"
            "    /* SUPER LINK 64 LOCAL HOOK: liboot vertices are world-space. */\n"
            "    struct GraphNode sl64Node = { 0 };\n"
            "    Gfx *sl64List = sl64_geo_render_opaque(\n"
            "        GEO_CONTEXT_RENDER, &sl64Node, gMatStack[gMatStackIndex]);\n"
            "    if (sl64List != NULL) {\n"
            "        geo_append_display_list(obj_save_gfx_state, LAYER_OPAQUE);\n"
            "        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(sl64List), LAYER_OPAQUE);\n"
            "        geo_append_display_list(obj_load_gfx_state, LAYER_OPAQUE);\n"
            "    }\n"
            "    sl64List = sl64_geo_render_translucent(\n"
            "        GEO_CONTEXT_RENDER, &sl64Node, gMatStack[gMatStackIndex]);\n"
            "    if (sl64List != NULL) {\n"
            "        geo_append_display_list(obj_save_gfx_state, LAYER_TRANSPARENT);\n"
            "        geo_append_display_list((void *) VIRTUAL_TO_PHYSICAL(sl64List), LAYER_TRANSPARENT);\n"
            "        geo_append_display_list(obj_load_gfx_state, LAYER_TRANSPARENT);\n"
            "    }\n"
            "#endif\n\n",
        ),
        (
            Path("src/pc/pc_main.c"),
            "include-math-util",
            include,
        ),
        (
            Path("src/pc/pc_main.c"),
            "audio-play",
            "#ifdef SUPER_LINK_64\n"
            "        /* SUPER LINK 64 LOCAL HOOK */\n"
            "        sl64_audio_mix_s16(sAudioBuffer, 2u * numAudioSamples, gMasterVolume);\n"
            "#endif\n",
        ),
    )


def digest_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_manifest_path(name: object) -> Path:
    if not isinstance(name, str) or not name:
        raise PrepareError("installation manifest contains an invalid path")
    relative = Path(name)
    if relative.is_absolute() or relative == Path(".") or ".." in relative.parts:
        raise PrepareError("installation manifest contains a path outside the checkout")
    return relative


def valid_digest(value: object) -> bool:
    if not isinstance(value, str) or len(value) != 64:
        return False
    try:
        int(value, 16)
    except ValueError:
        return False
    return True


def validate_manifest(
    manifest: object,
) -> tuple[list[Path], dict[Path, str], dict[Path, str], dict[Path, str]]:
    manifest_fields = {
        "format",
        "coopdx_commit",
        "host_files",
        "installed_roots",
        "installed_hashes",
        "host_hashes",
        "backup_hashes",
    }
    if (
        not isinstance(manifest, dict)
        or set(manifest) != manifest_fields
        or manifest.get("format") != MANIFEST_FORMAT
        or manifest.get("coopdx_commit") != PINNED_COOPDX
        or manifest.get("installed_roots")
        != [str(BRIDGE_DESTINATION), str(MOD_DESTINATION)]
    ):
        raise PrepareError("unsupported local installation manifest")

    raw_host_files = manifest.get("host_files")
    raw_installed_hashes = manifest.get("installed_hashes")
    raw_host_hashes = manifest.get("host_hashes")
    raw_backup_hashes = manifest.get("backup_hashes")
    if (
        not isinstance(raw_host_files, list)
        or not isinstance(raw_installed_hashes, dict)
        or not isinstance(raw_host_hashes, dict)
        or not isinstance(raw_backup_hashes, dict)
    ):
        raise PrepareError("local installation manifest has invalid fields")

    allowed_host_files = {entry[0] for entry in host_insertions()}
    host_files: list[Path] = []
    for name in raw_host_files:
        relative = safe_manifest_path(name)
        if relative not in allowed_host_files or relative in host_files:
            raise PrepareError("local installation manifest names an unexpected host file")
        host_files.append(relative)

    installed_hashes: dict[Path, str] = {}
    for name, digest in raw_installed_hashes.items():
        relative = safe_manifest_path(name)
        if (
            not valid_digest(digest)
            or not any(root in relative.parents for root in (BRIDGE_DESTINATION, MOD_DESTINATION))
        ):
            raise PrepareError("local installation manifest has an invalid installed-file entry")
        installed_hashes[relative] = digest

    host_hashes: dict[Path, str] = {}
    for name, digest in raw_host_hashes.items():
        relative = safe_manifest_path(name)
        if relative not in allowed_host_files or not valid_digest(digest):
            raise PrepareError("local installation manifest has an invalid host-file entry")
        host_hashes[relative] = digest

    if set(host_files) != set(host_hashes):
        raise PrepareError("local installation manifest host-file lists disagree")

    backup_hashes: dict[Path, str] = {}
    for name, digest in raw_backup_hashes.items():
        relative = safe_manifest_path(name)
        if relative not in allowed_host_files or not valid_digest(digest):
            raise PrepareError("local installation manifest has an invalid backup entry")
        backup_hashes[relative] = digest
    if set(host_files) != set(backup_hashes):
        raise PrepareError("local installation manifest backup lists disagree")
    return host_files, installed_hashes, host_hashes, backup_hashes


def current_installed_paths(checkout: Path) -> set[Path]:
    paths: set[Path] = set()
    for root in (BRIDGE_DESTINATION, MOD_DESTINATION):
        target = checkout / root
        if target.exists():
            paths.update(
                path.relative_to(checkout)
                for path in target.rglob("*")
                if path.is_file()
            )
    return paths


def current_installed_directories(checkout: Path) -> set[Path]:
    directories: set[Path] = set()
    for root in (BRIDGE_DESTINATION, MOD_DESTINATION):
        target = checkout / root
        if target.is_dir() and not target.is_symlink():
            directories.add(root)
            directories.update(
                path.relative_to(checkout)
                for path in target.rglob("*")
                if path.is_dir() and not path.is_symlink()
            )
    return directories


def unsafe_installed_entries(checkout: Path) -> set[Path]:
    unsafe: set[Path] = set()
    for root in (BRIDGE_DESTINATION, MOD_DESTINATION):
        target = checkout / root
        if target.is_symlink() or (target.exists() and not target.is_dir()):
            unsafe.add(root)
            continue
        if not target.exists():
            continue
        for path in target.rglob("*"):
            if path.is_symlink() or (not path.is_file() and not path.is_dir()):
                unsafe.add(path.relative_to(checkout))
    return unsafe


def collect_hashes(root: Path, relative_roots: tuple[Path, ...]) -> dict[str, str]:
    hashes: dict[str, str] = {}
    for relative_root in relative_roots:
        target = root / relative_root
        for path in sorted(target.rglob("*")):
            if path.is_file():
                hashes[str(path.relative_to(root))] = digest_file(path)
    return hashes


def apply_host_patches(checkout: Path, backup: Path) -> tuple[Path, ...]:
    changed: list[Path] = []
    patched: dict[Path, str] = {}
    for relative, selector, insertion in host_insertions():
        path = checkout / relative
        if relative not in changed:
            if not path.is_file():
                raise PrepareError(f"required pinned host file is missing: {relative}")
            backup_path = backup / relative
            backup_path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, backup_path)
            changed.append(relative)
            patched[relative] = path.read_text(encoding="utf-8")
        text = patched[relative]
        if HOOK_MARKER in insertion and insertion in text:
            raise PrepareError(f"Super Link 64 hook is already present in {relative}")
        position = host_insertion_position(text, relative, selector)
        patched[relative] = text[:position] + insertion + text[position:]
    for relative in changed:
        (checkout / relative).write_text(patched[relative], encoding="utf-8")
    return tuple(changed)


def restore_backups(checkout: Path, control: Path, manifest: dict[str, object] | None) -> None:
    backup = control / "backup"
    if control.is_symlink() or not control.is_dir() or backup.is_symlink():
        raise PrepareError("local installation control directory is unsafe")
    host_files: list[Path] = []
    backup_hashes: dict[Path, str] = {}
    if manifest is not None:
        host_files, _, _, backup_hashes = validate_manifest(manifest)
    if not host_files and backup.exists():
        host_files = [path.relative_to(backup) for path in backup.rglob("*") if path.is_file()]
    invalid = [
        relative
        for relative in host_files
        if (
            (backup / relative).is_symlink()
            or not (backup / relative).is_file()
            or (
                relative in backup_hashes
                and digest_file(backup / relative) != backup_hashes[relative]
            )
        )
    ]
    if invalid:
        raise PrepareError(
            "cannot restore missing or changed host backup: "
            + ", ".join(map(str, invalid[:5]))
        )
    for relative in host_files:
        source = backup / relative
        destination = checkout / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    for relative in (BRIDGE_DESTINATION, MOD_DESTINATION):
        target = checkout / relative
        if target.is_symlink():
            target.unlink()
        elif target.exists():
            shutil.rmtree(target)
    if control.exists():
        shutil.rmtree(control)


def install_staged(stage: Path, checkout: Path) -> None:
    overlay = stage / "overlay"
    control = checkout / CONTROL_DIRECTORY
    if control.exists() or control.is_symlink():
        raise PrepareError("Super Link 64 is already installed in this checkout")
    for relative in (BRIDGE_DESTINATION, MOD_DESTINATION):
        destination = checkout / relative
        if destination.exists() or destination.is_symlink():
            raise PrepareError(f"install destination already exists: {relative}")
        if not (overlay / relative).is_dir():
            raise PrepareError(f"staged overlay is missing: {relative}")

    backup = control / "backup"
    backup.mkdir(parents=True)
    manifest: dict[str, object] | None = None
    installed_roots = (BRIDGE_DESTINATION, MOD_DESTINATION)
    try:
        staged_hashes = collect_hashes(overlay, installed_roots)
        host_files = apply_host_patches(checkout, backup)
        copy_tree(overlay / BRIDGE_DESTINATION, checkout / BRIDGE_DESTINATION)
        copy_tree(overlay / MOD_DESTINATION, checkout / MOD_DESTINATION)
        installed_hashes = collect_hashes(checkout, installed_roots)
        if installed_hashes != staged_hashes:
            raise PrepareError("installed files do not match the staged overlay")
        host_hashes = {str(path): digest_file(checkout / path) for path in host_files}
        backup_hashes = {str(path): digest_file(backup / path) for path in host_files}
        manifest = {
            "format": MANIFEST_FORMAT,
            "coopdx_commit": PINNED_COOPDX,
            "host_files": [str(path) for path in host_files],
            "installed_roots": [str(path) for path in installed_roots],
            "installed_hashes": installed_hashes,
            "host_hashes": host_hashes,
            "backup_hashes": backup_hashes,
        }
        (control / MANIFEST_NAME).write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    except Exception:
        restore_backups(checkout, control, manifest)
        raise


def uninstall_local(checkout: Path, force: bool) -> None:
    control = checkout / CONTROL_DIRECTORY
    if control.is_symlink() or not control.is_dir():
        raise PrepareError("this checkout has no safe Super Link 64 installation")
    manifest_path = control / MANIFEST_NAME
    if not manifest_path.is_file():
        raise PrepareError("this checkout has no reversible Super Link 64 installation")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise PrepareError(f"cannot read the local installation manifest: {error}") from error

    host_files, installed_hashes, host_hashes, _ = validate_manifest(manifest)
    expected = {**installed_hashes, **host_hashes}
    changed: list[str] = []
    for relative, digest in expected.items():
        path = checkout / relative
        if path.is_symlink() or not path.is_file() or digest_file(path) != digest:
            changed.append(str(relative))
    unexpected = current_installed_paths(checkout) - set(installed_hashes)
    changed.extend(str(relative) for relative in sorted(unexpected))
    expected_directories = {BRIDGE_DESTINATION, MOD_DESTINATION}
    for relative in installed_hashes:
        expected_directories.update(relative.parents)
    unexpected_directories = (
        current_installed_directories(checkout) - expected_directories
    )
    changed.extend(str(relative) for relative in sorted(unexpected_directories))
    changed.extend(str(relative) for relative in sorted(unsafe_installed_entries(checkout)))
    if changed and not force:
        joined = ", ".join(changed[:5])
        raise PrepareError(
            f"installed files changed ({joined}); preserve them or use --force-uninstall"
        )
    restore_backups(checkout, control, manifest)


def paths_overlap(first: Path, second: Path) -> bool:
    return first == second or first in second.parents or second in first.parents


def validate_stage(
    stage: Path, project: Path, checkout: Path, prefix: Path | None
) -> None:
    protected = (project, checkout) if prefix is None else (project, checkout, prefix)
    if (
        stage == Path("/")
        or any(paths_overlap(stage, path) for path in protected)
        or not stage.name.startswith(("super-link-64", "sl64"))
    ):
        raise PrepareError(
            "--stage must be a dedicated super-link-64 directory outside the project"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--coopdx", type=Path, required=True)
    parser.add_argument("--liboot-prefix", type=Path)
    parser.add_argument(
        "--stage",
        type=Path,
        default=None,
    )
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--install", action="store_true")
    modes.add_argument("--uninstall", action="store_true")
    parser.add_argument("--force-uninstall", action="store_true")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--allow-revision", action="store_true")
    args = parser.parse_args()
    if args.force_uninstall and not args.uninstall:
        parser.error("--force-uninstall requires --uninstall")

    project = Path(__file__).resolve().parents[1]
    checkout = args.coopdx.resolve()
    stage: Path | None = None
    temporary_stage = False
    completed = False
    try:
        if args.uninstall:
            validate_coopdx(checkout, True, args.allow_revision)
            uninstall_local(checkout, args.force_uninstall)
            print("prepare-local: restored the pinned CoopDX checkout")
            return 0

        if args.liboot_prefix is None:
            raise PrepareError("--liboot-prefix is required unless --uninstall is used")
        prefix = args.liboot_prefix.resolve()
        if args.stage is not None:
            stage = args.stage.resolve()
        else:
            stage = Path(tempfile.mkdtemp(prefix="super-link-64-"))
            temporary_stage = True
        validate_coopdx(checkout, args.allow_dirty, args.allow_revision)
        validate_liboot(prefix)
        validate_stage(stage, project, checkout, prefix)
        stage_original_files(project, stage)
        if args.install:
            install_staged(stage, checkout)
        completed = True
    except (OSError, PrepareError) as error:
        print(f"prepare-local: {error}", file=sys.stderr)
        return 1
    finally:
        if (temporary_stage and stage is not None and
                (args.install or not completed)):
            shutil.rmtree(stage, ignore_errors=True)
    if args.install:
        print("prepare-local: installed reversible hooks in the local checkout")
    else:
        print(f"prepare-local: staged original files in {stage}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
