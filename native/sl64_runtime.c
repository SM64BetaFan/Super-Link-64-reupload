/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_internal.h"

#include <PR/os_cont.h>

#include "engine/graph_node.h"
#include "game/camera.h"
#include "game/level_update.h"
#include "object_fields.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Sl64State gSl64;

static uint64_t sl64_capabilities(uint64_t libootCapabilities)
{
    uint64_t capabilities = SL64_CAP_LOCAL_LINK |
                            SL64_CAP_STATIC_COLLISION | SL64_CAP_WATER |
                            SL64_CAP_LUA_GLOBALS;

    if ((libootCapabilities & OOT_ENGINE_CAPABILITY_DYNAMIC_COLLISION) != 0u) {
        capabilities |= SL64_CAP_DYNAMIC_WORLD_BAKED;
    }
    if ((libootCapabilities & OOT_ENGINE_CAPABILITY_HOST_ACTORS) != 0u) {
        capabilities |= SL64_CAP_HOST_ACTORS | SL64_CAP_HOST_CONTACTS;
    }
    if ((libootCapabilities & OOT_ENGINE_CAPABILITY_GEOMETRY_BATCHES) != 0u) {
        capabilities |= SL64_CAP_RENDER_BATCHES;
    }
    if ((libootCapabilities & OOT_ENGINE_CAPABILITY_TEXTURES) != 0u) {
        capabilities |= SL64_CAP_RENDER_RGBA32_TEXTURES;
    }
    if ((libootCapabilities & OOT_ENGINE_CAPABILITY_AUDIO) != 0u) {
        capabilities |= SL64_CAP_AUDIO_S16_RING;
    }
    return capabilities;
}

void sl64_initialize_defaults(void)
{
    if (gSl64.status.structSize != 0u) {
        return;
    }

    memset(&gSl64, 0, sizeof(gSl64));
    gSl64.status.structSize = (uint32_t)sizeof(gSl64.status);
    gSl64.status.apiVersion = SL64_NATIVE_API_VERSION;
    gSl64.status.capabilityFlags = sl64_capabilities(0u);
    gSl64.status.enabled = 1u;
    gSl64.status.audioEnabled = 1u;
    gSl64.status.localIndex = -1;
    gSl64.status.globalIndex = -1;
    /* Adult Link is roughly 60 native units tall while Mario's ordinary
     * collision height is 160 host units. This scale keeps model, movement,
     * collision, camera, and interaction distances in the same world. */
    gSl64.coordinateMap.hostUnitsPerOotUnit =
        SL64_HOST_UNITS_PER_OOT_UNIT;
    atomic_init(&gSl64.audio.readIndex, 0u);
    atomic_init(&gSl64.audio.writeIndex, 0u);
    atomic_init(&gSl64.audio.underruns, 0u);
    atomic_init(&gSl64.audio.overruns, 0u);
    atomic_init(&gSl64.audio.enabled, 1u);
    atomic_init(&gSl64.audio.resetRequested, 0u);
}

void sl64_clear_error(void)
{
    sl64_initialize_defaults();
    gSl64.lastError[0] = '\0';
    gSl64.status.lastResult = OOT_ENGINE_RESULT_OK;
}

bool sl64_fail(OoTResult result, const char *operation)
{
    const char *resultText;

    sl64_initialize_defaults();
    resultText = oot_engine_result_string(result);
    if (resultText == NULL) {
        resultText = "unknown liboot result";
    }
    snprintf(gSl64.lastError, sizeof(gSl64.lastError), "%s: %s (%d)",
             operation != NULL ? operation : "liboot", resultText,
             (int)result);
    gSl64.status.lastResult = (int32_t)result;
    return false;
}

bool sl64_result(OoTResult result, const char *operation)
{
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, operation);
    }
    gSl64.status.lastResult = OOT_ENGINE_RESULT_OK;
    return true;
}

void sl64_debug_callback(void *userData, const char *message)
{
    (void)userData;
    (void)message;
    /* CoopDX owns logging policy. The checked operation that fails supplies a
     * stable user-facing error through sl64_last_error(). */
}

