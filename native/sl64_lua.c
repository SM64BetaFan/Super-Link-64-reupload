/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_internal.h"

#include "pc/lua/smlua.h"

#include <limits.h>

static int sl64_lua_available(lua_State *lua)
{
    lua_pushboolean(lua, sl64_available());
    return 1;
}

static int sl64_lua_boot(lua_State *lua)
{
    lua_pushboolean(lua, sl64_boot());
    return 1;
}

static int sl64_lua_level_init(lua_State *lua)
{
    lua_pushboolean(lua, sl64_level_init());
    return 1;
}

static int sl64_lua_tick(lua_State *lua)
{
    lua_Integer localIndex = luaL_checkinteger(lua, 1);
    lua_Integer globalIndex = luaL_checkinteger(lua, 2);
    bool valid = localIndex >= INT32_MIN && localIndex <= INT32_MAX &&
                 globalIndex >= INT32_MIN && globalIndex <= INT32_MAX;

    lua_pushboolean(lua, valid && sl64_tick((int32_t)localIndex,
                                            (int32_t)globalIndex));
    return 1;
}

static int sl64_lua_sync_link_from_mario(lua_State *lua)
{
    lua_Integer localIndex = luaL_checkinteger(lua, 1);
    bool valid = localIndex >= INT32_MIN && localIndex <= INT32_MAX;

    lua_pushboolean(lua, valid &&
                         sl64_sync_link_from_mario((int32_t)localIndex));
    return 1;
}

static int sl64_lua_sync_link_render_from_mario(lua_State *lua)
{
    lua_Integer localIndex = luaL_checkinteger(lua, 1);
    bool valid = localIndex >= INT32_MIN && localIndex <= INT32_MAX;

    lua_pushboolean(lua, valid &&
                         sl64_sync_link_render_from_mario(
                             (int32_t)localIndex));
    return 1;
}

static int sl64_lua_shutdown(lua_State *lua)
{
    (void)lua;
    sl64_shutdown();
    return 0;
}

static int sl64_lua_last_error(lua_State *lua)
{
    lua_pushstring(lua, sl64_last_error());
    return 1;
}

static void sl64_lua_boolean_field(lua_State *lua, const char *name,
                                   uint8_t value)
{
    lua_pushboolean(lua, value != 0u);
    lua_setfield(lua, -2, name);
}

static void sl64_lua_integer_field(lua_State *lua, const char *name,
                                   lua_Integer value)
{
    lua_pushinteger(lua, value);
    lua_setfield(lua, -2, name);
}

static int sl64_lua_get_status(lua_State *lua)
{
    Sl64NativeStatus status;

    status.structSize = (uint32_t)sizeof(status);
    if (!sl64_get_status(&status)) {
        lua_pushnil(lua);
        return 1;
    }
    lua_createtable(lua, 0, 26);
    sl64_lua_boolean_field(lua, "available", status.available);
    sl64_lua_boolean_field(lua, "booted", status.booted);
    sl64_lua_boolean_field(lua, "enabled", status.enabled);
    sl64_lua_boolean_field(lua, "worldReady", status.worldReady);
    sl64_lua_boolean_field(lua, "linkReady", status.linkReady);
    sl64_lua_boolean_field(lua, "renderReady", status.renderReady);
    sl64_lua_boolean_field(lua, "audioEnabled", status.audioEnabled);
    sl64_lua_integer_field(lua, "lastResult", status.lastResult);
    lua_pushstring(lua, sl64_last_error());
    lua_setfield(lua, -2, "lastError");
    sl64_lua_integer_field(lua, "localIndex", status.localIndex);
    sl64_lua_integer_field(lua, "globalIndex", status.globalIndex);
    sl64_lua_integer_field(lua, "simulationTick",
                           (lua_Integer)status.simulationTick);
    sl64_lua_integer_field(lua, "triangles", status.triangles);
    sl64_lua_integer_field(lua, "batches", status.batches);
    sl64_lua_integer_field(lua, "droppedTriangles",
                           status.droppedTriangles);
    sl64_lua_integer_field(lua, "staticSurfaces", status.staticSurfaces);
    sl64_lua_integer_field(lua, "waterBoxes", status.waterBoxes);
    sl64_lua_integer_field(lua, "dynamicObjects", status.dynamicObjects);
    sl64_lua_integer_field(lua, "hostActors", status.hostActors);
    sl64_lua_integer_field(lua, "contactsApplied", status.contactsApplied);
    sl64_lua_integer_field(lua, "textureFallbacks",
                           status.textureFallbacks);
    sl64_lua_integer_field(lua, "audioFramesQueued",
                           status.audioFramesQueued);
    sl64_lua_integer_field(lua, "audioUnderruns", status.audioUnderruns);
    sl64_lua_integer_field(lua, "audioOverruns", status.audioOverruns);
    sl64_lua_integer_field(lua, "linkHealth", status.linkHealth);
    sl64_lua_integer_field(lua, "linkHealthCapacity",
                           status.linkHealthCapacity);
    sl64_lua_integer_field(lua, "linkMagic", status.linkMagic);
    sl64_lua_integer_field(lua, "linkAnimId", status.linkAnimId);
    sl64_lua_integer_field(lua, "linkAction", status.linkAction);
    sl64_lua_integer_field(lua, "linkAge", status.linkAge);
    sl64_lua_boolean_field(lua, "linkDead", status.linkDead);
    sl64_lua_boolean_field(lua, "linkInWater", status.linkInWater);
    sl64_lua_boolean_field(lua, "linkLockOn", status.linkLockOn);
    sl64_lua_integer_field(lua, "apiVersion", status.apiVersion);
    sl64_lua_integer_field(lua, "capabilityFlags",
                           (lua_Integer)status.capabilityFlags);
    sl64_lua_integer_field(lua, "libootCapabilityFlags",
                           (lua_Integer)status.libootCapabilityFlags);
    lua_pushstring(lua, SL64_COOPDX_VERSION);
    lua_setfield(lua, -2, "coopdxVersion");
    lua_pushstring(lua, SL64_COOPDX_COMMIT);
    lua_setfield(lua, -2, "coopdxCommit");
    return 1;
}

