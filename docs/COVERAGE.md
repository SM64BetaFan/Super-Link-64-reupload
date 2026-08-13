# Bridge coverage

`wired` means the pinned local build calls the path. `partial` means it is
usable with the stated loss. `status only` means data is shown but does not
drive host behavior. `unused` means liboot has the API but this bridge does not
call it.

| Area | State | Current mapping |
| --- | --- | --- |
| Engine lifecycle and limits | wired | One checked engine; exact API version; shutdown on Lua reload and exit. |
| PAL 1.1 ROM input | wired | Read from local `SL64_ROM_PATH`; liboot copies the bytes during create. |
| Fixed-step input | wired | CoopDX's 30 Hz logic passes elapsed time to liboot's 60 ms accumulator. |
| Age, equipment, selected item, magic | wired | Age-aware Lua controls call checked native setters and persist the loadout. |
| Link health, action, animation | status only | Shown in the Link state menu; no OoT HUD or remote replication. |
| SM64 damage to Link | partial | Common damaging interactions map one host damage point to four liboot units; their Mario damage and knockback are suppressed. Unsupported interaction types remain host-owned. |
| Static collision | wired | Deduplicated and converted at level load; intangible surfaces are skipped and any invalid or over-budget input rejects the level. Directional force is left to CoopDX. |
| Water boxes | partial | Imported at level load; later SM64 water changes are not refreshed. |
| Dynamic collision | partial | World-space triangles are rebuilt after the object update. Capacity failure disables the level. No platform carry. |
| Static-object collision | partial | Tangible SOC surfaces present at level import are captured. Later additions, removals, or intangible toggles are not refreshed. |
| Link geometry | wired | Borrowed frame data is copied, reflected, scaled to host units, and chunked into CoopDX display lists. |
| Material batches | partial | Pass, culling, alpha test, baked color/material alpha, and small RGBA32 textures are used. Blend/depth/decal fidelity is incomplete. |
| Navi | partial | Wing geometry follows the mesh path; the glowing body sprite is absent. |
| Arrow, bomb, boomerang, hookshot geometry | wired | Included through liboot's actor render flag. SM64 collision remains contact-based. |
| Host actors and targeting | partial | A conservative set of live hostile objects is synchronized each tick. Unsupported behavior types remain ordinary SM64 objects. |
| Link weapon contacts | partial | Contacts set common SM64 attacked/interacted flags and attack categories. Per-enemy damage rules stay in SM64 behavior code. |
| Stereo mixer | wired | Game thread produces 32 kHz S16 into a single-producer ring; CoopDX mixes it before playback. |
| Positional SFX | partial | Catalog events are played through liboot with centered pan. Lifetime and attenuation are approximate. |
| Multiplayer | partial | CoopDX networks the Mario proxy. Remote Link meshes, animation correction, and deterministic resimulation are absent. |
| Link pose and freeze | wired | Spawn and same-area host cutscenes synchronize render pose without changing host motion; the final handoff writes the proxy. Level changes recreate Link. |
| Surface query | unused | No proxy floor-material mapping. |
| Actor snapshots | unused | Host actors are the only actor bridge. |
| Ocarina and song matcher | unused | No input or HUD flow. |
| Enemy battle music controls | unused | No bridge setting. |
| ROM scenes and rooms | unused | Loading a ROM scene would replace the SM64 custom world. |
| Scene actors, exits, doors, environment | unused | CoopDX remains authoritative for these systems. |
| Scene backgrounds and animated materials | unused | No matching compositor or material animation path. |
| Exact save state | unavailable | liboot has no complete snapshot/restore contract. |

The local proxy is suppressed at CoopDX's object-render entry without changing
its networked graph flags. This also skips local-Mario render and animation
hooks while Link is visible; mods that depend on those hooks may be
incompatible. Host visibility still controls the Link pass, so transitions
that hide Mario also hide the cached Link mesh.

## Local test gates

The combined pinned host has compiled and completed a headless local-server
startup with a PAL 1.1 OoT ROM. That smoke test covered ROM validation, native
binding, mod loading, level entry, collision import, and sustained update-loop
startup. It did not observe graphics, controls, combat, audio output, or a
second client.

Before calling the bridge playable, verify at least:

- startup, level entry, area warp, instant warp, death, and Lua reload;
- ground, air, water, slopes, ledges, moving platforms, and out-of-bounds;
- sword, shield, targeting, each supported projectile, and several enemy
  interaction types;
- opaque, cutout, transparent, large-texture fallback, and render-state cleanup;
- audio enable/disable, mute, focus loss, shutdown, underrun, and long sessions;
- collision limit rejection with synthetic oversized inputs; and
- two clients, with a clear check that no ROM path or ROM-derived byte is sent.

Passing those checks would not clear the legal blockers in
`../RELEASE_BLOCKERS.md`.
