-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (C) 2026 Super Link 64 contributors
-- name: Super Link 64 - native frontend
-- description: Lifecycle and settings for the local liboot source build.
-- version: 0.1.0

local bridgePresent = type(sl64_available) == "function"
local booted = false
local levelReady = false
local enabled = true
local audioEnabled = true
local age = 0
local item = 8
local equipment = { 2, 2, 0, 0 }
local shown = {}
local hostActionActive = false
local ACT_SL64_PROXY = allocate_mario_action(
    ACT_GROUP_STATIONARY | ACT_FLAG_STATIONARY | ACT_FLAG_IDLE |
    ACT_FLAG_ALLOW_FIRST_PERSON | ACT_FLAG_PAUSE_EXIT
)

local DAMAGE_INTERACTIONS = INTERACT_DAMAGE | INTERACT_BOUNCE_TOP |
    INTERACT_BOUNCE_TOP2 | INTERACT_BULLY | INTERACT_SPINY_WALKING |
    INTERACT_FLAME | INTERACT_SHOCK | INTERACT_SNUFIT_BULLET

local function act_sl64_proxy(_)
    -- liboot already advanced and wrote the proxy in HOOK_BEFORE_MARIO_UPDATE.
    return 0
end

hook_mario_action(ACT_SL64_PROXY, act_sl64_proxy)

local function report_once(key, message, lines)
    if shown[key] then return end
    shown[key] = true
    print("[Super Link 64] " .. message)
    djui_popup_create("Super Link 64\n" .. message, lines or 3)
end

local function bridge_error(prefix)
    local detail = "native bridge error"
    if type(sl64_last_error) == "function" then
        detail = sl64_last_error()
    end
    report_once(prefix .. detail, prefix .. ": " .. detail, 4)
end

local function apply_loadout()
    if not booted or not levelReady then return end
    if not sl64_set_age(age) then bridge_error("Age") end
    if not sl64_set_equipment(
        equipment[1], equipment[2], equipment[3], equipment[4]) then
        bridge_error("Equipment")
    end
    if not sl64_set_item(item) then bridge_error("Item") end
end

local function on_mods_loaded()
    if not bridgePresent then
        report_once(
            "missing-bridge",
            "This mod needs the pinned native source build. Stock CoopDX cannot load liboot.",
            4
        )
        return
    end
    if not sl64_available() then
        bridge_error("Bridge unavailable")
        return
    end
    if type(sl64_set_proxy_action) ~= "function" or
            not sl64_set_proxy_action(ACT_SL64_PROXY) then
        bridge_error("Proxy setup failed")
        return
    end
    booted = sl64_boot()
    if not booted then
        bridge_error("Boot failed")
        return
    end
    sl64_set_enabled(enabled)
    sl64_set_audio_enabled(audioEnabled)
    print("[Super Link 64] liboot runtime initialized")
end

local function on_level_init()
    levelReady = false
    hostActionActive = false
    if not booted then return end
    levelReady = sl64_level_init()
    if not levelReady then
        bridge_error("Level import failed")
        return
    end
    shown.tick = nil
    apply_loadout()
    local status = sl64_get_status()
    print(string.format(
        "[Super Link 64] level ready: %d static surfaces, %d water boxes",
        status.staticSurfaces or 0,
        status.waterBoxes or 0
    ))
end

local function before_mario_update(m)
    if not booted or not levelReady or not enabled or m.playerIndex ~= 0 then
        return
    end
    -- Door, star, pipe, teleport, and area-transition actions live in the
    -- cutscene group. CoopDX owns those timers; Link follows the host pose
    -- without advancing its own movement simulation.
    if (m.action & ACT_GROUP_MASK) == ACT_GROUP_CUTSCENE then
        hostActionActive = true
        if not sl64_sync_link_render_from_mario(0) then
            bridge_error("Host-action sync failed")
            enabled = false
            sl64_set_enabled(false)
        end
        return
    end

    if hostActionActive then
        hostActionActive = false
        if not sl64_sync_link_from_mario(0) then
            bridge_error("Host-action sync failed")
            enabled = false
            sl64_set_enabled(false)
            set_mario_action(m, ACT_IDLE, 0)
            return
        end
    end

    local globalIndex = network_global_index_from_local(0)
    if not sl64_tick(0, globalIndex) then
        bridge_error("Link update failed")
        enabled = false
        sl64_set_enabled(false)
        set_mario_action(m, ACT_IDLE, 0)
        return
    end
    -- Prevent the ordinary Mario action that follows this hook from applying a
    -- second physics step and replacing liboot's position.
    m.action = ACT_SL64_PROXY
