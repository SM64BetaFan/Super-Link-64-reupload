/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_internal.h"

#include "game/interaction.h"
#include "game/level_update.h"
#include "game/object_list_processor.h"
#include "object_constants.h"
#include "object_fields.h"

#include <math.h>
#include <string.h>

static uint32_t sl64_hostile_interact_mask(void)
{
    return INTERACT_DAMAGE | INTERACT_KOOPA | INTERACT_SPINY_WALKING |
           INTERACT_BREAKABLE | INTERACT_BOUNCE_TOP | INTERACT_BULLY |
           INTERACT_FLAME | INTERACT_BOUNCE_TOP2 | INTERACT_MR_BLIZZARD |
           INTERACT_CLAM_OR_BUBBA | INTERACT_SNUFIT_BULLET |
           INTERACT_SHOCK;
}

static Sl64ActorSlot *sl64_actor_slot(uint64_t userTag, bool create)
{
    Sl64ActorSlot *empty = NULL;
    uint32_t index;

    for (index = 0u; index < SL64_MAX_HOST_ACTORS; ++index) {
        Sl64ActorSlot *slot = &gSl64.actors[index];
        if (slot->used && slot->userTag == userTag) {
            return slot;
        }
        if (!slot->used && empty == NULL) {
            empty = slot;
        }
    }
    if (create && empty != NULL) {
        empty->used = 1u;
        empty->userTag = userTag;
    }
    return create ? empty : NULL;
}

static bool sl64_live_object(const struct Object *object)
{
    uintptr_t address = (uintptr_t)object;
    uintptr_t begin = (uintptr_t)&gObjectPool[0];
    uintptr_t end = (uintptr_t)&gObjectPool[OBJECT_POOL_CAPACITY];

    return address >= begin && address < end &&
           (address - begin) % sizeof(gObjectPool[0]) == 0u &&
           (object->activeFlags & ACTIVE_FLAG_ACTIVE) != 0;
}

static bool sl64_safe_hostile_object(const struct Object *object)
{
    return sl64_live_object(object) &&
           object != gMarioStates[0].marioObj &&
           (object->oInteractType & sl64_hostile_interact_mask()) != 0u &&
           isfinite(object->oPosX) && isfinite(object->oPosY) &&
           isfinite(object->oPosZ) && isfinite(object->hitboxHeight) &&
           isfinite(object->hitboxRadius) &&
           isfinite(object->hurtboxHeight) &&
           isfinite(object->hurtboxRadius) &&
           isfinite(object->hitboxDownOffset);
}

static void sl64_actor_count(void)
{
    uint32_t count = 0u;
    uint32_t index;

    for (index = 0u; index < SL64_MAX_HOST_ACTORS; ++index) {
        if (gSl64.actors[index].used) {
            count++;
        }
    }
    gSl64.status.hostActors = count;
}

bool sl64_host_actor_upsert(const Sl64HostActor *actor)
{
    struct OoTHostActorState state;
    Sl64ActorSlot *slot;
    Sl64Vec3 host;
    Sl64Vec3 oot;
    OoTResult result;

    if ((gSl64.status.capabilityFlags & SL64_CAP_HOST_ACTORS) == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_AVAILABLE,
                         "registering CoopDX actor");
    }
    if (gSl64.engine == NULL || actor == NULL || actor->userTag == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "registering CoopDX actor");
    }
    memset(&state, 0, sizeof(state));
    state.structSize = (uint32_t)sizeof(state);
    state.version = OOT_HOST_ACTOR_STATE_VERSION;
    state.userTag = actor->userTag;
    state.typeId = actor->typeId;
    state.flags = actor->flags;
    host.x = actor->position[0];
    host.y = actor->position[1];
    host.z = actor->position[2];
    if (!sl64_host_position_to_oot(&gSl64.coordinateMap, &host, &oot)) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "converting CoopDX actor position");
    }
    state.position[0] = oot.x;
    state.position[1] = oot.y;
    state.position[2] = oot.z;
    state.focusOffset[0] = -actor->focusOffset[0] /
                           gSl64.coordinateMap.hostUnitsPerOotUnit;
    state.focusOffset[1] = actor->focusOffset[1] /
                           gSl64.coordinateMap.hostUnitsPerOotUnit;
    state.focusOffset[2] = actor->focusOffset[2] /
                           gSl64.coordinateMap.hostUnitsPerOotUnit;
    state.hurtRadius = actor->hurtRadius /
                       gSl64.coordinateMap.hostUnitsPerOotUnit;
    state.hurtHeight = actor->hurtHeight /
                       gSl64.coordinateMap.hostUnitsPerOotUnit;
    state.hurtYOffset = actor->hurtYOffset /
                        gSl64.coordinateMap.hostUnitsPerOotUnit;
    state.rotation[0] = actor->rotation[0];
    state.rotation[1] = sl64_host_yaw_to_oot(actor->rotation[1]);
    state.rotation[2] = actor->rotation[2];
    state.room = actor->room;
    state.attentionRange = actor->attentionRange;

    slot = sl64_actor_slot(actor->userTag, true);
    if (slot == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_HOST_ACTOR_CAPACITY,
                         "CoopDX actor registry is full");
    }
    if (slot->handle == OOT_ENGINE_INVALID_HOST_ACTOR) {
        result = oot_engine_host_actor_create(gSl64.engine, &state,
                                               &slot->handle);
        if (result != OOT_ENGINE_RESULT_OK) {
            memset(slot, 0, sizeof(*slot));
            return sl64_fail(result, "creating CoopDX host actor");
        }
    } else {
        result = oot_engine_host_actor_update(gSl64.engine, slot->handle,
                                               &state);
        if (result == OOT_ENGINE_RESULT_HOST_ACTOR_NOT_FOUND) {
            slot->handle = OOT_ENGINE_INVALID_HOST_ACTOR;
            result = oot_engine_host_actor_create(gSl64.engine, &state,
                                                   &slot->handle);
            if (result != OOT_ENGINE_RESULT_OK) {
                memset(slot, 0, sizeof(*slot));
            }
        }
        if (result != OOT_ENGINE_RESULT_OK) {
            return sl64_fail(result, "updating CoopDX host actor");
        }
    }
    sl64_actor_count();
    return true;
}

