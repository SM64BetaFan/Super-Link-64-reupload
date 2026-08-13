/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#ifndef SL64_NATIVE_H
#define SL64_NATIVE_H

#include <PR/gbi.h>
#include <PR/ultratypes.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SL64_NATIVE_API_VERSION 2u
#define SL64_COOPDX_VERSION "v1.5.1"
#define SL64_COOPDX_COMMIT "8cd6e5977d9f920d51ca71f2c61801d019ed79c6"
#define SL64_ROM_ENVIRONMENT_VARIABLE "SL64_ROM_PATH"

struct GraphNode;
struct MarioState;
struct Object;
struct Surface;
typedef struct lua_State lua_State;

enum Sl64NativeCapabilities
{
    SL64_CAP_LOCAL_LINK             = UINT64_C(1) << 0,
    SL64_CAP_STATIC_COLLISION       = UINT64_C(1) << 1,
    SL64_CAP_WATER                  = UINT64_C(1) << 2,
    SL64_CAP_DYNAMIC_WORLD_BAKED    = UINT64_C(1) << 3,
    SL64_CAP_HOST_ACTORS            = UINT64_C(1) << 4,
    SL64_CAP_HOST_CONTACTS          = UINT64_C(1) << 5,
    SL64_CAP_RENDER_BATCHES         = UINT64_C(1) << 6,
    SL64_CAP_RENDER_RGBA32_TEXTURES = UINT64_C(1) << 7,
    SL64_CAP_AUDIO_S16_RING         = UINT64_C(1) << 8,
    SL64_CAP_LUA_GLOBALS            = UINT64_C(1) << 9
};

typedef struct Sl64NativeStatus
{
    uint32_t structSize;
    uint32_t apiVersion;
    uint64_t capabilityFlags;
    uint64_t libootCapabilityFlags;
    int32_t lastResult;
    uint8_t available;
    uint8_t booted;
    uint8_t enabled;
    uint8_t worldReady;
    uint8_t linkReady;
    uint8_t renderReady;
    uint8_t audioEnabled;
    uint8_t reserved0;
    int32_t localIndex;
    int32_t globalIndex;
    uint64_t simulationTick;
    uint32_t triangles;
    uint32_t batches;
    uint32_t droppedTriangles;
    uint32_t staticSurfaces;
    uint32_t waterBoxes;
    uint32_t dynamicObjects;
    uint32_t hostActors;
    uint32_t contactsApplied;
    uint32_t textureFallbacks;
    uint32_t audioFramesQueued;
    uint32_t audioUnderruns;
    uint32_t audioOverruns;
    int16_t linkHealth;
    int16_t linkHealthCapacity;
    int16_t linkMagic;
    int16_t linkAnimId;
    uint32_t linkAction;
    uint8_t linkAge;
    uint8_t linkDead;
    uint8_t linkInWater;
    uint8_t linkLockOn;
} Sl64NativeStatus;

typedef struct Sl64HostActor
{
    uint64_t userTag;
    uint32_t typeId;
    uint32_t flags;
    float position[3];
    float focusOffset[3];
    float hurtRadius;
    float hurtHeight;
    float hurtYOffset;
    int16_t rotation[3];
    int16_t room;
    uint8_t attentionRange;
    uint8_t reserved[3];
} Sl64HostActor;

typedef struct Sl64ActorContact
{
    uint64_t userTag;
    uint32_t source;
    uint32_t sourceActorId;
    uint64_t simulationTick;
    float position[3];
} Sl64ActorContact;

/* The zero-argument boot path intentionally reads only SL64_ROM_PATH. ROM
 * bytes are copied by liboot and are never retained by this bridge. */
bool sl64_available(void);
bool sl64_boot(void);
bool sl64_boot_from_path(const char *romPath);
bool sl64_level_init(void);
/* Call exactly once from CoopDX's 30 Hz logic update, not from interpolated
 * rendering or the audio callback. The pinned v1.5.1 logic step is fixed. */
bool sl64_tick(int32_t localIndex, int32_t globalIndex);
/* Adopt a position produced by a host-owned door, star, pipe, or cutscene
 * before returning control to liboot. This also rebuilds the cached frame. */
bool sl64_sync_link_from_mario(int32_t localIndex);
/* Rebuild Link at the host pose for rendering during a host-owned action,
 * without writing position or motion back into MarioState. */
bool sl64_sync_link_render_from_mario(int32_t localIndex);
void sl64_shutdown(void);

/* The host keeps its local Mario graph node network-visible. The pinned
 * renderer calls this before drawing an object so only the local view can
 * suppress that proxy after Link geometry is ready. */
bool sl64_should_hide_mario_proxy(const struct Object *object);

const char *sl64_last_error(void);
bool sl64_get_status(Sl64NativeStatus *outStatus);
bool sl64_set_enabled(bool enabled);
bool sl64_set_age(uint8_t age);
bool sl64_set_item(uint8_t item);
bool sl64_set_magic(uint8_t level, int16_t amount);
bool sl64_damage_link(int16_t amount);
bool sl64_set_equipment(uint8_t sword, uint8_t shield,
                        uint8_t tunic, uint8_t boots);
bool sl64_set_audio_enabled(bool enabled);
bool sl64_set_proxy_action(uint32_t action);

/* Optional upstream physics gate. Set the allocated Lua proxy action first.
 * If this returns true, the installer should return *stepResultOverride from
 * the intercepted Mario physics function. */
bool sl64_before_phys_step(struct MarioState *mario, s32 stepType,
                           u32 stepArg, s32 *stepResultOverride);

/* Static collision is scanned by sl64_level_init. Dynamic surfaces require
 * three small hooks around CoopDX's per-frame dynamic surface rebuild. */
void sl64_dynamic_begin_frame(void);
void sl64_on_surface_added(const struct Surface *surface, bool dynamic);
void sl64_dynamic_end_frame(void);

/* Generic actor registration is the authoritative integration path. The
 * Object helpers are a conservative convenience for ordinary SM64 enemies. */
bool sl64_host_actor_upsert(const Sl64HostActor *actor);
bool sl64_host_actor_remove(uint64_t userTag);
void sl64_host_actors_clear(void);
bool sl64_host_actor_from_object(struct Object *object);
void sl64_on_object_unload(struct Object *object);
bool sl64_poll_contact(Sl64ActorContact *outContact);

/* Place two GEO_ASM nodes under an identity/root transform. Link geometry is
 * already world-space and must not inherit Mario's object transform. */
Gfx *sl64_geo_render_opaque(s32 callContext, struct GraphNode *node,
                            void *context);
Gfx *sl64_geo_render_translucent(s32 callContext, struct GraphNode *node,
                                 void *context);

/* Produce only on the game/liboot owner thread. Mix may run on CoopDX's audio
 * thread and never calls liboot. Both counts are stereo frames, not samples. */
uint32_t sl64_audio_produce(uint32_t frames, uint32_t sampleRate);
uint32_t sl64_audio_mix_s16(int16_t *stereo, uint32_t frames, float gain);

/* Call once from smlua_init after CoopDX installs its standard globals. */
void sl64_lua_bind(lua_State *luaState);

#ifdef __cplusplus
}
#endif

#endif