void sl64_sfx_callback(void *userData, const struct OoTSfxEvent *event)
{
    Sl64State *state = (Sl64State *)userData;
    uint32_t next;

    if (state == NULL || event == NULL) {
        return;
    }
    next = (state->sfx.writeIndex + 1u) % SL64_SFX_QUEUE_CAPACITY;
    if (next == state->sfx.readIndex) {
        state->sfx.dropped++;
        return;
    }
    state->sfx.events[state->sfx.writeIndex] = *event;
    state->sfx.writeIndex = next;
}

static bool sl64_read_rom(const char *romPath, uint8_t **outBytes,
                          size_t *outSize)
{
    FILE *file;
    long fileSize;
    uint8_t *bytes;
    size_t bytesRead;
    int closeResult;

    if (romPath == NULL || romPath[0] == '\0' || outBytes == NULL ||
        outSize == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "ROM path is empty");
    }
    file = fopen(romPath, "rb");
    if (file == NULL) {
        snprintf(gSl64.lastError, sizeof(gSl64.lastError),
                 "cannot open ROM: %s", strerror(errno));
        gSl64.status.lastResult = OOT_ENGINE_RESULT_INVALID_ARGUMENT;
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (fileSize = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        snprintf(gSl64.lastError, sizeof(gSl64.lastError),
                 "cannot measure ROM file");
        fclose(file);
        gSl64.status.lastResult = OOT_ENGINE_RESULT_INVALID_ARGUMENT;
        return false;
    }
    if ((uint64_t)fileSize < OOT_ENGINE_MIN_ROM_SIZE ||
        (uint64_t)fileSize > OOT_ENGINE_MAX_ROM_SIZE) {
        snprintf(gSl64.lastError, sizeof(gSl64.lastError),
                 "ROM size %ld is outside liboot's accepted range", fileSize);
        fclose(file);
        gSl64.status.lastResult = OOT_ENGINE_RESULT_INVALID_ARGUMENT;
        return false;
    }
    bytes = (uint8_t *)malloc((size_t)fileSize);
    if (bytes == NULL) {
        fclose(file);
        return sl64_fail(OOT_ENGINE_RESULT_OUT_OF_MEMORY, "reading ROM");
    }
    bytesRead = fread(bytes, 1u, (size_t)fileSize, file);
    closeResult = fclose(file);
    if (bytesRead != (size_t)fileSize || closeResult != 0) {
        snprintf(gSl64.lastError, sizeof(gSl64.lastError),
                 "short read while loading ROM file");
        free(bytes);
        gSl64.status.lastResult = OOT_ENGINE_RESULT_INVALID_ARGUMENT;
        return false;
    }
    *outBytes = bytes;
    *outSize = (size_t)fileSize;
    return true;
}

bool sl64_available(void)
{
    OoTEngineLimits limits = OOT_ENGINE_LIMITS_INIT;
    OoTResult result;
    uint64_t required;

    sl64_initialize_defaults();
    if (oot_engine_api_version() != OOT_ENGINE_API_VERSION) {
        gSl64.status.available = 0u;
        return sl64_fail(OOT_ENGINE_RESULT_API_VERSION,
                         "liboot checked API version");
    }
    result = oot_engine_get_limits(&limits);
    if (result != OOT_ENGINE_RESULT_OK) {
        gSl64.status.available = 0u;
        return sl64_fail(result, "querying liboot capabilities");
    }
    sl64_clear_error();
    required = OOT_ENGINE_CAPABILITY_STATIC_WORLD |
               OOT_ENGINE_CAPABILITY_LINK_GEOMETRY |
               OOT_ENGINE_CAPABILITY_FIXED_STEP;
    if ((limits.capabilityFlags & required) != required) {
        gSl64.status.available = 0u;
        snprintf(gSl64.lastError, sizeof(gSl64.lastError),
                 "liboot lacks required static-world, Link geometry, or fixed-step support");
        gSl64.status.lastResult = OOT_ENGINE_RESULT_NOT_AVAILABLE;
        return false;
    }
    gSl64.limits = limits;
    gSl64.status.libootCapabilityFlags = limits.capabilityFlags;
    gSl64.status.capabilityFlags = sl64_capabilities(limits.capabilityFlags);
    gSl64.status.available = 1u;
    gSl64.status.lastResult = OOT_ENGINE_RESULT_OK;
    return true;
}

