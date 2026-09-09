# THIS GIT ISN'T MINE
This project was originally made by Cycl0o0 and recently got deleted, so I reuploaded it.

# Super Link 64

Super Link 64 runs liboot's Link simulation in an SM64CoopDX level. Link is
the local player; CoopDX still owns the level, camera, objects, warps, and
network connection.

This is a source-integration prototype, not a plug-in for the stock CoopDX
executable. CoopDX Lua cannot load liboot, submit its display lists, copy the
collision world, or mix its PCM output. The test build therefore compiles a C
bridge into a pinned CoopDX checkout. Lua handles settings and lifecycle only.

No ROM or extracted Nintendo asset belongs in this directory. The bridge reads
a local Ocarina of Time PAL 1.1 ROM from `SL64_ROM_PATH` at runtime. The ROM is
not copied, logged, or sent over the network.

## What works

The current local build has these paths connected:

- one locally controlled Link, using the CoopDX stick and camera direction;
- static SM64 collision and a level-load snapshot of water regions;
- world-baked moving collision, with strict capacity checks;
- Link, Navi wing, and supported projectile geometry in opaque and transparent
  passes;
- conservative SM64 enemy registration for Z-targeting and Link weapon hits;
- SM64 enemy damage routed into liboot's Link damage function;
- liboot stereo audio mixed into the CoopDX signed 16-bit output;
- an age-aware loadout menu for swords, shields, tunics, boots, items, magic,
  audio, Link state, and world diagnostics; and
- a reversible installer for SM64CoopDX v1.5.1 commit
  `8cd6e5977d9f920d51ca71f2c61801d019ed79c6`.

Position, velocity, and facing are written to the local Mario proxy. Doors,
stars, pipes, teleports, and area transitions remain CoopDX actions. Remote
Link rendering and exact multiplayer correction are not implemented;
other clients receive the ordinary Mario proxy instead.

The supplied PAL 1.1 ROM has passed liboot's checked engine test, static
example, and a headless startup of the combined CoopDX build. Hands-on input,
rendering, combat, transitions, and multiplayer still need play testing; see
`docs/COVERAGE.md` for the exact gaps.

## Local build

You need a local checkout of the pinned CoopDX commit, its normal desktop build
dependencies, a legal SM64 US ROM for that checkout's ordinary asset build,
and a legally obtained Ocarina of Time PAL 1.1 ROM for liboot.

From a checkout of the liboot revision in `dependencies.lock`, install a static
library to a path without spaces:

```sh
SL64_PREFIX=/tmp/super-link-64-liboot
cmake -S /path/to/liboot -B /tmp/super-link-64-liboot-build \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$SL64_PREFIX"
cmake --build /tmp/super-link-64-liboot-build --parallel
cmake --install /tmp/super-link-64-liboot-build
```

Install the bridge into a clean, pinned CoopDX checkout:

```sh
python3 tools/prepare_local.py \
  --coopdx /path/to/sm64coopdx \
  --liboot-prefix "$SL64_PREFIX" \
  --install
```

The installer verifies the commit, backs up every edited host file under
`.super-link-64-local`, copies the bridge and Lua frontend, and applies only the
known v1.5.1 hooks. It stops when an expected source anchor differs. Use
`--allow-dirty` only after reviewing the checkout yourself.

Build through CoopDX's normal desktop target:

```sh
make -C /path/to/sm64coopdx \
  SUPER_LINK_64=1 \
  LIBOOT_PREFIX="$SL64_PREFIX" \
  -j4
```

Set the OoT ROM path only for the process that launches the local build:

```sh
export SL64_ROM_PATH="/absolute/path/to/oot-pal-1.1.z64"
```

The installed Lua directory is `/path/to/sm64coopdx/mods/super-link-64`.
Either copy it to CoopDX's normal user mod directory, or launch a source-tree
test with the checkout as its save path and enable the directory explicitly:

```sh
/path/to/sm64coopdx/build/us_pc/sm64coopdx \
  --savepath /path/to/sm64coopdx \
  --enable-mod super-link-64
```

