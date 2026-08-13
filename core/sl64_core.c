/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_core.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

static bool sl64_map_valid(const Sl64CoordinateMap *map)
{
    return map != NULL && isfinite(map->hostUnitsPerOotUnit) &&
           map->hostUnitsPerOotUnit > 0.0f;
}

bool sl64_rom_header_is_pal_1_1(const uint8_t *rom, size_t romSize)
{
    static const uint8_t z64Magic[4] = { 0x80u, 0x37u, 0x12u, 0x40u };
    static const uint8_t v64Magic[4] = { 0x37u, 0x80u, 0x40u, 0x12u };
    static const uint8_t n64Magic[4] = { 0x40u, 0x12u, 0x37u, 0x80u };
    static const char title[] = "THE LEGEND OF ZELDA";
    static const char gameCode[] = "NZLP";
    uint8_t order;
    size_t index;

    if (rom == NULL || romSize < 0x40u) {
        return false;
    }
    if (memcmp(rom, z64Magic, sizeof(z64Magic)) == 0) {
        order = 0u;
    } else if (memcmp(rom, v64Magic, sizeof(v64Magic)) == 0) {
        order = 1u;
    } else if (memcmp(rom, n64Magic, sizeof(n64Magic)) == 0) {
        order = 3u;
    } else {
        return false;
    }
    for (index = 0u; index < sizeof(title) - 1u; ++index) {
        if (rom[(0x20u + index) ^ order] != (uint8_t)title[index]) {
            return false;
        }
    }
    for (index = 0u; index < sizeof(gameCode) - 1u; ++index) {
        if (rom[(0x3bu + index) ^ order] != (uint8_t)gameCode[index]) {
            return false;
        }
    }
    return rom[0x3fu ^ order] == 1u;
}

static bool sl64_vec3_valid(const Sl64Vec3 *value)
{
    return value != NULL && isfinite(value->x) && isfinite(value->y) &&
           isfinite(value->z);
}

static bool sl64_float_from_double(double value, float *outValue)
{
    if (outValue == NULL || !isfinite(value) || value < -(double)FLT_MAX ||
        value > (double)FLT_MAX) {
        return false;
    }
    *outValue = (float)value;
    return isfinite(*outValue);
}