bool sl64_boot(void)
{
    const char *romPath;

    sl64_initialize_defaults();
    romPath = getenv(SL64_ROM_ENVIRONMENT_VARIABLE);
    if (romPath == NULL || romPath[0] == '\0') {
        snprintf(gSl64.lastError, sizeof(gSl64.lastError),
                 "%s is not set", SL64_ROM_ENVIRONMENT_VARIABLE);
        gSl64.status.lastResult = OOT_ENGINE_RESULT_INVALID_ARGUMENT;
        return false;
    }
    return sl64_boot_from_path(romPath);
}

bool sl64_boot_from_path(const char *romPath)
{
    OoTEngineConfig config;
    OoTResult result;
    uint8_t *romBytes = NULL;
    size_t romSize = 0u;

    sl64_initialize_defaults();
    if (gSl64.engine != NULL) {
        return true;
    }
    sl64_clear_error();
    if (!sl64_available() || !sl64_read_rom(romPath, &romBytes, &romSize)) {
        return false;
    }
    if (!sl64_rom_header_is_pal_1_1(romBytes, romSize)) {
        free(romBytes);
        snprintf(gSl64.lastError, sizeof(gSl64.lastError),
                 "ROM is not Ocarina of Time PAL Europe Rev 1 (PAL 1.1)");
        gSl64.status.lastResult = OOT_ENGINE_RESULT_INVALID_ARGUMENT;
        return false;
    }
    memset(&config, 0, sizeof(config));
    result = oot_engine_config_init(&config);
    if (result != OOT_ENGINE_RESULT_OK) {
        free(romBytes);
        return sl64_fail(result, "initializing liboot configuration");
    }
    config.romData = romBytes;
    config.romSize = romSize;
    config.actorCapacity = OOT_ENGINE_DEFAULT_ACTOR_CAPACITY;
    config.renderFlags = OOT_ENGINE_RENDER_NAVI | OOT_ENGINE_RENDER_ACTORS;
    config.debugCallback = sl64_debug_callback;
    config.debugUserData = &gSl64;
    config.sfxCallback = sl64_sfx_callback;
    config.sfxUserData = &gSl64;
    result = oot_engine_create(&config, &gSl64.engine);
    free(romBytes);
    if (result != OOT_ENGINE_RESULT_OK) {
        gSl64.engine = NULL;
        return sl64_fail(result, "creating liboot engine");
    }
    gSl64.status.booted = 1u;
    gSl64.status.worldReady = 0u;
    gSl64.status.linkReady = 0u;
    gSl64.status.renderReady = 0u;
    if ((gSl64.status.capabilityFlags & SL64_CAP_AUDIO_S16_RING) == 0u) {
        gSl64.status.audioEnabled = 0u;
        atomic_store_explicit(&gSl64.audio.enabled, 0u,
                              memory_order_release);
    }
    sl64_audio_reset();
    sl64_clear_error();
    return true;
}

static bool sl64_create_link_from_mario(struct MarioState *mario)
{
    Sl64Vec3 hostPosition;
    Sl64Vec3 ootPosition;
    OoTResult result;

    if (mario == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "local Mario state is unavailable");
    }
    hostPosition.x = mario->pos[0];
    hostPosition.y = mario->pos[1];
    hostPosition.z = mario->pos[2];
    if (!sl64_host_position_to_oot(&gSl64.coordinateMap, &hostPosition,
                                   &ootPosition)) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "converting Link spawn position");
    }
    result = oot_engine_link_create(gSl64.engine, ootPosition.x,
                                    ootPosition.y, ootPosition.z);
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "creating Link");
    }
    result = oot_engine_link_set_pose(
        gSl64.engine, ootPosition.x, ootPosition.y, ootPosition.z,
        sl64_host_yaw_to_oot(mario->faceAngle[1]));
    if (result != OOT_ENGINE_RESULT_OK) {
        (void)oot_engine_link_delete(gSl64.engine);
        return sl64_fail(result, "setting Link spawn pose");
    }
    gSl64.status.linkReady = 1u;
    gSl64.status.linkAge = OOT_AGE_ADULT;
    return true;
}