The macOS executable is inside the generated app bundle instead. A failed ROM
check, collision import, or native limit is shown in the CoopDX popup console.

Restore the host checkout when finished:

```sh
python3 tools/prepare_local.py \
  --coopdx /path/to/sm64coopdx \
  --uninstall
```

Uninstall refuses to overwrite bridge or host files changed after installation.
Preserve those edits first, or pass `--force-uninstall` deliberately.

## Controls

| CoopDX input | Link input |
| --- | --- |
| Left stick | Move |
| A | OoT A action |
| B | Sword/action |
| Z | Target |
| R | Shield |
| X | Selected item |
| C-Up | First-person look |

The default loadout is adult Link, Master Sword, Hylian Shield, Kokiri Tunic,
Kokiri Boots, the Fairy Bow, and a full single magic meter. The menu only
cycles through choices valid for the selected age and applies each change
immediately.

## Known limits

- liboot accepts at most 2,730 static triangles, signed 16-bit collision
  coordinates, 50 dynamic objects, 512 dynamic triangles, and 512 unique
  dynamic vertices. Super Link 64 disables the level if an import would be
  incomplete.
- Moving surfaces are rebuilt from world-space SM64 triangles. Link collides
  with their current shape, but platform carry is not reproduced.
- Water is captured at level load. Levels that change their water boxes during
  play can become stale.
- Static-object collision is a level-load snapshot. Surfaces marked intangible
  are skipped, but later SOC additions, removals, or visibility toggles are not
  refreshed until the level is reloaded.
- Directional force surfaces stay CoopDX-owned. The bridge imports their shape
  without applying an OoT conveyor, because the two force encodings differ.
- Enemy classification and hit translation cover common SM64 interaction
  types. This is not a general actor or combat conversion layer.
- One SM64 damage point maps to four liboot damage units, or one quarter-heart.
  Link health and magic are reported through the Link state menu; no OoT-style
  HUD is drawn.
- Burning floors damage Link through liboot while the Mario lava reaction is
  suppressed. CoopDX remains responsible for death-plane and vertical-wind
  transitions. Quicksand imports as sand, but sinking is not reproduced.
- Decoded textures up to 2,048 texels use CoopDX's load-block path. Larger
  images fall back to vertex shading until the bridge has a tiled uploader.
  Custom blend, depth, decal, and combine behavior is approximate. Navi's
  glowing billboard body is not drawn.
- Link SFX are currently centered rather than spatialized. liboot's mixer is a
  native approximation, not bit-exact RSP audio.
- Only local player zero runs liboot. Other players remain CoopDX proxies, and
  no exact Link snapshot/restore contract exists.
- ROM scene loading, scene actors, exits, animated materials, and prerendered
  backgrounds are not used by this bridge.
- PAL 1.1 is the only OoT gameplay revision claimed here. The host pin is the
  only CoopDX revision covered by the installer.

## Checks

The repository-only checks do not need either ROM. They do need the public
headers from the pinned liboot checkout; the default path is the sibling
directory `../liboot-public`:

```sh
make check LIBOOT_SOURCE=/path/to/liboot
```

They cover coordinate and collision conversion, installer behavior, Lua
syntax, release-content policy, and copy-quality scanning. A full host build
and live play session remain separate local tests.

## License and release status

Original files in this directory are offered under `AGPL-3.0-or-later`; see
`LICENSE`. That grant does not relicense either dependency or Nintendo data.

This directory is a source-only AGPL scaffold. The reviewed CoopDX repository
has no repository-wide license, and liboot records unresolved provenance for
selected zeldaret decompilation material. Do not publish the modified host,
generated patch, linked library, combined executable, ROM data, or extracted
assets. `NOTICE.md`, `THIRD_PARTY.yml`, and `RELEASE_BLOCKERS.md` record the
work required before a runnable release can be considered.

Super Link 64 is an independent interoperability experiment. It is not
affiliated with Nintendo, SM64CoopDX, zeldaret, or liboot.