end

local function before_phys_step(m, _, _)
    if booted and levelReady and enabled and m.playerIndex == 0 and
            m.action == ACT_SL64_PROXY then
        return 0
    end
end

local function on_interact(m, object, interactType, applied)
    if not applied or not booted or not levelReady or not enabled or
            m.playerIndex ~= 0 or (interactType & DAMAGE_INTERACTIONS) == 0 then
        return
    end
    local damage = object.oDamageOrCoinValue or 1
    if damage < 1 then damage = 1 end
    -- liboot damage is measured in sixteenth-hearts. One SM64 damage point
    -- maps to a quarter-heart and SM64's own health drain is cancelled.
    if not sl64_damage_link(math.min(damage * 4, 32767)) then
        bridge_error("Damage")
    end
    m.hurtCounter = 0
    m.health = 0x880
    m.invincTimer = math.max(m.invincTimer, 30)
    -- The interaction handler may have selected an SM64 knockback action.
    -- liboot owns the reaction and position, so keep the proxy stationary.
    m.vel.x = 0
    m.vel.y = 0
    m.vel.z = 0
    m.forwardVel = 0
    m.action = ACT_SL64_PROXY
end

local function allow_hazard_surface(m, hazardType)
    if not booted or not levelReady or not enabled or m.playerIndex ~= 0 then
        return true
    end
    -- Burning floors are imported as OoT damage surfaces. Other hazards are
    -- not suppressed here because any host-owned transition must still run.
    if hazardType == HAZARD_TYPE_LAVA_FLOOR or
            hazardType == HAZARD_TYPE_LAVA_WALL then
        return false
    end
    return true
end

local function on_exit()
    if bridgePresent and type(sl64_set_proxy_action) == "function" then
        sl64_set_proxy_action(0)
    end
    if booted then sl64_shutdown() end
    booted = false
    levelReady = false
    hostActionActive = false
end

local function on_enabled(_, value)
    if value and booted and levelReady and not enabled then
        if not sl64_sync_link_from_mario(0) then
            bridge_error("Enable sync failed")
            return
        end
    end
    if bridgePresent and not sl64_set_enabled(value) then
        bridge_error("Enable state failed")
        return
    end
    enabled = value
    if not enabled and gMarioStates[0] ~= nil then
        set_mario_action(gMarioStates[0], ACT_IDLE, 0)
    end
end

local function on_audio(_, value)
    audioEnabled = value
    if bridgePresent then sl64_set_audio_enabled(value) end
end

local function on_child(_, value)
    age = value and 1 or 0
    apply_loadout()
end

local function on_item(_, value)
    item = value
    if booted and levelReady and not sl64_set_item(item) then
        bridge_error("Item")
    end
end

local function show_status()
    if not bridgePresent then
        report_once("status-no-bridge", "Native bridge is not installed.", 2)
        return
    end
    local status = sl64_get_status()
    local text = string.format(
        "booted=%s world=%s link=%s\ntriangles=%d batches=%d dropped=%d\ncollision=%d water=%d dynamic=%d actors=%d",
        tostring(status.booted),
        tostring(status.worldReady),
        tostring(status.linkReady),
        status.triangles or 0,
        status.batches or 0,
        status.droppedTriangles or 0,
        status.staticSurfaces or 0,
        status.waterBoxes or 0,
        status.dynamicObjects or 0,
        status.hostActors or 0
    )
    print("[Super Link 64] " .. text)
    djui_popup_create("Super Link 64\n" .. text, 5)
end

hook_event(HOOK_ON_MODS_LOADED, on_mods_loaded)
hook_event(HOOK_ON_LEVEL_INIT, on_level_init)
hook_event(HOOK_ON_INSTANT_WARP, on_level_init)
hook_event(HOOK_BEFORE_MARIO_UPDATE, before_mario_update)
hook_event(HOOK_BEFORE_PHYS_STEP, before_phys_step)
hook_event(HOOK_ON_INTERACT, on_interact)
hook_event(HOOK_ALLOW_HAZARD_SURFACE, allow_hazard_surface)
hook_event(HOOK_ON_EXIT, on_exit)

hook_mod_menu_text("Requires the local native source build.")
hook_mod_menu_checkbox("Enable Link", enabled, on_enabled)
hook_mod_menu_checkbox("OoT audio", audioEnabled, on_audio)
hook_mod_menu_checkbox("Child Link", false, on_child)
hook_mod_menu_slider("Item: 0 none, 1-8 usable", item, 0, 8, on_item)
hook_mod_menu_button("Runtime status", show_status)