bool sl64_level_init(void)
{
    OoTResult result;

    sl64_initialize_defaults();
    if (gSl64.engine == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "initializing CoopDX level");
    }
    sl64_render_reset();
    sl64_actor_reset();
    sl64_world_reset_dynamic();
    if (gSl64.status.linkReady != 0u) {
        result = oot_engine_link_delete(gSl64.engine);
        if (result != OOT_ENGINE_RESULT_OK &&
            result != OOT_ENGINE_RESULT_LINK_NOT_FOUND) {
            return sl64_fail(result, "replacing Link for a new level");
        }
        gSl64.status.linkReady = 0u;
    }
    if (!sl64_world_load_static() ||
        !sl64_create_link_from_mario(&gMarioStates[0])) {
        return false;
    }
    result = oot_engine_reset_clock(gSl64.engine);
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "resetting Link simulation clock");
    }
    sl64_clear_error();
    return true;
}

static void sl64_build_input(const struct MarioState *mario,
                             OoTEngineInput *outInput)
{
    Sl64HostInput hostInput;
    uint16_t buttons = 0u;

    memset(&hostInput, 0, sizeof(hostInput));
    hostInput.cameraToPlayer.x = mario->pos[0] - gLakituState.pos[0];
    hostInput.cameraToPlayer.y = 0.0f;
    hostInput.cameraToPlayer.z = mario->pos[2] - gLakituState.pos[2];
    if (mario->controller != NULL) {
        hostInput.moveRight = mario->controller->stickX / 64.0f;
        hostInput.moveForward = mario->controller->stickY / 64.0f;
        buttons = mario->controller->buttonDown;
    }
    if ((buttons & A_BUTTON) != 0u) {
        hostInput.buttons |= SL64_HOST_BUTTON_JUMP;
    }
    if ((buttons & B_BUTTON) != 0u) {
        hostInput.buttons |= SL64_HOST_BUTTON_ATTACK;
    }
    if ((buttons & Z_TRIG) != 0u) {
        hostInput.buttons |= SL64_HOST_BUTTON_TARGET;
    }
    if ((buttons & R_TRIG) != 0u) {
        hostInput.buttons |= SL64_HOST_BUTTON_GUARD;
    }
    if ((buttons & X_BUTTON) != 0u) {
        hostInput.buttons |= SL64_HOST_BUTTON_ITEM;
    }
    if ((buttons & U_CBUTTONS) != 0u) {
        hostInput.buttons |= SL64_HOST_BUTTON_LOOK;
    }
    if (!sl64_input_to_oot(&hostInput, outInput)) {
        OoTEngineInput fallback = OOT_ENGINE_INPUT_INIT;
        *outInput = fallback;
    }
}

static void sl64_copy_link_to_mario(struct MarioState *mario,
                                    const OoTEngineFrame *frame)
{
    const OoTEngineLinkState *link = &frame->link;
    Sl64Vec3 oot;
    Sl64Vec3 host;
    float alpha = sl64_clampf(frame->interpolationAlpha, 0.0f, 1.0f);
    int16_t yaw;

    /* PAL gameplay remains authoritative at 60 ms. Extrapolating only the
     * copied host proxy by the accumulator fraction removes 16.7 Hz camera
     * judder without changing liboot's simulation rate or state. */
    oot.x = link->position[0] + link->velocity[0] * alpha;
    oot.y = link->position[1] + link->velocity[1] * alpha;
    oot.z = link->position[2] + link->velocity[2] * alpha;
    if (sl64_oot_position_to_host(&gSl64.coordinateMap, &oot, &host)) {
        mario->pos[0] = host.x;
        mario->pos[1] = host.y;
        mario->pos[2] = host.z;
    }
    oot.x = link->velocity[0];
    oot.y = link->velocity[1];
    oot.z = link->velocity[2];
    if (sl64_oot_position_to_host(&gSl64.coordinateMap, &oot, &host)) {
        mario->vel[0] = host.x;
        mario->vel[1] = host.y;
        mario->vel[2] = host.z;
        mario->slideVelX = host.x;
        mario->slideVelZ = host.z;
    }
    mario->forwardVel = link->linearVelocity *
                        gSl64.coordinateMap.hostUnitsPerOotUnit;
    yaw = sl64_oot_yaw_to_host(link->faceAngle);
    mario->faceAngle[1] = yaw;
    if (mario->marioObj != NULL) {
        mario->marioObj->oPosX = mario->pos[0];
        mario->marioObj->oPosY = mario->pos[1];
        mario->marioObj->oPosZ = mario->pos[2];
        mario->marioObj->oFaceAngleYaw = yaw;
        mario->marioObj->header.gfx.pos[0] = mario->pos[0];
        mario->marioObj->header.gfx.pos[1] = mario->pos[1];
        mario->marioObj->header.gfx.pos[2] = mario->pos[2];
        mario->marioObj->header.gfx.angle[1] = yaw;
    }
}

