/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_internal.h"

#include "engine/surface_load.h"
#include "game/object_list_processor.h"
#include "surface_terrains.h"

#include <stdlib.h>
#include <string.h>

static uint16_t sl64_surface_type(const struct Surface *surface)
{
    if (surface_has_force(surface->type)) {
        /* CoopDX force encodes direction and strength differently. Mapping it
         * as a fixed OoT conveyor would apply a second, incorrect +Z force. */
        return OOT_SURFACE_NO_HOOKSHOT;
    }
    switch (surface->type) {
        case SURFACE_BURNING:
            return OOT_SURFACE_DAMAGE;
        case SURFACE_DEATH_PLANE:
        case SURFACE_VERTICAL_WIND:
            /* CoopDX owns void-out and wind transitions. Import their shape,
             * but do not also turn them into immediate OoT damage floors. */
            return OOT_SURFACE_NO_HOOKSHOT;
        case SURFACE_SHALLOW_QUICKSAND:
        case SURFACE_DEEP_QUICKSAND:
        case SURFACE_INSTANT_QUICKSAND:
        case SURFACE_DEEP_MOVING_QUICKSAND:
        case SURFACE_SHALLOW_MOVING_QUICKSAND:
        case SURFACE_QUICKSAND:
        case SURFACE_MOVING_QUICKSAND:
        case SURFACE_INSTANT_MOVING_QUICKSAND:
            return OOT_SURFACE_SAND;
        case SURFACE_VERY_SLIPPERY:
        case SURFACE_SLIPPERY:
        case SURFACE_NOISE_SLIPPERY:
        case SURFACE_ICE:
        case SURFACE_HARD_SLIPPERY:
        case SURFACE_HARD_VERY_SLIPPERY:
        case SURFACE_NOISE_VERY_SLIPPERY_73:
        case SURFACE_NOISE_VERY_SLIPPERY_74:
        case SURFACE_NOISE_VERY_SLIPPERY:
        case SURFACE_NO_CAM_COL_VERY_SLIPPERY:
        case SURFACE_NO_CAM_COL_SLIPPERY:
            return OOT_SURFACE_SLIPPERY;
        case SURFACE_TTM_VINES:
            return OOT_SURFACE_CLIMB_WALL;
        default:
            /* SM64 has no general hookshot material bit. Defaulting to
             * NO_HOOKSHOT avoids granting traversal through arbitrary walls. */
            return OOT_SURFACE_NO_HOOKSHOT;
    }
}

static bool sl64_convert_surface(const struct Surface *surface,
                                 struct OoTSurface *outSurface)
{
    Sl64HostTriangle triangle;
    size_t vertex;
    size_t axis;

    if (surface == NULL || outSurface == NULL ||
        (surface->flags & SURFACE_FLAG_INTANGIBLE) != 0u) {
        return false;
    }
    memset(&triangle, 0, sizeof(triangle));
    triangle.surfaceCategory = (uint32_t)(uint16_t)surface->type;
    for (vertex = 0u; vertex < 3u; ++vertex) {
        const Vec3s *source = vertex == 0u ? &surface->vertex1 :
                              (vertex == 1u ? &surface->vertex2 :
                                             &surface->vertex3);
        for (axis = 0u; axis < 3u; ++axis) {
            triangle.vertices[vertex][axis] = (*source)[axis];
        }
    }
    return sl64_triangle_to_oot(&gSl64.coordinateMap, &triangle,
                                sl64_surface_type(surface), outSurface);
}

static bool sl64_seen_surface(const struct Surface *surface,
                              const struct Surface *const *seen,
                              uint32_t count)
{
    uint32_t index;

    for (index = 0u; index < count; ++index) {
        if (seen[index] == surface) {
            return true;
        }
    }
    return false;
}

