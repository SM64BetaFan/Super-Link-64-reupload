/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#ifndef SL64_CORE_H
#define SL64_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <liboot_engine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Super Link 64 uses a reflected X axis at the liboot boundary. The scale is
   expressed as host units per one OoT world unit and must be finite and > 0. */
typedef struct Sl64CoordinateMap
{
    float hostUnitsPerOotUnit;
} Sl64CoordinateMap;

typedef struct Sl64Vec3
{
    float x;
    float y;
    float z;
} Sl64Vec3;

#define SL64_COORDINATE_MAP_INIT { 1.0f }

bool sl64_coordinate_map_init(Sl64CoordinateMap *map,
                              float hostUnitsPerOotUnit);
bool sl64_oot_position_to_host(const Sl64CoordinateMap *map,
                               const Sl64Vec3 *ootPosition,
                               Sl64Vec3 *outHostPosition);
bool sl64_host_position_to_oot(const Sl64CoordinateMap *map,
                               const Sl64Vec3 *hostPosition,
                               Sl64Vec3 *outOotPosition);
bool sl64_oot_direction_to_host(const Sl64Vec3 *ootDirection,
                                Sl64Vec3 *outHostDirection);
bool sl64_host_direction_to_oot(const Sl64Vec3 *hostDirection,
                                Sl64Vec3 *outOotDirection);

/* Both coordinate systems face +Z at yaw zero. Reflecting X negates yaw. */
int16_t sl64_oot_yaw_to_host(int16_t ootYaw);
int16_t sl64_host_yaw_to_oot(int16_t hostYaw);

enum Sl64HostButtons
{
    SL64_HOST_BUTTON_JUMP   = 1u << 0,
    SL64_HOST_BUTTON_ATTACK = 1u << 1,
    SL64_HOST_BUTTON_TARGET = 1u << 2,
    SL64_HOST_BUTTON_GUARD  = 1u << 3,
    SL64_HOST_BUTTON_ITEM   = 1u << 4,
    SL64_HOST_BUTTON_LOOK   = 1u << 5
};

/* Host-neutral input. cameraToPlayer is a direction in host coordinates;
   vertical camera pitch is intentionally ignored by liboot. Buttons are held
   levels, not edge-triggered events. */
typedef struct Sl64HostInput
{
    Sl64Vec3 cameraToPlayer;
    float moveRight;
    float moveForward;
    uint32_t buttons;
} Sl64HostInput;

bool sl64_input_to_oot(const Sl64HostInput *hostInput,
                       OoTEngineInput *outInput);

/* The adapter deliberately receives numeric host surface categories rather
   than depending on a host game's private enum. A caller-owned rule table maps
   those values to stable liboot OoTSurfaceType presets. */
typedef struct Sl64SurfaceRule
{
    uint32_t hostCategory;
    uint16_t ootPreset;
} Sl64SurfaceRule;

bool sl64_surface_preset_for_category(uint32_t hostCategory,
                                      const Sl64SurfaceRule *rules,
                                      size_t ruleCount,
                                      uint16_t fallbackPreset,
                                      uint16_t *outPreset);

/* SM64-like collision triangle: integral host-space vertices plus a numeric
   surface category. This is an original interchange type, not an upstream game
   structure. */
typedef struct Sl64HostTriangle
{
    uint32_t surfaceCategory;
    int32_t vertices[3][3];
} Sl64HostTriangle;

/* Converts one triangle to liboot coordinates, reversing vertices 1 and 2 to
   preserve the outward-facing normal across the reflected X axis. */
bool sl64_triangle_to_oot(const Sl64CoordinateMap *map,
                          const Sl64HostTriangle *hostTriangle,
                          uint16_t ootPreset,
                          struct OoTSurface *outSurface);

/* Converts a complete static-world array without allocation. Validation is
   transactional: no output element is written unless every input is valid. */
bool sl64_triangles_to_oot(const Sl64CoordinateMap *map,
                           const Sl64HostTriangle *hostTriangles,
                           size_t triangleCount,
                           const Sl64SurfaceRule *rules,
                           size_t ruleCount,
                           uint16_t fallbackPreset,
                           struct OoTSurface *outSurfaces);

/* Axis-aligned host water volume. liboot water extends down from ySurface;
   level geometry must provide the basin floor and walls. */
typedef struct Sl64HostWaterBox
{
    float xMin;
    float xMax;
    float zMin;
    float zMax;
    float ySurface;
} Sl64HostWaterBox;

bool sl64_water_box_to_oot(const Sl64CoordinateMap *map,
                           const Sl64HostWaterBox *hostWater,
                           struct OoTWaterBox *outWater);

/* Like triangle conversion, the array form is allocation-free and
   transactional. */
bool sl64_water_boxes_to_oot(const Sl64CoordinateMap *map,
                             const Sl64HostWaterBox *hostWater,
                             size_t waterCount,
                             struct OoTWaterBox *outWater);

#ifdef __cplusplus
}
#endif

#endif