static void sl64_apply_frame_status(const OoTEngineFrame *frame)
{
    gSl64.status.simulationTick = frame->simulationTick;
    gSl64.status.linkHealth = frame->link.health;
    gSl64.status.linkHealthCapacity = frame->link.healthCapacity;
    gSl64.status.linkMagic = frame->link.magic;
    gSl64.status.linkAnimId = frame->link.animId;
    gSl64.status.linkAction = frame->link.action;
    gSl64.status.linkAge = frame->link.age;
    gSl64.status.linkDead = frame->link.isDead;
    gSl64.status.linkInWater = frame->link.inWater;
    gSl64.status.linkLockOn = frame->link.lockOnActive;
    gSl64.status.lastResult = OOT_ENGINE_RESULT_OK;
}

static void sl64_apply_frame_to_proxy(struct MarioState *mario,
                                      const OoTEngineFrame *frame)
{
    sl64_copy_link_to_mario(mario, frame);
    sl64_apply_frame_status(frame);
}

bool sl64_tick(int32_t localIndex, int32_t globalIndex)
{
    struct MarioState *mario;
    OoTEngineInput input;
    const OoTEngineFrame *frame = NULL;
    uint32_t steps = 0u;
    OoTResult result;

    sl64_initialize_defaults();
    gSl64.status.localIndex = localIndex;
    gSl64.status.globalIndex = globalIndex;
    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u ||
        gSl64.status.worldReady == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "ticking Link before level initialization");
    }
    if (localIndex != 0 || globalIndex < 0) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "only local player index 0 may simulate Link");
    }
    if (gSl64.status.enabled == 0u) {
        return true;
    }

    mario = &gMarioStates[localIndex];
    sl64_build_input(mario, &input);
    if (!sl64_actor_sync_objects()) {
        return false;
    }
    result = oot_engine_advance(gSl64.engine, 1.0f / 30.0f, &input,
                                &steps, &frame);
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "advancing Link");
    }
    if (frame == NULL) {
        if (steps == 0u) {
            /* Link creation may precede liboot's first complete frame. */
            gSl64.status.lastResult = OOT_ENGINE_RESULT_OK;
            return true;
        }
        return sl64_fail(OOT_ENGINE_RESULT_NO_FRAME,
                         "liboot returned no Link frame");
    }

    /* A zero-step advance still returns the current borrowed frame with a new
     * interpolation alpha. Re-copy it so host wall pushes cannot survive and
     * presentation keeps moving between native ticks. */
    if (!sl64_render_capture(frame)) {
        return false;
    }
    sl64_apply_frame_to_proxy(mario, frame);
    if (steps != 0u) {
        sl64_actor_apply_contacts();
    }
    sl64_audio_tick();
    return true;
}

