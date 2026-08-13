# Architecture

## Ownership

liboot owns one local Link simulation. SM64CoopDX owns the level, camera,
objects, transitions, and network transport. `MarioState[0]` is a proxy for
camera, interaction, warp, and network code; its normal geometry is hidden
while the bridge is active.

```text
controller + camera
        |
        v
 liboot Link step -----> copied world-space display lists
        |                         |
        v                         v
 local Mario proxy          CoopDX renderer
        |
        +-----------> CoopDX network state

liboot mixer --------> SPSC ring --------> CoopDX S16 output
```

There is no native plug-in boundary in stock CoopDX. The installer adds the
bridge sources and a small set of calls to the pinned host tree. Static liboot
linking keeps the bridge in the same toolchain and runtime as CoopDX.

## Boot and shutdown

The Lua frontend asks the bridge to validate liboot engine API 1, reads the file named
by `SL64_ROM_PATH`, and calls `oot_engine_create`. liboot copies the ROM bytes;
the bridge frees its read buffer immediately. Link is created only after the
current SM64 collision world commits successfully.

Lua reinitialization and process exit call `sl64_shutdown`. Shutdown clears
host actors and dynamic collision, drains the audio ring, frees copied frame
data, and destroys the engine. The bridge never changes the Mario graph flag;
local proxy suppression exists only inside the render traversal.

## Coordinate conversion

SM64 and liboot use opposite handedness in this bridge:

```text
sm64 = (-oot.x, oot.y, oot.z) * scale
```

Reflection reverses triangle winding. The same conversion is used for
collision, Link state, camera-relative input, host actors, contacts, and render
vertices. Collision conversion rejects non-finite values, degenerate triangles,
and values outside signed 16-bit coordinates.

## World import

On level initialization the bridge walks CoopDX's static surface partitions,
deduplicates shared partition entries, converts surface presets, and commits a
single liboot static world. Water regions are converted in the same transaction.
Any invalid or over-budget surface rejects the whole level; the bridge never
loads a first-N subset of the floor.

CoopDX rebuilds dynamic surfaces during each object update. The host hooks copy
those already-transformed world-space triangles, group them by owner object,
and update liboot after the rebuild finishes. Link therefore sees the previous
completed dynamic frame during its current tick. Geometry changes recreate the
native dynamic object at the identity transform. This gives collision at the
current location but does not reproduce SM64 platform displacement.

## Update and interactions

`HOOK_BEFORE_MARIO_UPDATE` marks local player zero as the Link proxy. CoopDX
then updates geometry inputs and host interactions. `HOOK_MARIO_UPDATE`
advances liboot after that host work, scans live SM64 object-pool entries for a
conservative hostile interaction mask, and finally restores Link's position,
velocity, and facing to the proxy.

CoopDX calls the hooks at 30 Hz; liboot's PAL step is 60 ms. Accumulator calls
that do not complete a native step recopy the current borrowed frame with its
interpolation fraction. The bridge applies a presentation-only velocity offset
to the proxy and Link geometry, while authoritative liboot state remains on the
60 ms step. Input bits remain latched until the next completed frame.

The Lua custom action prevents ordinary Mario movement. CoopDX's existing
pre-physics hook returns a zero step only while that proxy action is active.
Cutscene-group actions remain host-owned so their timers and transitions can
complete. Each same-area host-action frame freezes liboot, adopts Mario's
position and facing, captures the pose, resets the fixed-step clock, and
resumes Link without advancing Link movement or writing back into MarioState.
Mario's host-owned velocity and action fields therefore continue untouched. A
final resync writes the Link proxy when the host action ends. Level and area
changes still rebuild the world and Link through normal level initialization.

Link weapon contacts are translated to SM64 attack and interaction status bits.
Common SM64 damaging interactions call `oot_engine_link_damage`; Mario's second
health drain is then cleared. This is an explicit compatibility policy, not a
conversion of every SM64 combat rule.

## Rendering

liboot emits de-indexed world-space triangles. The bridge copies every borrowed
array, reflects positions and winding, scales them into SM64 units, and emits
vertex-cache chunks of at most ten triangles. Opaque and transparent lists are
submitted under CoopDX's camera matrix, outside Mario's object transform. Host
render state is saved and loaded around each Link pass.

Decoded RGBA32 textures are cached by revision. Images up to 2,048 texels use
CoopDX's load-block path; larger images fall back to vertex shading until the
bridge has a tiled uploader. The bridge clears CoopDX-only geometry modes at
each batch and uses stable textured or shaded host combiners with liboot's
baked vertex colors and material alpha. The remaining blend, depth, decal, and
billboard behavior is approximate.

## Audio

Only the game thread calls liboot. It renders signed 16-bit stereo at 32 kHz
into a fixed single-producer/single-consumer ring. CoopDX's audio path consumes
the ring immediately before playback with saturating addition. Atomic cursors,
enable state, reset requests, and underrun/overrun counters keep shutdown and
mute transitions from resetting a cursor under an active consumer.

SFX callback data is copied into a game-thread queue. The current mapping uses
center pan because the bridge has no listener transform contract.

## Network boundary

CoopDX sends its ordinary Mario proxy state. The bridge does not send the ROM
path, ROM bytes, texture bytes, geometry buffers, PCM, or a native component.
It also does not send enough state to reproduce Link remotely. Each client can
run its own local Link. The local renderer suppresses only player zero's Mario
object and leaves its networked render flag untouched, so remote players remain
normal CoopDX proxies until a versioned Link snapshot and animation protocol
exists.