static uint32_t sl64_scan_static(struct OoTSurface *outSurfaces,
                                 uint32_t capacity, uint32_t *outDropped)
{
    uint32_t estimate;
    const struct Surface **seen;
    uint32_t seenCount = 0u;
    uint32_t outputCount = 0u;
    uint32_t dropped = 0u;
    int cellX;
    int cellZ;
    int partition;

    estimate = (uint32_t)(gNumStaticSurfaces > 0 ? gNumStaticSurfaces : 0) +
               (uint32_t)(gNumSOCSurfaces > 0 ? gNumSOCSurfaces : 0) + 16u;
    if (estimate > OOT_ENGINE_MAX_STATIC_SURFACES * 2u) {
        estimate = OOT_ENGINE_MAX_STATIC_SURFACES * 2u;
    }
    seen = (const struct Surface **)calloc(estimate, sizeof(*seen));
    if (seen == NULL) {
        *outDropped = 0u;
        return 0u;
    }
    for (cellX = 0; cellX < NUM_CELLS; ++cellX) {
        for (cellZ = 0; cellZ < NUM_CELLS; ++cellZ) {
            for (partition = 0; partition < 3; ++partition) {
                struct SurfaceNode *node =
                    gStaticSurfacePartition[cellX][cellZ][partition].next;
                while (node != NULL) {
                    const struct Surface *surface = node->surface;
                    node = node->next;
                    if (surface == NULL ||
                        sl64_seen_surface(surface, seen, seenCount)) {
                        continue;
                    }
                    if (seenCount >= estimate) {
                        dropped++;
                        continue;
                    }
                    seen[seenCount++] = surface;
                    if ((surface->flags & SURFACE_FLAG_INTANGIBLE) != 0u) {
                        continue;
                    }
                    if (outputCount >= capacity ||
                        !sl64_convert_surface(surface,
                                              &outSurfaces[outputCount])) {
                        dropped++;
                        continue;
                    }
                    outputCount++;
                }
            }
        }
    }
    free(seen);
    *outDropped = dropped;
    return outputCount;
}

static uint32_t sl64_scan_water(struct OoTWaterBox *outWater,
                                uint32_t capacity, uint32_t *outDropped)
{
    const s16 *cursor;
    uint32_t regionCount;
    uint32_t available;
    uint32_t outputCount = 0u;
    uint32_t dropped = 0u;
    uint32_t index;

    if (gEnvironmentRegions == NULL || gEnvironmentRegionsLength < 1) {
        *outDropped = 0u;
        return 0u;
    }
    cursor = gEnvironmentRegions;
    regionCount = (uint32_t)(*cursor++ < 0 ? 0 : gEnvironmentRegions[0]);
    available = (uint32_t)(gEnvironmentRegionsLength - 1) / 6u;
    if (regionCount > available) {
        regionCount = available;
    }
    for (index = 0u; index < regionCount; ++index, cursor += 6) {
        Sl64HostWaterBox hostWater;
        int16_t kind = cursor[0];

        if (kind >= 50) {
            continue; /* gas/fog regions are not water */
        }
        hostWater.xMin = (float)cursor[1];
        hostWater.zMin = (float)cursor[2];
        hostWater.xMax = (float)cursor[3];
        hostWater.zMax = (float)cursor[4];
        hostWater.ySurface = (float)cursor[5];
        if (outputCount >= capacity ||
            !sl64_water_box_to_oot(&gSl64.coordinateMap, &hostWater,
                                   &outWater[outputCount])) {
            dropped++;
            continue;
        }
        outputCount++;
    }
    *outDropped = dropped;
    return outputCount;
}

bool sl64_world_load_static(void)
{
    struct OoTSurface *surfaces;
    struct OoTWaterBox *water;
    uint32_t surfaceCapacity;
    uint32_t waterCapacity;
    uint32_t surfaceCount;
    uint32_t waterCount;
    uint32_t surfaceDropped;
    uint32_t waterDropped;
    OoTResult result;

    if (gSl64.engine == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "loading CoopDX collision");
    }
    surfaceCapacity = gSl64.limits.staticSurfaceCapacity;
    if (surfaceCapacity == 0u ||
        surfaceCapacity > OOT_ENGINE_MAX_STATIC_SURFACES) {
        surfaceCapacity = OOT_ENGINE_MAX_STATIC_SURFACES;
    }
    waterCapacity = gSl64.limits.waterBoxCapacity;
    if (waterCapacity == 0u || waterCapacity > 20u) {
        waterCapacity = 20u; /* CoopDX caps imported environment regions at 20 */
    }
    surfaces = (struct OoTSurface *)calloc(surfaceCapacity, sizeof(*surfaces));
    water = waterCapacity != 0u ?
            (struct OoTWaterBox *)calloc(waterCapacity, sizeof(*water)) : NULL;
    if (surfaces == NULL || (waterCapacity != 0u && water == NULL)) {
        free(surfaces);
        free(water);
        return sl64_fail(OOT_ENGINE_RESULT_OUT_OF_MEMORY,
                         "allocating CoopDX collision import");
    }
    surfaceCount = sl64_scan_static(surfaces, surfaceCapacity,
                                    &surfaceDropped);
    waterCount = sl64_scan_water(water, waterCapacity, &waterDropped);
    gSl64.status.droppedTriangles = surfaceDropped;
    if (surfaceDropped != 0u || waterDropped != 0u) {
        free(surfaces);
        free(water);
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "CoopDX collision exceeds liboot limits or contains invalid geometry");
    }
    if (surfaceCount == 0u) {
        free(surfaces);
        free(water);
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "CoopDX level has no importable static collision");
    }
    result = oot_engine_static_world_load(
        gSl64.engine, surfaces, surfaceCount,
        waterCount != 0u ? water : NULL, waterCount);
    free(surfaces);
    free(water);
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "committing CoopDX collision to liboot");
    }
    gSl64.status.staticSurfaces = surfaceCount;
    gSl64.status.waterBoxes = waterCount;
    gSl64.status.worldReady = 1u;
    return true;
}