static float sl64_clamp_stick(float value)
{
    if (value < -1.0f) {
        return -1.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static bool sl64_preset_valid(uint16_t preset)
{
    return preset < (uint16_t)OOT_SURFACE_PRESET_COUNT;
}

static bool sl64_rules_valid(const Sl64SurfaceRule *rules, size_t ruleCount,
                             uint16_t fallbackPreset)
{
    size_t index;

    if (!sl64_preset_valid(fallbackPreset) ||
        (ruleCount != 0u && rules == NULL)) {
        return false;
    }
    for (index = 0u; index < ruleCount; ++index) {
        if (!sl64_preset_valid(rules[index].ootPreset)) {
            return false;
        }
    }
    return true;
}

static uint16_t sl64_lookup_preset(uint32_t hostCategory,
                                   const Sl64SurfaceRule *rules,
                                   size_t ruleCount,
                                   uint16_t fallbackPreset)
{
    size_t index;

    for (index = 0u; index < ruleCount; ++index) {
        if (rules[index].hostCategory == hostCategory) {
            return rules[index].ootPreset;
        }
    }
    return fallbackPreset;
}

static bool sl64_round_oot_coordinate(double value, int32_t *outValue)
{
    double rounded;

    if (outValue == NULL || !isfinite(value)) {
        return false;
    }
    rounded = round(value);
    if (rounded < (double)INT16_MIN || rounded > (double)INT16_MAX) {
        return false;
    }
    *outValue = (int32_t)rounded;
    return true;
}

static bool sl64_triangle_non_degenerate(const struct OoTSurface *surface)
{
    int64_t ax = (int64_t)surface->vertices[1][0] - surface->vertices[0][0];
    int64_t ay = (int64_t)surface->vertices[1][1] - surface->vertices[0][1];
    int64_t az = (int64_t)surface->vertices[1][2] - surface->vertices[0][2];
    int64_t bx = (int64_t)surface->vertices[2][0] - surface->vertices[0][0];
    int64_t by = (int64_t)surface->vertices[2][1] - surface->vertices[0][1];
    int64_t bz = (int64_t)surface->vertices[2][2] - surface->vertices[0][2];
    int64_t crossX = ay * bz - az * by;
    int64_t crossY = az * bx - ax * bz;
    int64_t crossZ = ax * by - ay * bx;

    return crossX != 0 || crossY != 0 || crossZ != 0;
}

bool sl64_coordinate_map_init(Sl64CoordinateMap *map,
                              float hostUnitsPerOotUnit)
{
    Sl64CoordinateMap next;

    if (map == NULL || !isfinite(hostUnitsPerOotUnit) ||
        hostUnitsPerOotUnit <= 0.0f) {
        return false;
    }
    next.hostUnitsPerOotUnit = hostUnitsPerOotUnit;
    *map = next;
    return true;
}

bool sl64_oot_position_to_host(const Sl64CoordinateMap *map,
                               const Sl64Vec3 *ootPosition,
                               Sl64Vec3 *outHostPosition)
{
    Sl64Vec3 converted;
    double scale;

    if (!sl64_map_valid(map) || !sl64_vec3_valid(ootPosition) ||
        outHostPosition == NULL) {
        return false;
    }
    scale = map->hostUnitsPerOotUnit;
    if (!sl64_float_from_double(-(double)ootPosition->x * scale,
                                &converted.x) ||
        !sl64_float_from_double((double)ootPosition->y * scale,
                                &converted.y) ||
        !sl64_float_from_double((double)ootPosition->z * scale,
                                &converted.z)) {
        return false;
    }
    *outHostPosition = converted;
    return true;
}

bool sl64_host_position_to_oot(const Sl64CoordinateMap *map,
                               const Sl64Vec3 *hostPosition,
                               Sl64Vec3 *outOotPosition)
{
    Sl64Vec3 converted;
    double scale;

    if (!sl64_map_valid(map) || !sl64_vec3_valid(hostPosition) ||
        outOotPosition == NULL) {
        return false;
    }
    scale = map->hostUnitsPerOotUnit;
    if (!sl64_float_from_double(-(double)hostPosition->x / scale,
                                &converted.x) ||
        !sl64_float_from_double((double)hostPosition->y / scale,
                                &converted.y) ||
        !sl64_float_from_double((double)hostPosition->z / scale,
                                &converted.z)) {
        return false;
    }
    *outOotPosition = converted;
    return true;
}

bool sl64_oot_direction_to_host(const Sl64Vec3 *ootDirection,
                                Sl64Vec3 *outHostDirection)
{
    Sl64Vec3 converted;

    if (!sl64_vec3_valid(ootDirection) || outHostDirection == NULL) {
        return false;
    }
    converted.x = -ootDirection->x;
    converted.y = ootDirection->y;
    converted.z = ootDirection->z;
    *outHostDirection = converted;
    return true;
}

bool sl64_host_direction_to_oot(const Sl64Vec3 *hostDirection,
                                Sl64Vec3 *outOotDirection)
{
    return sl64_oot_direction_to_host(hostDirection, outOotDirection);
}

int16_t sl64_oot_yaw_to_host(int16_t ootYaw)
{
    /* Negating INT16_MIN cannot be represented as a positive int16_t. Under
       the reflected binary-angle convention it is its own opposite. */
    if (ootYaw == INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)-ootYaw;
}

int16_t sl64_host_yaw_to_oot(int16_t hostYaw)
{
    return sl64_oot_yaw_to_host(hostYaw);
}

bool sl64_input_to_oot(const Sl64HostInput *hostInput,
                       OoTEngineInput *outInput)
{
    OoTEngineInput converted;
    double cameraLength;
    uint32_t buttons;

    if (hostInput == NULL || outInput == NULL ||
        !sl64_vec3_valid(&hostInput->cameraToPlayer) ||
        !isfinite(hostInput->moveRight) ||
        !isfinite(hostInput->moveForward)) {
        return false;
    }

    memset(&converted, 0, sizeof(converted));
    converted.structSize = (uint32_t)sizeof(converted);
    cameraLength = sqrt((double)hostInput->cameraToPlayer.x *
                            hostInput->cameraToPlayer.x +
                        (double)hostInput->cameraToPlayer.z *
                            hostInput->cameraToPlayer.z);
    if (cameraLength > 0.00001) {
        converted.camLookX = (float)(-(double)hostInput->cameraToPlayer.x /
                                     cameraLength);
        converted.camLookZ = (float)((double)hostInput->cameraToPlayer.z /
                                     cameraLength);
    } else {
        converted.camLookZ = 1.0f;
    }
    /* Host X is reflected into OoT space, so camera-relative lateral input
     * must be reflected with it. Leaving this positive makes screen-right
     * input move Link screen-left after the output transform. */
    converted.stickX = -sl64_clamp_stick(hostInput->moveRight);
    converted.stickY = sl64_clamp_stick(hostInput->moveForward);

    buttons = hostInput->buttons;
    if ((buttons & SL64_HOST_BUTTON_JUMP) != 0u) {
        converted.buttons |= OOT_ENGINE_BUTTON_A;
    }
    if ((buttons & SL64_HOST_BUTTON_ATTACK) != 0u) {
        converted.buttons |= OOT_ENGINE_BUTTON_B;
    }
    if ((buttons & SL64_HOST_BUTTON_TARGET) != 0u) {
        converted.buttons |= OOT_ENGINE_BUTTON_Z;
    }
    if ((buttons & SL64_HOST_BUTTON_GUARD) != 0u) {
        converted.buttons |= OOT_ENGINE_BUTTON_R;
    }
    if ((buttons & SL64_HOST_BUTTON_ITEM) != 0u) {
        converted.buttons |= OOT_ENGINE_BUTTON_ITEM;
    }
    if ((buttons & SL64_HOST_BUTTON_LOOK) != 0u) {
        converted.buttons |= OOT_ENGINE_BUTTON_CUP;
    }

    *outInput = converted;
    return true;
}

bool sl64_surface_preset_for_category(uint32_t hostCategory,
                                      const Sl64SurfaceRule *rules,
                                      size_t ruleCount,
                                      uint16_t fallbackPreset,
                                      uint16_t *outPreset)
{
    if (outPreset == NULL ||
        !sl64_rules_valid(rules, ruleCount, fallbackPreset)) {
        return false;
    }
    *outPreset = sl64_lookup_preset(hostCategory, rules, ruleCount,
                                    fallbackPreset);
    return true;
}

bool sl64_triangle_to_oot(const Sl64CoordinateMap *map,
                          const Sl64HostTriangle *hostTriangle,
                          uint16_t ootPreset,
                          struct OoTSurface *outSurface)
{
    static const uint8_t reflectedOrder[3] = { 0u, 2u, 1u };
    struct OoTSurface converted;
    size_t outputVertex;
    size_t axis;
    double scale;

    if (!sl64_map_valid(map) || hostTriangle == NULL || outSurface == NULL ||
        !sl64_preset_valid(ootPreset)) {
        return false;
    }
    memset(&converted, 0, sizeof(converted));
    converted.type = ootPreset;
    scale = map->hostUnitsPerOotUnit;

    for (outputVertex = 0u; outputVertex < 3u; ++outputVertex) {
        size_t inputVertex = reflectedOrder[outputVertex];
        for (axis = 0u; axis < 3u; ++axis) {
            double value = (double)hostTriangle->vertices[inputVertex][axis];
            if (axis == 0u) {
                value = -value;
            }
            if (!sl64_round_oot_coordinate(
                    value / scale,
                    &converted.vertices[outputVertex][axis])) {
                return false;
            }
        }
    }
    if (!sl64_triangle_non_degenerate(&converted)) {
        return false;
    }
    *outSurface = converted;
    return true;
}

bool sl64_triangles_to_oot(const Sl64CoordinateMap *map,
                           const Sl64HostTriangle *hostTriangles,
                           size_t triangleCount,
                           const Sl64SurfaceRule *rules,
                           size_t ruleCount,
                           uint16_t fallbackPreset,
                           struct OoTSurface *outSurfaces)
{
    size_t index;

    if (!sl64_map_valid(map) || hostTriangles == NULL || outSurfaces == NULL ||
        triangleCount == 0u ||
        triangleCount > (size_t)OOT_ENGINE_MAX_STATIC_SURFACES ||
        !sl64_rules_valid(rules, ruleCount, fallbackPreset)) {
        return false;
    }

    /* Validate the whole transaction before touching caller output. */
    for (index = 0u; index < triangleCount; ++index) {
        struct OoTSurface probe;
        uint16_t preset = sl64_lookup_preset(
            hostTriangles[index].surfaceCategory, rules, ruleCount,
            fallbackPreset);
        if (!sl64_triangle_to_oot(map, &hostTriangles[index], preset, &probe)) {
            return false;
        }
    }
    for (index = 0u; index < triangleCount; ++index) {
        uint16_t preset = sl64_lookup_preset(
            hostTriangles[index].surfaceCategory, rules, ruleCount,
            fallbackPreset);
        (void)sl64_triangle_to_oot(map, &hostTriangles[index], preset,
                                   &outSurfaces[index]);
    }
    return true;
}

bool sl64_water_box_to_oot(const Sl64CoordinateMap *map,
                           const Sl64HostWaterBox *hostWater,
                           struct OoTWaterBox *outWater)
{
    struct OoTWaterBox converted;
    int32_t xA;
    int32_t xB;
    int32_t zA;
    int32_t zB;
    int32_t y;
    int32_t xMin;
    int32_t xMax;
    int32_t zMin;
    int32_t zMax;
    int64_t xLength;
    int64_t zLength;
    double scale;

    if (!sl64_map_valid(map) || hostWater == NULL || outWater == NULL ||
        !isfinite(hostWater->xMin) || !isfinite(hostWater->xMax) ||
        !isfinite(hostWater->zMin) || !isfinite(hostWater->zMax) ||
        !isfinite(hostWater->ySurface) || hostWater->xMin >= hostWater->xMax ||
        hostWater->zMin >= hostWater->zMax) {
        return false;
    }
    scale = map->hostUnitsPerOotUnit;
    if (!sl64_round_oot_coordinate(-(double)hostWater->xMin / scale, &xA) ||
        !sl64_round_oot_coordinate(-(double)hostWater->xMax / scale, &xB) ||
        !sl64_round_oot_coordinate((double)hostWater->zMin / scale, &zA) ||
        !sl64_round_oot_coordinate((double)hostWater->zMax / scale, &zB) ||
        !sl64_round_oot_coordinate((double)hostWater->ySurface / scale, &y)) {
        return false;
    }

    xMin = xA < xB ? xA : xB;
    xMax = xA > xB ? xA : xB;
    zMin = zA < zB ? zA : zB;
    zMax = zA > zB ? zA : zB;
    xLength = (int64_t)xMax - xMin;
    zLength = (int64_t)zMax - zMin;
    if (xLength <= 0 || xLength > INT16_MAX || zLength <= 0 ||
        zLength > INT16_MAX) {
        return false;
    }

    memset(&converted, 0, sizeof(converted));
    converted.xMin = (int16_t)xMin;
    converted.zMin = (int16_t)zMin;
    converted.xLength = (int16_t)xLength;
    converted.zLength = (int16_t)zLength;
    converted.ySurface = (int16_t)y;
    *outWater = converted;
    return true;
}

bool sl64_water_boxes_to_oot(const Sl64CoordinateMap *map,
                             const Sl64HostWaterBox *hostWater,
                             size_t waterCount,
                             struct OoTWaterBox *outWater)
{
    size_t index;

    if (!sl64_map_valid(map) ||
        waterCount > (size_t)OOT_ENGINE_MAX_WATER_BOXES ||
        (waterCount != 0u && (hostWater == NULL || outWater == NULL))) {
        return false;
    }
    if (waterCount == 0u) {
        return true;
    }
    for (index = 0u; index < waterCount; ++index) {
        struct OoTWaterBox probe;
        if (!sl64_water_box_to_oot(map, &hostWater[index], &probe)) {
            return false;
        }
    }
    for (index = 0u; index < waterCount; ++index) {
        (void)sl64_water_box_to_oot(map, &hostWater[index], &outWater[index]);
    }
    return true;
}