bool sl64_host_actor_remove(uint64_t userTag)
{
    Sl64ActorSlot *slot;
    OoTResult result;

    if (gSl64.engine == NULL || userTag == 0u) {
        return false;
    }
    slot = sl64_actor_slot(userTag, false);
    if (slot == NULL) {
        return true;
    }
    result = oot_engine_host_actor_remove(gSl64.engine, slot->handle);
    if (result != OOT_ENGINE_RESULT_OK &&
        result != OOT_ENGINE_RESULT_HOST_ACTOR_NOT_FOUND) {
        return sl64_fail(result, "removing CoopDX host actor");
    }
    memset(slot, 0, sizeof(*slot));
    sl64_actor_count();
    return true;
}

void sl64_host_actors_clear(void)
{
    sl64_actor_reset();
}

bool sl64_host_actor_from_object(struct Object *object)
{
    Sl64HostActor actor;
    Sl64ActorSlot *slot;
    uint32_t interactType;

    if (!sl64_safe_hostile_object(object)) {
        return false;
    }
    interactType = object->oInteractType;
    memset(&actor, 0, sizeof(actor));
    actor.userTag = (uint64_t)(uintptr_t)object;
    actor.typeId = interactType;
    actor.flags = OOT_HOST_ACTOR_ENABLED | OOT_HOST_ACTOR_TARGETABLE |
                  OOT_HOST_ACTOR_HOSTILE | OOT_HOST_ACTOR_HURT;
    if ((object->activeFlags & ACTIVE_FLAG_IN_DIFFERENT_ROOM) != 0 ||
        object->oIntangibleTimer != 0) {
        actor.flags &= ~(uint32_t)(OOT_HOST_ACTOR_TARGETABLE |
                                   OOT_HOST_ACTOR_HURT);
    }
    actor.position[0] = object->oPosX;
    actor.position[1] = object->oPosY;
    actor.position[2] = object->oPosZ;
    actor.focusOffset[1] = object->hitboxHeight * 0.65f;
    actor.hurtRadius = object->hurtboxRadius > 0.0f ?
                       object->hurtboxRadius : object->hitboxRadius;
    actor.hurtHeight = object->hurtboxHeight > 0.0f ?
                       object->hurtboxHeight : object->hitboxHeight;
    if (actor.hurtRadius < 0.0f) {
        actor.hurtRadius = 0.0f;
    }
    if (actor.hurtHeight < 0.0f) {
        actor.hurtHeight = 0.0f;
    }
    actor.hurtYOffset = object->hitboxDownOffset;
    actor.rotation[1] = (int16_t)object->oFaceAngleYaw;
    actor.room = -1;
    actor.attentionRange = 3u;
    if (!sl64_host_actor_upsert(&actor)) {
        return false;
    }
    slot = sl64_actor_slot(actor.userTag, false);
    if (slot != NULL) {
        slot->object = object;
        slot->seen = 1u;
    }
    return true;
}