static int sl64_lua_set_enabled(lua_State *lua)
{
    lua_pushboolean(lua, sl64_set_enabled(lua_toboolean(lua, 1) != 0));
    return 1;
}

static int sl64_lua_set_age(lua_State *lua)
{
    lua_Integer value = luaL_checkinteger(lua, 1);
    lua_pushboolean(lua, value >= 0 && value <= UINT8_MAX &&
                         sl64_set_age((uint8_t)value));
    return 1;
}

static int sl64_lua_set_item(lua_State *lua)
{
    lua_Integer value = luaL_checkinteger(lua, 1);
    lua_pushboolean(lua, value >= 0 && value <= UINT8_MAX &&
                         sl64_set_item((uint8_t)value));
    return 1;
}

static int sl64_lua_set_equipment(lua_State *lua)
{
    lua_Integer sword = luaL_checkinteger(lua, 1);
    lua_Integer shield = luaL_checkinteger(lua, 2);
    lua_Integer tunic = luaL_checkinteger(lua, 3);
    lua_Integer boots = luaL_checkinteger(lua, 4);
    bool valid = sword >= 0 && sword <= UINT8_MAX && shield >= 0 &&
                 shield <= UINT8_MAX && tunic >= 0 && tunic <= UINT8_MAX &&
                 boots >= 0 && boots <= UINT8_MAX;

    lua_pushboolean(lua, valid && sl64_set_equipment(
                                     (uint8_t)sword, (uint8_t)shield,
                                     (uint8_t)tunic, (uint8_t)boots));
    return 1;
}

static int sl64_lua_damage_link(lua_State *lua)
{
    lua_Integer amount = luaL_checkinteger(lua, 1);

    lua_pushboolean(lua, amount > 0 && amount <= INT16_MAX &&
                         sl64_damage_link((int16_t)amount));
    return 1;
}

static int sl64_lua_set_audio_enabled(lua_State *lua)
{
    lua_pushboolean(lua,
                    sl64_set_audio_enabled(lua_toboolean(lua, 1) != 0));
    return 1;
}

static int sl64_lua_set_proxy_action(lua_State *lua)
{
    lua_Integer value = luaL_checkinteger(lua, 1);

    lua_pushboolean(lua, value >= 0 && (uint64_t)value <= UINT32_MAX &&
                         sl64_set_proxy_action((uint32_t)value));
    return 1;
}

void sl64_lua_bind(lua_State *luaState)
{
    if (luaState == NULL) {
        return;
    }
    smlua_bind_function(luaState, "sl64_available", sl64_lua_available);
    smlua_bind_function(luaState, "sl64_boot", sl64_lua_boot);
    smlua_bind_function(luaState, "sl64_level_init", sl64_lua_level_init);
    smlua_bind_function(luaState, "sl64_tick", sl64_lua_tick);
    smlua_bind_function(luaState, "sl64_sync_link_from_mario",
                        sl64_lua_sync_link_from_mario);
    smlua_bind_function(luaState, "sl64_sync_link_render_from_mario",
                        sl64_lua_sync_link_render_from_mario);
    smlua_bind_function(luaState, "sl64_shutdown", sl64_lua_shutdown);
    smlua_bind_function(luaState, "sl64_last_error", sl64_lua_last_error);
    smlua_bind_function(luaState, "sl64_get_status", sl64_lua_get_status);
    smlua_bind_function(luaState, "sl64_set_enabled", sl64_lua_set_enabled);
    smlua_bind_function(luaState, "sl64_set_age", sl64_lua_set_age);
    smlua_bind_function(luaState, "sl64_set_item", sl64_lua_set_item);
    smlua_bind_function(luaState, "sl64_damage_link", sl64_lua_damage_link);
    smlua_bind_function(luaState, "sl64_set_equipment",
                        sl64_lua_set_equipment);
    smlua_bind_function(luaState, "sl64_set_audio_enabled",
                        sl64_lua_set_audio_enabled);
    smlua_bind_function(luaState, "sl64_set_proxy_action",
                        sl64_lua_set_proxy_action);
}
