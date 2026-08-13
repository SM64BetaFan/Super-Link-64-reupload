/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t sl64_audio_queued(void)
{
    uint32_t readIndex = atomic_load_explicit(&gSl64.audio.readIndex,
                                              memory_order_acquire);
    uint32_t writeIndex = atomic_load_explicit(&gSl64.audio.writeIndex,
                                               memory_order_acquire);
    return writeIndex - readIndex;
}

void sl64_audio_reset(void)
{
    /* Only the consumer advances readIndex once CoopDX's audio thread is
     * running. Requesting a drain avoids resetting SPSC cursors underneath an
     * in-flight mix callback. */
    atomic_store_explicit(&gSl64.audio.resetRequested, 1u,
                          memory_order_release);
    atomic_store_explicit(&gSl64.audio.underruns, 0u, memory_order_relaxed);
    atomic_store_explicit(&gSl64.audio.overruns, 0u, memory_order_relaxed);
    gSl64.audio.producerRemainder = 0u;
    gSl64.sfx.readIndex = 0u;
    gSl64.sfx.writeIndex = 0u;
    gSl64.sfx.dropped = 0u;
}

void sl64_audio_shutdown(void)
{
    atomic_store_explicit(&gSl64.audio.enabled, 0u, memory_order_release);
    sl64_audio_reset();
}

bool sl64_set_audio_enabled(bool enabled)
{
    sl64_initialize_defaults();
    if (enabled &&
        (gSl64.status.capabilityFlags & SL64_CAP_AUDIO_S16_RING) == 0u) {
        return sl64_fail(OOT_ENGINE_RESULT_NOT_AVAILABLE,
                         "enabling Link audio");
    }
    gSl64.status.audioEnabled = enabled ? 1u : 0u;
    atomic_store_explicit(&gSl64.audio.enabled, enabled ? 1u : 0u,
                          memory_order_release);
    sl64_audio_reset();
    if (!enabled && gSl64.engine != NULL) {
        return sl64_result(oot_engine_audio_stop_all(gSl64.engine, 0u),
                           "stopping Link audio");
    }
    return true;
}

static void sl64_audio_drain_sfx(void)
{
    while (gSl64.sfx.readIndex != gSl64.sfx.writeIndex) {
        const struct OoTSfxEvent *event =
            &gSl64.sfx.events[gSl64.sfx.readIndex];
        OoTResult result = OOT_ENGINE_RESULT_OK;

        if (event->action == OOT_SFX_STOP_ID ||
            event->action == OOT_SFX_STOP_POSITION) {
            result = oot_engine_audio_sfx_stop(gSl64.engine, event->sfxId);
        } else {
            /* The callback's position is in native projected space. Until a
             * listener transform is exposed, center pan is deterministic and
             * avoids inventing spatialization. */
            result = oot_engine_audio_sfx_play(
                gSl64.engine, event->sfxId, 0.0f,
                sl64_clampf(event->volume, 0.0f, 1.0f));
        }
        gSl64.sfx.readIndex =
            (gSl64.sfx.readIndex + 1u) % SL64_SFX_QUEUE_CAPACITY;
        if (result != OOT_ENGINE_RESULT_OK &&
            result != OOT_ENGINE_RESULT_NOT_AVAILABLE) {
            (void)sl64_fail(result, "submitting Link sound effect");
            break;
        }
    }
}