static bool sl64_sync_link_from_mario_internal(int32_t localIndex,
                                               bool writeProxy)
{
    struct MarioState *mario;
    Sl64Vec3 hostPosition;
    Sl64Vec3 ootPosition;
    OoTEngineInput input = OOT_ENGINE_INPUT_INIT;
    const OoTEngineFrame *frame = NULL;
    OoTEngineFrame frameSnapshot;
    OoTResult result;
    bool captured = false;

    sl64_initialize_defaults();
    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u ||
        gSl64.status.worldReady == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "synchronizing Link before level initialization");
    }
    if (localIndex != 0) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "only local player index 0 may synchronize Link");
    }

    mario = &gMarioStates[localIndex];
    hostPosition.x = mario->pos[0];
    hostPosition.y = mario->pos[1];
    hostPosition.z = mario->pos[2];
    if (!sl64_host_position_to_oot(&gSl64.coordinateMap, &hostPosition,
                                   &ootPosition)) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "converting the host-owned Link position");
    }

    result = oot_engine_link_freeze(gSl64.engine, 1u);
    if (result == OOT_ENGINE_RESULT_OK) {
        result = oot_engine_link_set_pose(
            gSl64.engine, ootPosition.x, ootPosition.y, ootPosition.z,
            sl64_host_yaw_to_oot(mario->faceAngle[1]));
    }
    if (result == OOT_ENGINE_RESULT_OK) {
        result = oot_engine_step(gSl64.engine, &input, &frame);
    }
    if (result == OOT_ENGINE_RESULT_OK && frame == NULL) {
        result = OOT_ENGINE_RESULT_NO_FRAME;
    }
    if (result == OOT_ENGINE_RESULT_OK) {
        captured = sl64_render_capture(frame);
        if (captured) {
            frameSnapshot = *frame;
        }
    }
    if (result == OOT_ENGINE_RESULT_OK) {
        result = oot_engine_reset_clock(gSl64.engine);
    }

    {
        OoTResult restoreResult = oot_engine_link_freeze(
            gSl64.engine, gSl64.status.enabled != 0u ? 0u : 1u);
        if (result == OOT_ENGINE_RESULT_OK && restoreResult != OOT_ENGINE_RESULT_OK) {
            result = restoreResult;
        }
    }
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "synchronizing Link to a host-owned action");
    }
    if (!captured) {
        return false;
    }

    if (writeProxy) {
        sl64_apply_frame_to_proxy(mario, &frameSnapshot);
    } else {
        sl64_apply_frame_status(&frameSnapshot);
    }
    sl64_clear_error();
    return true;
}

bool sl64_sync_link_from_mario(int32_t localIndex)
{
    return sl64_sync_link_from_mario_internal(localIndex, true);
}

bool sl64_sync_link_render_from_mario(int32_t localIndex)
{
    return sl64_sync_link_from_mario_internal(localIndex, false);
}

bool sl64_should_hide_mario_proxy(const struct Object *object)
{
    sl64_initialize_defaults();
    return object != NULL && object == gMarioStates[0].marioObj &&
           gSl64.status.enabled != 0u &&
           gSl64.status.linkReady != 0u &&
           gSl64.status.renderReady != 0u &&
           (object->header.gfx.node.flags & GRAPH_RENDER_ACTIVE) != 0u;
}

void sl64_shutdown(void)
{
    OoTResult result;
    uint8_t wasAvailable;
    uint64_t libootCapabilities;

    sl64_initialize_defaults();
    wasAvailable = gSl64.status.available;
    libootCapabilities = gSl64.status.libootCapabilityFlags;
    if (gSl64.engine != NULL) {
        (void)oot_engine_audio_stop_all(gSl64.engine, 0u);
        sl64_actor_reset();
        sl64_world_reset_dynamic();
        result = oot_engine_destroy(gSl64.engine);
        if (result != OOT_ENGINE_RESULT_OK) {
            (void)sl64_fail(result, "destroying liboot engine");
            return;
        }
        gSl64.engine = NULL;
    }
    sl64_render_reset();
    sl64_world_shutdown();
    sl64_audio_shutdown();
    memset(&gSl64.status, 0, sizeof(gSl64.status));
    gSl64.status.structSize = (uint32_t)sizeof(gSl64.status);
    gSl64.status.apiVersion = SL64_NATIVE_API_VERSION;
    gSl64.status.capabilityFlags = sl64_capabilities(libootCapabilities);
    gSl64.status.libootCapabilityFlags = libootCapabilities;
    gSl64.status.available = wasAvailable;
    gSl64.status.enabled = 1u;
    gSl64.status.audioEnabled = 0u;
    gSl64.status.localIndex = -1;
    gSl64.status.globalIndex = -1;
    gSl64.coordinateMap.hostUnitsPerOotUnit =
        SL64_HOST_UNITS_PER_OOT_UNIT;
    atomic_store_explicit(&gSl64.audio.enabled, 0u, memory_order_release);
    gSl64.lastError[0] = '\0';
}

const char *sl64_last_error(void)
{
    sl64_initialize_defaults();
    return gSl64.lastError;
}

