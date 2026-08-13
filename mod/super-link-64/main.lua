-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Copyright (C) 2026 Super Link 64 contributors
-- name: Super Link 64 - native frontend
-- description: Play as liboot Link in the local CoopDX source build.
-- version: 0.2.0

local BRIDGE_API_VERSION = 2
local bridgeDetected = type(sl64_available) == "function"
local bridgePresent = false
if bridgeDetected and type(sl64_get_status) == "function" and
        type(sl64_set_magic) == "function" then
    local ok, status = pcall(sl64_get_status)
    bridgePresent = ok and status ~= nil and
        status.apiVersion == BRIDGE_API_VERSION
end
local booted = false
local levelReady = false
local hostActionActive = false
local pendingLinkTick = false
local shown = {}
local menu = {}

local ACT_SL64_PROXY = allocate_mario_action(
    ACT_GROUP_STATIONARY | ACT_FLAG_STATIONARY | ACT_FLAG_IDLE |
    ACT_FLAG_ALLOW_FIRST_PERSON | ACT_FLAG_PAUSE_EXIT
)

local DAMAGE_INTERACTIONS = INTERACT_DAMAGE | INTERACT_BOUNCE_TOP |
    INTERACT_BOUNCE_TOP2 | INTERACT_BULLY | INTERACT_SPINY_WALKING |
    INTERACT_FLAME | INTERACT_SHOCK | INTERACT_SNUFIT_BULLET

local CHOICES = {
    age = {
        { value = 0, label = "Adult" },
        { value = 1, label = "Child" },
    },
    sword = {
        { value = 0, label = "None" },
        { value = 1, label = "Kokiri Sword" },
        { value = 2, label = "Master Sword" },
        { value = 3, label = "Biggoron Sword" },
    },
    shield = {
        { value = 0, label = "None" },
        { value = 1, label = "Deku Shield" },
        { value = 2, label = "Hylian Shield" },
        { value = 3, label = "Mirror Shield" },
    },
    tunic = {
        { value = 0, label = "Kokiri Tunic" },
        { value = 1, label = "Goron Tunic" },
        { value = 2, label = "Zora Tunic" },
    },
    boots = {
        { value = 0, label = "Kokiri Boots" },
        { value = 1, label = "Iron Boots" },
        { value = 2, label = "Hover Boots" },
    },
    item = {
        { value = 0, label = "None" },
        { value = 1, label = "Ocarina" },
        { value = 2, label = "Bottle" },
        { value = 3, label = "Megaton Hammer" },
        { value = 4, label = "Deku Stick" },
        { value = 5, label = "Boomerang" },
        { value = 6, label = "Fairy Bow" },
        { value = 7, label = "Hookshot" },
        { value = 8, label = "Bomb" },
    },
    magic = {
        { value = 0, label = "None" },
        { value = 1, label = "Single" },
        { value = 2, label = "Double" },
    },
}

local VALID = {
    [0] = {
        sword = { 0, 2, 3 },
        shield = { 0, 2, 3 },
        tunic = { 0, 1, 2 },
        boots = { 0, 1, 2 },
        item = { 0, 1, 2, 3, 6, 7, 8 },
    },
    [1] = {
        sword = { 0, 1 },
        shield = { 0, 1, 2 },
        tunic = { 0 },
        boots = { 0 },
        item = { 0, 1, 2, 4, 5, 8 },
    },
}

local MAGIC_FILL = { 0, 25, 50, 75, 100 }

local enabled = mod_storage_load_bool("enabled", true)
local audioEnabled = mod_storage_load_bool("audio", true)
local age = mod_storage_load_integer("age", 0)
local sword = mod_storage_load_integer("sword", 2)
local shield = mod_storage_load_integer("shield", 2)
local tunic = mod_storage_load_integer("tunic", 0)
local boots = mod_storage_load_integer("boots", 0)
local item = mod_storage_load_integer("item", 6)
local magicLevel = mod_storage_load_integer("magic_level", 1)
local magicFillIndex = mod_storage_load_integer("magic_fill", 5)

local function choice_label(kind, value)
    for _, choice in ipairs(CHOICES[kind]) do
        if choice.value == value then return choice.label end
    end
    return "Unknown"
end

local function contains(values, wanted)
    for _, value in ipairs(values) do
        if value == wanted then return true end
    end
    return false
end