uint32_t sl64_audio_produce(uint32_t frames, uint32_t sampleRate)
{
    int16_t *temporary;
    uint32_t queued;
    uint32_t available;
    uint32_t rendered = 0u;
    uint32_t writeIndex;
    uint32_t index;
    OoTResult result;

    if (gSl64.engine == NULL ||
        atomic_load_explicit(&gSl64.audio.enabled, memory_order_acquire) == 0u ||
        atomic_load_explicit(&gSl64.audio.resetRequested,
                             memory_order_acquire) != 0u ||
        frames == 0u || sampleRate < 8000u || sampleRate > 192000u) {
        return 0u;
    }
    queued = sl64_audio_queued();
    available = SL64_AUDIO_RING_FRAMES - queued;
    if (frames > available) {
        atomic_fetch_add_explicit(&gSl64.audio.overruns, 1u,
                                  memory_order_relaxed);
        frames = available;
    }
    if (frames == 0u) {
        return 0u;
    }
    temporary = (int16_t *)malloc((size_t)frames * 2u * sizeof(*temporary));
    if (temporary == NULL) {
        (void)sl64_fail(OOT_ENGINE_RESULT_OUT_OF_MEMORY,
                        "allocating Link audio producer buffer");
        return 0u;
    }
    sl64_audio_drain_sfx();
    result = oot_engine_audio_render_s16(gSl64.engine, temporary, frames,
                                         sampleRate, &rendered);
    if (result != OOT_ENGINE_RESULT_OK) {
        free(temporary);
        (void)sl64_fail(result, "rendering Link audio");
        return 0u;
    }
    if (rendered > frames) {
        free(temporary);
        (void)sl64_fail(OOT_ENGINE_RESULT_NO_FRAME,
                        "validating Link audio frame count");
        return 0u;
    }
    writeIndex = atomic_load_explicit(&gSl64.audio.writeIndex,
                                      memory_order_relaxed);
    for (index = 0u; index < rendered; ++index) {
        uint32_t destination = (writeIndex + index) & SL64_AUDIO_RING_MASK;
        gSl64.audio.samples[destination * 2u] = temporary[index * 2u];
        gSl64.audio.samples[destination * 2u + 1u] =
            temporary[index * 2u + 1u];
    }
    free(temporary);
    atomic_store_explicit(&gSl64.audio.writeIndex, writeIndex + rendered,
                          memory_order_release);
    return rendered;
}

void sl64_audio_tick(void)
{
    uint32_t queued;
    uint32_t target = (SL64_AUDIO_RATE * 3u) / 20u; /* 150 ms */

    if (atomic_load_explicit(&gSl64.audio.enabled, memory_order_acquire) == 0u ||
        gSl64.engine == NULL) {
        return;
    }
    queued = sl64_audio_queued();
    if (queued < target) {
        (void)sl64_audio_produce(target - queued, SL64_AUDIO_RATE);
    }
}

static int16_t sl64_saturate(int32_t sample)
{
    if (sample < -32768) {
        return -32768;
    }
    if (sample > 32767) {
        return 32767;
    }
    return (int16_t)sample;
}

uint32_t sl64_audio_mix_s16(int16_t *stereo, uint32_t frames, float gain)
{
    uint32_t readIndex;
    uint32_t writeIndex;
    uint32_t available;
    uint32_t mixFrames;
    uint32_t index;

    if (atomic_exchange_explicit(&gSl64.audio.resetRequested, 0u,
                                 memory_order_acq_rel) != 0u) {
        writeIndex = atomic_load_explicit(&gSl64.audio.writeIndex,
                                          memory_order_acquire);
        atomic_store_explicit(&gSl64.audio.readIndex, writeIndex,
                              memory_order_release);
        return 0u;
    }
    if (stereo == NULL || frames == 0u || !isfinite(gain) || gain <= 0.0f ||
        atomic_load_explicit(&gSl64.audio.enabled, memory_order_acquire) == 0u) {
        return 0u;
    }
    gain = sl64_clampf(gain, 0.0f, 4.0f);
    readIndex = atomic_load_explicit(&gSl64.audio.readIndex,
                                     memory_order_relaxed);
    writeIndex = atomic_load_explicit(&gSl64.audio.writeIndex,
                                      memory_order_acquire);
    available = writeIndex - readIndex;
    mixFrames = frames < available ? frames : available;
    if (mixFrames < frames) {
        atomic_fetch_add_explicit(&gSl64.audio.underruns, 1u,
                                  memory_order_relaxed);
    }
    for (index = 0u; index < mixFrames; ++index) {
        uint32_t source = (readIndex + index) & SL64_AUDIO_RING_MASK;
        int32_t left = stereo[index * 2u] +
                       (int32_t)lrintf(gSl64.audio.samples[source * 2u] * gain);
        int32_t right = stereo[index * 2u + 1u] +
                        (int32_t)lrintf(
                            gSl64.audio.samples[source * 2u + 1u] * gain);
        stereo[index * 2u] = sl64_saturate(left);
        stereo[index * 2u + 1u] = sl64_saturate(right);
    }
    atomic_store_explicit(&gSl64.audio.readIndex, readIndex + mixFrames,
                          memory_order_release);
    return mixFrames;
}