static Sl64DynamicSlot *sl64_dynamic_slot(const struct Object *object,
                                          bool create)
{
    Sl64DynamicSlot *empty = NULL;
    uint32_t index;

    for (index = 0u; index < SL64_MAX_DYNAMIC_OBJECTS; ++index) {
        Sl64DynamicSlot *slot = &gSl64.dynamic[index];
        if (slot->object == object) {
            return slot;
        }
        if (empty == NULL && slot->object == NULL) {
            empty = slot;
        }
    }
    if (create && empty != NULL) {
        empty->object = object;
    }
    return create ? empty : NULL;
}

static uint64_t sl64_hash_surfaces(const struct OoTSurface *surfaces,
                                   uint32_t count)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t surfaceIndex;

    /* Hash fields, not struct bytes: OoTSurface has alignment padding whose
     * value is not part of the collision geometry. */
    for (surfaceIndex = 0u; surfaceIndex < count; ++surfaceIndex) {
        const struct OoTSurface *surface = &surfaces[surfaceIndex];
        size_t vertex;
        size_t axis;

        hash ^= surface->type;
        hash *= UINT64_C(1099511628211);
        for (vertex = 0u; vertex < 3u; ++vertex) {
            for (axis = 0u; axis < 3u; ++axis) {
                uint32_t value = (uint32_t)surface->vertices[vertex][axis];
                unsigned byte;
                for (byte = 0u; byte < 4u; ++byte) {
                    hash ^= (value >> (byte * 8u)) & UINT32_C(0xff);
                    hash *= UINT64_C(1099511628211);
                }
            }
        }
    }
    return hash;
}

void sl64_dynamic_begin_frame(void)
{
    uint32_t index;

    if ((gSl64.status.capabilityFlags & SL64_CAP_DYNAMIC_WORLD_BAKED) == 0u ||
        gSl64.engine == NULL || gSl64.status.worldReady == 0u) {
        return;
    }
    for (index = 0u; index < SL64_MAX_DYNAMIC_OBJECTS; ++index) {
        gSl64.dynamic[index].seen = 0u;
        gSl64.dynamic[index].surfaceCount = 0u;
    }
    gSl64.dynamicCollectionFailed = 0u;
    gSl64.dynamicCollecting = 1u;
}

void sl64_on_surface_added(const struct Surface *surface, bool dynamic)
{
    Sl64DynamicSlot *slot;
    struct OoTSurface converted;
    struct OoTSurface *resized;
    uint32_t total = 0u;
    uint32_t index;

    if (!dynamic || !gSl64.dynamicCollecting) {
        return;
    }
    if (surface != NULL &&
        (surface->flags & SURFACE_FLAG_INTANGIBLE) != 0u) {
        return;
    }
    if (surface == NULL || surface->object == NULL ||
        !sl64_convert_surface(surface, &converted)) {
        gSl64.status.droppedTriangles++;
        gSl64.dynamicCollectionFailed = 1u;
        return;
    }
    for (index = 0u; index < SL64_MAX_DYNAMIC_OBJECTS; ++index) {
        total += gSl64.dynamic[index].surfaceCount;
    }
    if (total >= OOT_DYNAMIC_COLLISION_MAX_SURFACES) {
        gSl64.status.droppedTriangles++;
        gSl64.dynamicCollectionFailed = 1u;
        return;
    }
    slot = sl64_dynamic_slot(surface->object, true);
    if (slot == NULL) {
        gSl64.status.droppedTriangles++;
        gSl64.dynamicCollectionFailed = 1u;
        return;
    }
    if (slot->surfaceCount == slot->surfaceCapacity) {
        uint32_t capacity = slot->surfaceCapacity == 0u ? 8u :
                            slot->surfaceCapacity * 2u;
        resized = (struct OoTSurface *)realloc(
            slot->surfaces, (size_t)capacity * sizeof(*resized));
        if (resized == NULL) {
            gSl64.status.droppedTriangles++;
            gSl64.dynamicCollectionFailed = 1u;
            return;
        }
        slot->surfaces = resized;
        slot->surfaceCapacity = capacity;
    }
    slot->surfaces[slot->surfaceCount++] = converted;
    slot->seen = 1u;
}

