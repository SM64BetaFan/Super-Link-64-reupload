/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#ifndef SL64_INTERNAL_H
#define SL64_INTERNAL_H

#include "sl64_native.h"

#include "../core/sl64_core.h"

#include <liboot_engine.h>

#include <stdatomic.h>

#define SL64_ERROR_CAPACITY 256u
#define SL64_SFX_QUEUE_CAPACITY 64u
#define SL64_AUDIO_RING_FRAMES 16384u
#define SL64_AUDIO_RING_MASK (SL64_AUDIO_RING_FRAMES - 1u)
#define SL64_AUDIO_RATE 32000u
#define SL64_MAX_DYNAMIC_OBJECTS OOT_DYNAMIC_COLLISION_MAX_OBJECTS
#define SL64_MAX_HOST_ACTORS OOT_ENGINE_MAX_HOST_ACTORS

typedef struct Sl64TextureCache
{
    uint8_t *rgba;
    size_t rgbaSize;
    uint32_t revision;
    uint16_t width;
    uint16_t height;
    uint8_t wrapS;
    uint8_t wrapT;
    uint8_t valid;
} Sl64TextureCache;

typedef struct Sl64RenderCache
{
    float *position;
    float *normal;
    float *color;
    float *uv;
    float *alpha;
    uint16_t *triTexture;
    uint8_t *triFlags;
    struct OoTGeometryBatch *batches;
    uint32_t triangleCapacity;
    uint32_t triangleCount;
    uint32_t batchCapacity;
    uint32_t batchCount;
    Sl64TextureCache *textures;
    uint32_t textureCount;
} Sl64RenderCache;

typedef struct Sl64DynamicSlot
{
    const struct Object *object;
    struct OoTSurface *surfaces;
    uint32_t surfaceCount;
    uint32_t surfaceCapacity;
    uint64_t geometryHash;
    OoTDynamicCollision handle;
    uint8_t seen;
} Sl64DynamicSlot;

typedef struct Sl64ActorSlot
{
    uint64_t userTag;
    struct Object *object;
    OoTEngineHostActor handle;
    uint8_t used;
    uint8_t seen;
} Sl64ActorSlot;

typedef struct Sl64SfxQueue
{
    struct OoTSfxEvent events[SL64_SFX_QUEUE_CAPACITY];
    uint32_t readIndex;
    uint32_t writeIndex;
    uint32_t dropped;
} Sl64SfxQueue;

typedef struct Sl64AudioRing
{
    int16_t samples[SL64_AUDIO_RING_FRAMES * 2u];
    atomic_uint_least32_t readIndex;
    atomic_uint_least32_t writeIndex;
    atomic_uint_least32_t underruns;
    atomic_uint_least32_t overruns;
    atomic_uchar enabled;
    atomic_uchar resetRequested;
    uint32_t producerRemainder;
} Sl64AudioRing;

typedef struct Sl64State
{
    OoTEngine *engine;
    OoTEngineLimits limits;
    Sl64CoordinateMap coordinateMap;
    Sl64NativeStatus status;
    char lastError[SL64_ERROR_CAPACITY];
    Sl64RenderCache render;
    Sl64DynamicSlot dynamic[SL64_MAX_DYNAMIC_OBJECTS];
    Sl64ActorSlot actors[SL64_MAX_HOST_ACTORS];
    Sl64SfxQueue sfx;
    Sl64AudioRing audio;
    uint32_t proxyAction;
    uint8_t dynamicCollecting;
    uint8_t dynamicCollectionFailed;
} Sl64State;

extern Sl64State gSl64;

void sl64_initialize_defaults(void);
void sl64_clear_error(void);
bool sl64_fail(OoTResult result, const char *operation);
bool sl64_result(OoTResult result, const char *operation);
void sl64_debug_callback(void *userData, const char *message);
void sl64_sfx_callback(void *userData, const struct OoTSfxEvent *event);

bool sl64_world_load_static(void);
void sl64_world_reset_dynamic(void);
void sl64_world_shutdown(void);

bool sl64_render_capture(const OoTEngineFrame *frame);
void sl64_render_reset(void);

void sl64_actor_reset(void);
bool sl64_actor_sync_objects(void);
void sl64_actor_apply_contacts(void);

void sl64_audio_reset(void);
void sl64_audio_shutdown(void);
void sl64_audio_tick(void);

static inline float sl64_clampf(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

#endif