bool sl64_get_status(Sl64NativeStatus *outStatus)
{
    Sl64NativeStatus snapshot;
    uint32_t readIndex;
    uint32_t writeIndex;

    sl64_initialize_defaults();
    if (outStatus == NULL ||
        (outStatus->structSize != 0u &&
         outStatus->structSize < sizeof(Sl64NativeStatus))) {
        return false;
    }
    readIndex = atomic_load_explicit(&gSl64.audio.readIndex,
                                     memory_order_acquire);
    writeIndex = atomic_load_explicit(&gSl64.audio.writeIndex,
                                      memory_order_acquire);
    snapshot = gSl64.status;
    snapshot.audioFramesQueued =
        atomic_load_explicit(&gSl64.audio.resetRequested,
                             memory_order_acquire) != 0u ?
        0u : writeIndex - readIndex;
    snapshot.audioUnderruns = atomic_load_explicit(&gSl64.audio.underruns,
                                                   memory_order_relaxed);
    snapshot.audioOverruns = atomic_load_explicit(&gSl64.audio.overruns,
                                                  memory_order_relaxed);
    *outStatus = snapshot;
    return true;
}

bool sl64_set_enabled(bool enabled)
{
    OoTResult result;

    sl64_initialize_defaults();
    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u) {
        gSl64.status.enabled = enabled ? 1u : 0u;
        return true;
    }
    result = oot_engine_link_freeze(gSl64.engine, enabled ? 0u : 1u);
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "changing Link enabled state");
    }
    gSl64.status.enabled = enabled ? 1u : 0u;
    sl64_clear_error();
    return true;
}

bool sl64_set_age(uint8_t age)
{
    const OoTEngineFrame *frame = NULL;
    float position[3] = { 0.0f, 0.0f, 0.0f };
    int16_t yaw = 0;
    bool restorePose = false;
    OoTResult result;

    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "setting Link age");
    }
    if (age == gSl64.status.linkAge) {
        return true;
    }
    if (oot_engine_get_frame(gSl64.engine, &frame) == OOT_ENGINE_RESULT_OK &&
        frame != NULL) {
        memcpy(position, frame->link.position, sizeof(position));
        yaw = frame->link.faceAngle;
        restorePose = true;
    }
    result = oot_engine_link_set_age(gSl64.engine, age);
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "setting Link age");
    }
    /* Age changes rebuild the native Link and invalidate host-actor handles. */
    sl64_actor_reset();
    if (restorePose) {
        result = oot_engine_link_set_pose(gSl64.engine, position[0],
                                          position[1], position[2], yaw);
        if (result != OOT_ENGINE_RESULT_OK) {
            return sl64_fail(result, "restoring Link pose after age change");
        }
    }
    gSl64.status.linkAge = age;
    return true;
}

bool sl64_set_item(uint8_t item)
{
    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "equipping Link item");
    }
    return sl64_result(oot_engine_link_use_item(gSl64.engine, item),
                       "equipping Link item");
}

bool sl64_set_magic(uint8_t level, int16_t amount)
{
    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "setting Link magic");
    }
    if (level > 2u || amount < 0 ||
        amount > (int16_t)(level * 0x30u)) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "setting Link magic");
    }
    return sl64_result(oot_engine_link_set_magic(gSl64.engine, level, amount),
                       "setting Link magic");
}

bool sl64_damage_link(int16_t amount)
{
    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u || amount <= 0) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "applying SM64 damage to Link");
    }
    return sl64_result(oot_engine_link_damage(gSl64.engine, amount),
                       "applying SM64 damage to Link");
}

bool sl64_set_equipment(uint8_t sword, uint8_t shield,
                        uint8_t tunic, uint8_t boots)
{
    if (gSl64.engine == NULL || gSl64.status.linkReady == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "setting Link equipment");
    }
    return sl64_result(oot_engine_link_set_equipment(
                           gSl64.engine, sword, shield, tunic, boots),
                       "setting Link equipment");
}

bool sl64_set_proxy_action(uint32_t action)
{
    sl64_initialize_defaults();
    gSl64.proxyAction = action;
    return true;
}

bool sl64_before_phys_step(struct MarioState *mario, s32 stepType,
                           u32 stepArg, s32 *stepResultOverride)
{
    (void)stepType;
    (void)stepArg;
    sl64_initialize_defaults();
    if (stepResultOverride == NULL || mario != &gMarioStates[0] ||
        gSl64.proxyAction == 0u || mario->action != gSl64.proxyAction ||
        gSl64.status.enabled == 0u || gSl64.status.linkReady == 0u) {
        return false;
    }
    *stepResultOverride = 0;
    return true;
}