void sl64_dynamic_end_frame(void)
{
    struct OoTDynamicCollisionTransform identity =
        OOT_DYNAMIC_COLLISION_TRANSFORM_INIT;
    uint32_t active = 0u;
    uint32_t index;

    if (!gSl64.dynamicCollecting || gSl64.engine == NULL) {
        return;
    }
    gSl64.dynamicCollecting = 0u;
    if (gSl64.dynamicCollectionFailed != 0u) {
        sl64_world_reset_dynamic();
        gSl64.status.worldReady = 0u;
        (void)sl64_fail(OOT_ENGINE_RESULT_DYNAMIC_COLLISION_CAPACITY,
                        "CoopDX moving collision exceeds liboot limits or is invalid");
        return;
    }
    for (index = 0u; index < SL64_MAX_DYNAMIC_OBJECTS; ++index) {
        Sl64DynamicSlot *slot = &gSl64.dynamic[index];
        uint64_t hash;
        OoTResult result;

        if (slot->object == NULL) {
            continue;
        }
        if (!slot->seen || slot->surfaceCount == 0u) {
            if (slot->handle != OOT_DYNAMIC_COLLISION_INVALID) {
                (void)oot_engine_dynamic_collision_delete(gSl64.engine,
                                                           slot->handle);
            }
            free(slot->surfaces);
            memset(slot, 0, sizeof(*slot));
            continue;
        }
        hash = sl64_hash_surfaces(slot->surfaces, slot->surfaceCount);
        if (slot->handle == OOT_DYNAMIC_COLLISION_INVALID ||
            slot->geometryHash != hash) {
            if (slot->handle != OOT_DYNAMIC_COLLISION_INVALID) {
                (void)oot_engine_dynamic_collision_delete(gSl64.engine,
                                                           slot->handle);
                slot->handle = OOT_DYNAMIC_COLLISION_INVALID;
            }
            /* CoopDX exposes already-transformed s16 surface vertices here.
             * They are intentionally imported at identity. Moving platforms
             * collide correctly, but liboot carry flags cannot be promised. */
            result = oot_engine_dynamic_collision_create(
                gSl64.engine, slot->surfaces, slot->surfaceCount, &identity,
                0u, &slot->handle);
            if (result != OOT_ENGINE_RESULT_OK) {
                slot->handle = OOT_DYNAMIC_COLLISION_INVALID;
                gSl64.dynamicCollectionFailed = 1u;
                continue;
            }
            slot->geometryHash = hash;
        }
        active++;
    }
    if (gSl64.dynamicCollectionFailed != 0u) {
        sl64_world_reset_dynamic();
        gSl64.status.worldReady = 0u;
        (void)sl64_fail(OOT_ENGINE_RESULT_DYNAMIC_COLLISION_CAPACITY,
                        "CoopDX moving collision could not be imported completely");
        return;
    }
    gSl64.status.dynamicObjects = active;
}

void sl64_world_reset_dynamic(void)
{
    uint32_t index;

    for (index = 0u; index < SL64_MAX_DYNAMIC_OBJECTS; ++index) {
        Sl64DynamicSlot *slot = &gSl64.dynamic[index];
        if (gSl64.engine != NULL &&
            slot->handle != OOT_DYNAMIC_COLLISION_INVALID) {
            (void)oot_engine_dynamic_collision_delete(gSl64.engine,
                                                       slot->handle);
        }
        free(slot->surfaces);
        memset(slot, 0, sizeof(*slot));
    }
    gSl64.dynamicCollecting = 0u;
    gSl64.dynamicCollectionFailed = 0u;
    gSl64.status.dynamicObjects = 0u;
}

void sl64_world_shutdown(void)
{
    sl64_world_reset_dynamic();
    gSl64.status.staticSurfaces = 0u;
    gSl64.status.waterBoxes = 0u;
    gSl64.status.worldReady = 0u;
}
