/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_core.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                                \
            ++g_failures;                                                       \
        }                                                                       \
    } while (0)

static int nearly_equal(float a, float b)
{
    return fabsf(a - b) <= 0.0001f;
}

static Sl64HostTriangle floor_triangle(uint32_t category, int32_t offsetX)
{
    Sl64HostTriangle triangle = {
        category,
        {
            { offsetX, 0, 0 },
            { offsetX, 0, 20 },
            { offsetX + 20, 0, 0 }
        }
    };
    return triangle;
}

static void test_rom_header(void)
{
    uint8_t z64[0x40] = { 0 };
    uint8_t swapped[0x40];
    static const uint8_t magic[4] = { 0x80u, 0x37u, 0x12u, 0x40u };
    size_t index;

    memcpy(z64, magic, sizeof(magic));
    memcpy(z64 + 0x20, "THE LEGEND OF ZELDA", 19u);
    memcpy(z64 + 0x3b, "NZLP", 4u);
    z64[0x3f] = 1u;
    CHECK(sl64_rom_header_is_pal_1_1(z64, sizeof(z64)));

    for (index = 0u; index < sizeof(swapped); ++index) {
        swapped[index ^ 1u] = z64[index];
    }
    CHECK(sl64_rom_header_is_pal_1_1(swapped, sizeof(swapped)));
    for (index = 0u; index < sizeof(swapped); ++index) {
        swapped[index ^ 3u] = z64[index];
    }
    CHECK(sl64_rom_header_is_pal_1_1(swapped, sizeof(swapped)));

    z64[0x3f] = 0u;
    CHECK(!sl64_rom_header_is_pal_1_1(z64, sizeof(z64)));
    CHECK(!sl64_rom_header_is_pal_1_1(NULL, sizeof(z64)));
    CHECK(!sl64_rom_header_is_pal_1_1(z64, 0x3fu));
}

static void test_coordinate_conversion(void)
{
    Sl64CoordinateMap map = SL64_COORDINATE_MAP_INIT;
    Sl64Vec3 oot = { 2.0f, -3.0f, 4.0f };
    Sl64Vec3 host = { 99.0f, 99.0f, 99.0f };
    Sl64Vec3 roundTrip = { 0.0f, 0.0f, 0.0f };
    Sl64Vec3 direction = { 0.25f, -0.5f, 0.75f };
    Sl64Vec3 reflected = { 0.0f, 0.0f, 0.0f };

    CHECK(sl64_coordinate_map_init(&map, 2.5f));
    CHECK(nearly_equal(map.hostUnitsPerOotUnit, 2.5f));
    CHECK(sl64_oot_position_to_host(&map, &oot, &host));
    CHECK(nearly_equal(host.x, -5.0f));
    CHECK(nearly_equal(host.y, -7.5f));
    CHECK(nearly_equal(host.z, 10.0f));
    CHECK(sl64_host_position_to_oot(&map, &host, &roundTrip));
    CHECK(nearly_equal(roundTrip.x, oot.x));
    CHECK(nearly_equal(roundTrip.y, oot.y));
    CHECK(nearly_equal(roundTrip.z, oot.z));

    CHECK(sl64_host_direction_to_oot(&direction, &reflected));
    CHECK(nearly_equal(reflected.x, -0.25f));
    CHECK(nearly_equal(reflected.y, -0.5f));
    CHECK(nearly_equal(reflected.z, 0.75f));
    CHECK(sl64_oot_direction_to_host(&reflected, &roundTrip));
    CHECK(nearly_equal(roundTrip.x, direction.x));
    CHECK(nearly_equal(roundTrip.y, direction.y));
    CHECK(nearly_equal(roundTrip.z, direction.z));

    CHECK(sl64_oot_yaw_to_host(0x2000) == -0x2000);
    CHECK(sl64_host_yaw_to_oot(sl64_oot_yaw_to_host(12345)) == 12345);
    CHECK(sl64_oot_yaw_to_host(INT16_MIN) == INT16_MIN);

    host.x = 7.0f;
    CHECK(!sl64_coordinate_map_init(&map, 0.0f));
    CHECK(nearly_equal(map.hostUnitsPerOotUnit, 2.5f));
    CHECK(!sl64_coordinate_map_init(&map, NAN));
    CHECK(!sl64_oot_position_to_host(NULL, &oot, &host));
    CHECK(nearly_equal(host.x, 7.0f));
}