local function cycle(values, current)
    for index, value in ipairs(values) do
        if value == current then
            return values[index % #values + 1]
        end
    end
    return values[1]
end

local function normalize_loadout(previousAge)
    if age ~= 0 and age ~= 1 then age = 0 end
    if magicLevel < 0 or magicLevel > 2 then magicLevel = 1 end
    if magicFillIndex < 1 or magicFillIndex > #MAGIC_FILL then
        magicFillIndex = #MAGIC_FILL
    end

    if previousAge ~= nil and previousAge ~= age then
        if age == 1 then
            sword = sword == 0 and 0 or 1
            if shield ~= 0 and shield ~= 1 then shield = 2 end
            tunic = 0
            boots = 0
        else
            sword = sword == 0 and 0 or 2
            if shield ~= 0 and shield ~= 3 then shield = 2 end
        end
    end

    local valid = VALID[age]
    if not contains(valid.sword, sword) then sword = age == 0 and 2 or 1 end
    if not contains(valid.shield, shield) then shield = 2 end
    if not contains(valid.tunic, tunic) then tunic = 0 end
    if not contains(valid.boots, boots) then boots = 0 end
    if not contains(valid.item, item) then item = 0 end
end

normalize_loadout(nil)

local function save_loadout()
    mod_storage_save_integer("age", age)
    mod_storage_save_integer("sword", sword)
    mod_storage_save_integer("shield", shield)
    mod_storage_save_integer("tunic", tunic)
    mod_storage_save_integer("boots", boots)
    mod_storage_save_integer("item", item)
    mod_storage_save_integer("magic_level", magicLevel)
    mod_storage_save_integer("magic_fill", magicFillIndex)
end

local function magic_amount()
    local capacity = magicLevel * 0x30
    return math.floor(capacity * MAGIC_FILL[magicFillIndex] / 100)
end

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

local function refresh_menu_names()
    local names = {
        age = "Age: " .. choice_label("age", age),
        sword = "Sword: " .. choice_label("sword", sword),
        shield = "Shield: " .. choice_label("shield", shield),
        tunic = "Tunic: " .. choice_label("tunic", tunic),
        boots = "Boots: " .. choice_label("boots", boots),
        item = "Item: " .. choice_label("item", item),
        magic = "Magic: " .. choice_label("magic", magicLevel),
        magicFill = "Magic fill: " .. MAGIC_FILL[magicFillIndex] .. "%",
    }
    for key, name in pairs(names) do
        if menu[key] ~= nil then
            update_mod_menu_element_name(menu[key], name)
        end
    end
end

local function set_runtime_enabled(value)
    enabled = value
    mod_storage_save_bool("enabled", value)
    if menu.enabled ~= nil then
        update_mod_menu_element_checkbox(menu.enabled, value)
    end
end

local function stop_runtime(prefix)
    bridge_error(prefix)
    set_runtime_enabled(false)
    if bridgePresent then sl64_set_enabled(false) end
end

local function apply_loadout()
    if not booted or not levelReady then return end
    if not sl64_set_age(age) then bridge_error("Age") end
    if not sl64_set_equipment(sword, shield, tunic, boots) then
        bridge_error("Equipment")
    end
    if not sl64_set_item(item) then bridge_error("Item") end
    if not sl64_set_magic(magicLevel, magic_amount()) then
        bridge_error("Magic")
    end
end

local function act_sl64_proxy(_)
    -- The native tick runs in HOOK_MARIO_UPDATE, after CoopDX has gathered
    -- input and processed host interactions for this frame.
    return 0
end

hook_mario_action(ACT_SL64_PROXY, act_sl64_proxy)

local function on_mods_loaded()
    if not bridgePresent then
        local message = bridgeDetected and
            "Native bridge is outdated. Rebuild the pinned source checkout." or
            "Native bridge not installed. Run the pinned local source build."
        report_once(
            "missing-bridge",
            message,
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
    pendingLinkTick = false
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
    pendingLinkTick = false
    if not booted or not levelReady or not enabled or m.playerIndex ~= 0 then
        return
    end
    -- Door, star, pipe, teleport, and area-transition actions stay owned by
    -- CoopDX. Link adopts their pose before native movement resumes.
    if (m.action & ACT_GROUP_MASK) == ACT_GROUP_CUTSCENE then
        hostActionActive = true
        if not sl64_sync_link_render_from_mario(0) then
            stop_runtime("Host-action sync failed")
        end
        return
    end

    if hostActionActive then
        hostActionActive = false
        if not sl64_sync_link_from_mario(0) then
            stop_runtime("Host-action sync failed")
            set_mario_action(m, ACT_IDLE, 0)
            return
        end
    end

    pendingLinkTick = true
    m.action = ACT_SL64_PROXY
end

local function after_mario_update(m)
    if not pendingLinkTick or not booted or not levelReady or not enabled or
            m.playerIndex ~= 0 then
        return
    end
    pendingLinkTick = false
    -- Host interactions can select a transition action after the before hook.
    -- Preserve it and follow its pose instead of replacing it with the proxy.
    if (m.action & ACT_GROUP_MASK) == ACT_GROUP_CUTSCENE then
        hostActionActive = true
        if not sl64_sync_link_render_from_mario(0) then
            stop_runtime("Host-action sync failed")
        end
        return
    end
    local globalIndex = network_global_index_from_local(0)
    if not sl64_tick(0, globalIndex) then
        stop_runtime("Link update failed")
        set_mario_action(m, ACT_IDLE, 0)
        return
    end
    -- CoopDX may have adjusted the proxy during geometry or interaction work.
    -- sl64_tick has now restored the authoritative presentation pose.
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
    if not sl64_damage_link(math.min(damage * 4, 32767)) then
        bridge_error("Damage")
    end
    m.hurtCounter = 0
    m.health = 0x880
    m.invincTimer = math.max(m.invincTimer, 30)
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
    pendingLinkTick = false
end

local function on_enabled(_, value)
    if value and booted and levelReady and not enabled then
        if not sl64_sync_link_from_mario(0) then
            bridge_error("Enable sync failed")
            if menu.enabled ~= nil then
                update_mod_menu_element_checkbox(menu.enabled, false)
            end
            return
        end
    end
    if bridgePresent and not sl64_set_enabled(value) then
        bridge_error("Enable state failed")
        return
    end
    set_runtime_enabled(value)
    if not enabled and gMarioStates[0] ~= nil then
        set_mario_action(gMarioStates[0], ACT_IDLE, 0)
    end
end

local function on_audio(_, value)
    if bridgePresent and not sl64_set_audio_enabled(value) then
        bridge_error("Audio state failed")
        return
    end
    audioEnabled = value
    mod_storage_save_bool("audio", value)
end

local function on_age(_)
    local previous = age
    age = cycle({ 0, 1 }, age)
    normalize_loadout(previous)
    save_loadout()
    refresh_menu_names()
    apply_loadout()
end

local function cycle_loadout(kind)
    local valid = VALID[age][kind]
    if kind == "sword" then sword = cycle(valid, sword) end
    if kind == "shield" then shield = cycle(valid, shield) end
    if kind == "tunic" then tunic = cycle(valid, tunic) end
    if kind == "boots" then boots = cycle(valid, boots) end
    if kind == "item" then item = cycle(valid, item) end
    save_loadout()
    refresh_menu_names()
    apply_loadout()
end

local function on_magic(_)
    magicLevel = cycle({ 0, 1, 2 }, magicLevel)
    save_loadout()
    refresh_menu_names()
    apply_loadout()
end

local function on_magic_fill(_)
    magicFillIndex = magicFillIndex % #MAGIC_FILL + 1
    save_loadout()
    refresh_menu_names()
    apply_loadout()
end

local function yes_no(value)
    return value and "yes" or "no"
end

local function show_link_state()
    if not bridgePresent then
        report_once("status-no-bridge", "Native bridge is not installed.", 2)
        return
    end
    local status = sl64_get_status()
    if status == nil then
        bridge_error("Link state unavailable")
        return
    end
    local actualAge = status.linkAge == 1 and "Child" or "Adult"
    local activity = status.linkDead and "dead" or
        (status.enabled and "active" or "paused")
    local health = (status.linkHealth or 0) / 16
    local capacity = (status.linkHealthCapacity or 0) / 16
    local text = string.format(
        "%s Link - %s\nHealth: %.2f / %.2f hearts\nMagic: %d / %d\nWater: %s   Lock-on: %s\nAction: %08X   Animation: %d",
        actualAge,
        activity,
        health,
        capacity,
        status.linkMagic or 0,
        magicLevel * 0x30,
        yes_no(status.linkInWater),
        yes_no(status.linkLockOn),
        status.linkAction or 0,
        status.linkAnimId or 0
    )
    print("[Super Link 64] " .. text)
    djui_popup_create("Link state\n" .. text, 6)
end

local function show_world_diagnostics()
    if not bridgePresent then
        report_once("diagnostics-no-bridge", "Native bridge is not installed.", 2)
        return
    end
    local status = sl64_get_status()
    if status == nil then
        bridge_error("Diagnostics unavailable")
        return
    end
    local text = string.format(
        "World: %s   Render: %s\nGeometry: %d triangles in %d batches\nCollision: %d static, %d moving, %d water\nActors: %d host, %d contacts\nTexture fallbacks: %d   Dropped: %d\nAudio queue: %d   underruns: %d",
        status.worldReady and "ready" or "not ready",
        status.renderReady and "ready" or "not ready",
        status.triangles or 0,
        status.batches or 0,
        status.staticSurfaces or 0,
        status.dynamicObjects or 0,
        status.waterBoxes or 0,
        status.hostActors or 0,
        status.contactsApplied or 0,
        status.textureFallbacks or 0,
        status.droppedTriangles or 0,
        status.audioFramesQueued or 0,
        status.audioUnderruns or 0
    )
    print("[Super Link 64] " .. text)
    djui_popup_create("World diagnostics\n" .. text, 7)
end

local function restore_default_loadout()
    local previous = age
    age = 0
    sword = 2
    shield = 2
    tunic = 0
    boots = 0
    item = 6
    magicLevel = 1
    magicFillIndex = #MAGIC_FILL
    normalize_loadout(previous)
    save_loadout()
    refresh_menu_names()
    apply_loadout()
    djui_popup_create("Super Link 64\nDefault loadout restored.", 2)
end

hook_event(HOOK_ON_MODS_LOADED, on_mods_loaded)
hook_event(HOOK_ON_LEVEL_INIT, on_level_init)
hook_event(HOOK_ON_INSTANT_WARP, on_level_init)
hook_event(HOOK_BEFORE_MARIO_UPDATE, before_mario_update)
hook_event(HOOK_MARIO_UPDATE, after_mario_update)
hook_event(HOOK_BEFORE_PHYS_STEP, before_phys_step)
hook_event(HOOK_ON_INTERACT, on_interact)
hook_event(HOOK_ALLOW_HAZARD_SURFACE, allow_hazard_surface)
hook_event(HOOK_ON_EXIT, on_exit)

if bridgePresent then
    menu.enabled = hook_mod_menu_checkbox("Enable Link", enabled, on_enabled)
    menu.age = hook_mod_menu_button("Age: " .. choice_label("age", age), on_age)
    menu.sword = hook_mod_menu_button(
        "Sword: " .. choice_label("sword", sword),
        function(_) cycle_loadout("sword") end
    )
    menu.shield = hook_mod_menu_button(
        "Shield: " .. choice_label("shield", shield),
        function(_) cycle_loadout("shield") end
    )
    menu.tunic = hook_mod_menu_button(
        "Tunic: " .. choice_label("tunic", tunic),
        function(_) cycle_loadout("tunic") end
    )
    menu.boots = hook_mod_menu_button(
        "Boots: " .. choice_label("boots", boots),
        function(_) cycle_loadout("boots") end
    )
    menu.item = hook_mod_menu_button(
        "Item: " .. choice_label("item", item),
        function(_) cycle_loadout("item") end
    )
    menu.magic = hook_mod_menu_button(
        "Magic: " .. choice_label("magic", magicLevel),
        on_magic
    )
    menu.magicFill = hook_mod_menu_button(
        "Magic fill: " .. MAGIC_FILL[magicFillIndex] .. "%",
        on_magic_fill
    )
    menu.audio = hook_mod_menu_checkbox("OoT audio", audioEnabled, on_audio)
    menu.linkState = hook_mod_menu_button("Link state", show_link_state)
    menu.diagnostics = hook_mod_menu_button(
        "World diagnostics",
        show_world_diagnostics
    )
    menu.defaults = hook_mod_menu_button(
        "Restore default loadout",
        restore_default_loadout
    )
else
    local message = bridgeDetected and
        "Native bridge outdated. Rebuild the pinned source checkout." or
        "Native bridge not installed. Use the pinned source build."
    hook_mod_menu_text(message)
end