bool sl64_actor_sync_objects(void)
{
    uint32_t index;

    if ((gSl64.status.capabilityFlags & SL64_CAP_HOST_ACTORS) == 0u) {
        return true;
    }
    if (gSl64.engine == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_INITIALIZED,
                         "synchronizing CoopDX actors");
    }

    for (index = 0u; index < SL64_MAX_HOST_ACTORS; ++index) {
        if (gSl64.actors[index].used &&
            gSl64.actors[index].object != NULL) {
            gSl64.actors[index].seen = 0u;
        }
    }
    for (index = 0u; index < OBJECT_POOL_CAPACITY; ++index) {
        struct Object *object = &gObjectPool[index];
        Sl64ActorSlot *slot;

        if (!sl64_safe_hostile_object(object)) {
            continue;
        }
        slot = sl64_actor_slot((uint64_t)(uintptr_t)object, false);
        if (slot != NULL && slot->object != NULL) {
            slot->seen = 1u;
        }
    }
    for (index = 0u; index < SL64_MAX_HOST_ACTORS; ++index) {
        Sl64ActorSlot *slot = &gSl64.actors[index];

        if (slot->used && slot->object != NULL && slot->seen == 0u) {
            uint64_t userTag = slot->userTag;
            if (!sl64_host_actor_remove(userTag)) {
                return false;
            }
        }
    }
    for (index = 0u; index < OBJECT_POOL_CAPACITY; ++index) {
        struct Object *object = &gObjectPool[index];

        if (sl64_safe_hostile_object(object) &&
            !sl64_host_actor_from_object(object)) {
            return false;
        }
    }
    sl64_actor_count();
    return true;
}

void sl64_on_object_unload(struct Object *object)
{
    if (object != NULL) {
        (void)sl64_host_actor_remove((uint64_t)(uintptr_t)object);
    }
}

bool sl64_poll_contact(Sl64ActorContact *outContact)
{
    OoTEngineActorContact contact;
    OoTResult result;

    if (gSl64.engine == NULL || outContact == NULL) {
        return false;
    }
    memset(&contact, 0, sizeof(contact));
    contact.structSize = (uint32_t)sizeof(contact);
    contact.version = OOT_HOST_ACTOR_CONTACT_VERSION;
    result = oot_engine_host_actor_poll_contact(gSl64.engine, &contact);
    if (result == OOT_ENGINE_RESULT_NOT_AVAILABLE) {
        return false;
    }
    if (result != OOT_ENGINE_RESULT_OK) {
        (void)sl64_fail(result, "polling Link actor contacts");
        return false;
    }
    outContact->userTag = contact.userTag;
    outContact->source = contact.source;
    outContact->sourceActorId = contact.sourceActorId;
    outContact->simulationTick = contact.simulationTick;
    outContact->position[0] = -contact.position[0] *
                              gSl64.coordinateMap.hostUnitsPerOotUnit;
    outContact->position[1] = contact.position[1] *
                              gSl64.coordinateMap.hostUnitsPerOotUnit;
    outContact->position[2] = contact.position[2] *
                              gSl64.coordinateMap.hostUnitsPerOotUnit;
    return true;
}

void sl64_actor_apply_contacts(void)
{
    Sl64ActorContact contact;

    while (sl64_poll_contact(&contact)) {
        Sl64ActorSlot *slot = sl64_actor_slot(contact.userTag, false);
        if (slot != NULL && slot->object != NULL &&
            sl64_live_object(slot->object)) {
            uint32_t attackType = ATTACK_PUNCH;
            if (contact.source == OOT_HOST_CONTACT_ARROW ||
                contact.source == OOT_HOST_CONTACT_BOOMERANG ||
                contact.source == OOT_HOST_CONTACT_HOOKSHOT) {
                attackType = ATTACK_FAST_ATTACK;
            } else if (contact.source == OOT_HOST_CONTACT_BOMB) {
                attackType = ATTACK_GROUND_POUND_OR_TWIRL;
            }
            slot->object->oInteractStatus |=
                (s32)(attackType | INT_STATUS_WAS_ATTACKED |
                      INT_STATUS_INTERACTED);
            gSl64.status.contactsApplied++;
        } else if (slot != NULL && slot->object != NULL) {
            (void)sl64_host_actor_remove(contact.userTag);
        }
    }
}

void sl64_actor_reset(void)
{
    if (gSl64.engine != NULL) {
        (void)oot_engine_host_actors_clear(gSl64.engine);
    }
    memset(gSl64.actors, 0, sizeof(gSl64.actors));
    gSl64.status.hostActors = 0u;
}