static void test_input_mapping(void)
{
    Sl64HostInput host;
    OoTEngineInput output;
    OoTEngineInput before;
    uint32_t expectedButtons = OOT_ENGINE_BUTTON_A | OOT_ENGINE_BUTTON_B |
                               OOT_ENGINE_BUTTON_Z | OOT_ENGINE_BUTTON_R |
                               OOT_ENGINE_BUTTON_ITEM | OOT_ENGINE_BUTTON_CUP;

    memset(&host, 0, sizeof(host));
    memset(&output, 0xA5, sizeof(output));
    host.cameraToPlayer.x = 3.0f;
    host.cameraToPlayer.z = 4.0f;
    host.moveRight = 2.0f;
    host.moveForward = -2.0f;
    host.buttons = SL64_HOST_BUTTON_JUMP | SL64_HOST_BUTTON_ATTACK |
                   SL64_HOST_BUTTON_TARGET | SL64_HOST_BUTTON_GUARD |
                   SL64_HOST_BUTTON_ITEM | SL64_HOST_BUTTON_LOOK |
                   (1u << 31);

    CHECK(sl64_input_to_oot(&host, &output));
    CHECK(output.structSize == sizeof(output));
    CHECK(nearly_equal(output.camLookX, -0.6f));
    CHECK(nearly_equal(output.camLookZ, 0.8f));
    CHECK(nearly_equal(output.stickX, -1.0f));
    CHECK(nearly_equal(output.stickY, -1.0f));
    CHECK(output.buttons == expectedButtons);

    host.cameraToPlayer.x = 0.0f;
    host.cameraToPlayer.z = 0.0f;
    host.buttons = 0u;
    CHECK(sl64_input_to_oot(&host, &output));
    CHECK(nearly_equal(output.camLookX, 0.0f));
    CHECK(nearly_equal(output.camLookZ, 1.0f));

    before = output;
    host.moveForward = NAN;
    CHECK(!sl64_input_to_oot(&host, &output));
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);
}

static void test_surface_mapping(void)
{
    static const Sl64SurfaceRule rules[] = {
        { 7u, OOT_SURFACE_GRASS },
        { 42u, OOT_SURFACE_DAMAGE },
        { 7u, OOT_SURFACE_SAND }
    };
    Sl64SurfaceRule invalidRule = { 1u, OOT_SURFACE_PRESET_COUNT };
    uint16_t preset = UINT16_MAX;

    CHECK(sl64_surface_preset_for_category(
        7u, rules, sizeof(rules) / sizeof(rules[0]), OOT_SURFACE_STONE,
        &preset));
    CHECK(preset == OOT_SURFACE_GRASS);
    CHECK(sl64_surface_preset_for_category(
        999u, rules, sizeof(rules) / sizeof(rules[0]), OOT_SURFACE_STONE,
        &preset));
    CHECK(preset == OOT_SURFACE_STONE);
    CHECK(!sl64_surface_preset_for_category(
        1u, &invalidRule, 1u, OOT_SURFACE_DEFAULT, &preset));
    CHECK(!sl64_surface_preset_for_category(
        1u, NULL, 0u, OOT_SURFACE_PRESET_COUNT, &preset));
}

static void test_triangle_conversion(void)
{
    Sl64CoordinateMap map;
    Sl64HostTriangle triangle = floor_triangle(42u, 0);
    struct OoTSurface output;
    struct OoTSurface before;
    static const Sl64SurfaceRule rules[] = {
        { 42u, OOT_SURFACE_DAMAGE },
        { 7u, OOT_SURFACE_GRASS }
    };
    Sl64HostTriangle bulk[2];
    struct OoTSurface converted[2];
    struct OoTSurface snapshot[2];
    int64_t ax;
    int64_t az;
    int64_t bx;
    int64_t bz;
    int64_t normalY;

    CHECK(sl64_coordinate_map_init(&map, 2.0f));
    memset(&output, 0x5A, sizeof(output));
    CHECK(sl64_triangle_to_oot(&map, &triangle, OOT_SURFACE_STONE,
                               &output));
    CHECK(output.type == OOT_SURFACE_STONE);
    CHECK(output.vertices[0][0] == 0 && output.vertices[0][1] == 0 &&
          output.vertices[0][2] == 0);
    CHECK(output.vertices[1][0] == -10 && output.vertices[1][1] == 0 &&
          output.vertices[1][2] == 0);
    CHECK(output.vertices[2][0] == 0 && output.vertices[2][1] == 0 &&
          output.vertices[2][2] == 10);
    ax = (int64_t)output.vertices[1][0] - output.vertices[0][0];
    az = (int64_t)output.vertices[1][2] - output.vertices[0][2];
    bx = (int64_t)output.vertices[2][0] - output.vertices[0][0];
    bz = (int64_t)output.vertices[2][2] - output.vertices[0][2];
    normalY = az * bx - ax * bz;
    CHECK(normalY > 0);

    before = output;
    triangle.vertices[2][0] = triangle.vertices[1][0];
    triangle.vertices[2][1] = triangle.vertices[1][1];
    triangle.vertices[2][2] = triangle.vertices[1][2];
    CHECK(!sl64_triangle_to_oot(&map, &triangle, OOT_SURFACE_DEFAULT,
                                &output));
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);

    bulk[0] = floor_triangle(42u, 0);
    bulk[1] = floor_triangle(7u, 40);
    memset(converted, 0x33, sizeof(converted));
    CHECK(sl64_triangles_to_oot(
        &map, bulk, 2u, rules, sizeof(rules) / sizeof(rules[0]),
        OOT_SURFACE_DEFAULT, converted));
    CHECK(converted[0].type == OOT_SURFACE_DAMAGE);
    CHECK(converted[1].type == OOT_SURFACE_GRASS);

    memcpy(snapshot, converted, sizeof(snapshot));
    bulk[1].vertices[2][0] = INT32_MIN;
    CHECK(!sl64_triangles_to_oot(
        &map, bulk, 2u, rules, sizeof(rules) / sizeof(rules[0]),
        OOT_SURFACE_DEFAULT, converted));
    CHECK(memcmp(snapshot, converted, sizeof(snapshot)) == 0);
}

static void test_water_conversion(void)
{
    Sl64CoordinateMap map;
    Sl64HostWaterBox host = { -20.0f, 40.0f, -10.0f, 30.0f, 12.0f };
    struct OoTWaterBox output;
    struct OoTWaterBox before;
    Sl64HostWaterBox bulk[2] = {
        { -20.0f, 40.0f, -10.0f, 30.0f, 12.0f },
        { 50.0f, 90.0f, 10.0f, 50.0f, 8.0f }
    };
    struct OoTWaterBox converted[2];
    struct OoTWaterBox snapshot[2];

    CHECK(sl64_coordinate_map_init(&map, 2.0f));
    memset(&output, 0x44, sizeof(output));
    CHECK(sl64_water_box_to_oot(&map, &host, &output));
    CHECK(output.xMin == -20);
    CHECK(output.xLength == 30);
    CHECK(output.zMin == -5);
    CHECK(output.zLength == 20);
    CHECK(output.ySurface == 6);

    before = output;
    host.xMax = host.xMin;
    CHECK(!sl64_water_box_to_oot(&map, &host, &output));
    CHECK(memcmp(&before, &output, sizeof(output)) == 0);

    memset(converted, 0x22, sizeof(converted));
    CHECK(sl64_water_boxes_to_oot(&map, bulk, 2u, converted));
    CHECK(converted[1].xMin == -45);
    CHECK(converted[1].xLength == 20);
    memcpy(snapshot, converted, sizeof(snapshot));
    bulk[1].xMax = 1000000.0f;
    CHECK(!sl64_water_boxes_to_oot(&map, bulk, 2u, converted));
    CHECK(memcmp(snapshot, converted, sizeof(snapshot)) == 0);
    CHECK(sl64_water_boxes_to_oot(&map, NULL, 0u, NULL));
}

int main(void)
{
    test_rom_header();
    test_coordinate_conversion();
    test_input_mapping();
    test_surface_mapping();
    test_triangle_conversion();
    test_water_conversion();

    if (g_failures != 0) {
        fprintf(stderr, "sl64 core tests: %d failure(s)\n", g_failures);
        return 1;
    }
    puts("sl64 core tests: PASS");
    return 0;
}
