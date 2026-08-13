/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Cycl0o0
 */

#include "sl64_internal.h"

#include "engine/graph_node.h"
#include "game/level_update.h"
#include "game/memory.h"
#include "sm64.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool sl64_resize(void **buffer, size_t bytes, const char *operation)
{
    void *resized;

    if (bytes == 0u) {
        return true;
    }
    resized = realloc(*buffer, bytes);
    if (resized == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_OUT_OF_MEMORY, operation);
    }
    *buffer = resized;
    return true;
}

static bool sl64_render_reserve(uint32_t triangles, uint32_t batches)
{
    Sl64RenderCache *render = &gSl64.render;

    if (triangles > render->triangleCapacity) {
        size_t vertices = (size_t)triangles * 3u;
        if (!sl64_resize((void **)&render->position,
                         vertices * 3u * sizeof(float),
                         "growing Link position cache") ||
            !sl64_resize((void **)&render->normal,
                         vertices * 3u * sizeof(float),
                         "growing Link normal cache") ||
            !sl64_resize((void **)&render->color,
                         vertices * 3u * sizeof(float),
                         "growing Link color cache") ||
            !sl64_resize((void **)&render->uv,
                         vertices * 2u * sizeof(float),
                         "growing Link UV cache") ||
            !sl64_resize((void **)&render->alpha,
                         vertices * sizeof(float),
                         "growing Link alpha cache") ||
            !sl64_resize((void **)&render->triTexture,
                         (size_t)triangles * sizeof(uint16_t),
                         "growing Link texture-index cache") ||
            !sl64_resize((void **)&render->triFlags,
                         (size_t)triangles * sizeof(uint8_t),
                         "growing Link triangle-state cache")) {
            return false;
        }
        render->triangleCapacity = triangles;
    }
    if (batches > render->batchCapacity) {
        if (!sl64_resize((void **)&render->batches,
                         (size_t)batches * sizeof(*render->batches),
                         "growing Link batch cache")) {
            return false;
        }
        render->batchCapacity = batches;
    }
    return true;
}

static bool sl64_texture_cache_resize(uint32_t count)
{
    Sl64TextureCache *resized;
    uint32_t index;

    if (count == gSl64.render.textureCount) {
        return true;
    }
    if (count == 0u) {
        for (index = 0u; index < gSl64.render.textureCount; ++index) {
            free(gSl64.render.textures[index].rgba);
        }
        free(gSl64.render.textures);
        gSl64.render.textures = NULL;
        gSl64.render.textureCount = 0u;
        return true;
    }
    if (count < gSl64.render.textureCount) {
        for (index = count; index < gSl64.render.textureCount; ++index) {
            free(gSl64.render.textures[index].rgba);
            gSl64.render.textures[index].rgba = NULL;
            gSl64.render.textures[index].rgbaSize = 0u;
            gSl64.render.textures[index].valid = 0u;
        }
    }
    resized = (Sl64TextureCache *)realloc(
        gSl64.render.textures, (size_t)count * sizeof(*resized));
    if (count != 0u && resized == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_OUT_OF_MEMORY,
                         "growing Link texture cache");
    }
    if (count > gSl64.render.textureCount) {
        memset(resized + gSl64.render.textureCount, 0,
               (size_t)(count - gSl64.render.textureCount) *
                   sizeof(*resized));
    }
    gSl64.render.textures = resized;
    gSl64.render.textureCount = count;
    return true;
}

static bool sl64_capture_textures(void)
{
    uint32_t count = 0u;
    uint32_t index;
    OoTResult result;

    if ((gSl64.status.capabilityFlags &
         SL64_CAP_RENDER_RGBA32_TEXTURES) == 0u) {
        return sl64_texture_cache_resize(0u);
    }
    result = oot_engine_texture_count(gSl64.engine, &count);
    if (result != OOT_ENGINE_RESULT_OK) {
        return sl64_fail(result, "querying Link textures");
    }
    if (!sl64_texture_cache_resize(count)) {
        return false;
    }
    for (index = 0u; index < count; ++index) {
        OoTEngineTexture source;
        Sl64TextureCache *destination = &gSl64.render.textures[index];
        uint8_t *pixels;
        size_t expected;

        memset(&source, 0, sizeof(source));
        source.structSize = (uint32_t)sizeof(source);
        result = oot_engine_texture_get(gSl64.engine, index, &source);
        if (result == OOT_ENGINE_RESULT_NOT_AVAILABLE) {
            destination->valid = 0u;
            continue;
        }
        if (result != OOT_ENGINE_RESULT_OK) {
            return sl64_fail(result, "copying Link texture");
        }
        expected = (size_t)source.width * source.height * 4u;
        if (source.rgbaPixels == NULL || source.width == 0u ||
            source.height == 0u || source.rgbaSize != expected) {
            destination->valid = 0u;
            continue;
        }
        if (destination->valid && destination->revision == source.revision &&
            destination->rgbaSize == source.rgbaSize) {
            continue;
        }
        pixels = (uint8_t *)realloc(destination->rgba, source.rgbaSize);
        if (pixels == NULL) {
            return sl64_fail(OOT_ENGINE_RESULT_OUT_OF_MEMORY,
                             "copying Link texture pixels");
        }
        memcpy(pixels, source.rgbaPixels, source.rgbaSize);
        destination->rgba = pixels;
        destination->rgbaSize = source.rgbaSize;
        destination->revision = source.revision;
        destination->width = source.width;
        destination->height = source.height;
        destination->wrapS = source.wrapS;
        destination->wrapT = source.wrapT;
        destination->valid = 1u;
    }
    return true;
}

bool sl64_render_capture(const OoTEngineFrame *frame)
{
    const OoTEngineGeometry *geometry;
    size_t vertices;
    uint32_t triangles;
    uint32_t batches;

    if (frame == NULL) {
        return sl64_fail(OOT_ENGINE_RESULT_INVALID_ARGUMENT,
                         "capturing Link render frame");
    }
    geometry = &frame->geometry;
    triangles = geometry->numTriangles;
    batches = frame->geometryBatchCount;
    if (triangles != 0u &&
        (geometry->position == NULL || geometry->normal == NULL ||
         geometry->color == NULL || geometry->uv == NULL)) {
        return sl64_fail(OOT_ENGINE_RESULT_NO_FRAME,
                         "Link render frame is incomplete");
    }
    if (!sl64_render_reserve(triangles, batches)) {
        return false;
    }
    vertices = (size_t)triangles * 3u;
    if (triangles != 0u) {
        memcpy(gSl64.render.position, geometry->position,
               vertices * 3u * sizeof(float));
        memcpy(gSl64.render.normal, geometry->normal,
               vertices * 3u * sizeof(float));
        memcpy(gSl64.render.color, geometry->color,
               vertices * 3u * sizeof(float));
        memcpy(gSl64.render.uv, geometry->uv,
               vertices * 2u * sizeof(float));
        if (geometry->alpha != NULL) {
            memcpy(gSl64.render.alpha, geometry->alpha,
                   vertices * sizeof(float));
        } else {
            size_t index;
            for (index = 0u; index < vertices; ++index) {
                gSl64.render.alpha[index] = 1.0f;
            }
        }
        if (geometry->triTexture != NULL) {
            memcpy(gSl64.render.triTexture, geometry->triTexture,
                   (size_t)triangles * sizeof(uint16_t));
        } else {
            memset(gSl64.render.triTexture, 0xff,
                   (size_t)triangles * sizeof(uint16_t));
        }
        if (geometry->triFlags != NULL) {
            memcpy(gSl64.render.triFlags, geometry->triFlags,
                   (size_t)triangles * sizeof(uint8_t));
        } else {
            memset(gSl64.render.triFlags, 0,
                   (size_t)triangles * sizeof(uint8_t));
        }
    }
    if (batches != 0u && frame->geometryBatches != NULL) {
        memcpy(gSl64.render.batches, frame->geometryBatches,
               (size_t)batches * sizeof(*gSl64.render.batches));
    } else {
        batches = 0u;
    }
    gSl64.render.triangleCount = triangles;
    gSl64.render.batchCount = batches;
    gSl64.status.triangles = triangles;
    gSl64.status.batches = batches;
    if (frame->linkGeometryTruncated != 0u) {
        /* The checked frame exposes truncation as a flag, not an exact count.
         * Preserve world-import drops and record at least one render drop. */
        gSl64.status.droppedTriangles++;
    }
    if (!sl64_capture_textures()) {
        return false;
    }
    gSl64.status.renderReady = triangles != 0u;
    return true;
}

static uint8_t sl64_color(float value)
{
    if (!isfinite(value)) {
        return 255u;
    }
    return (uint8_t)lrintf(sl64_clampf(value, 0.0f, 1.0f) * 255.0f);
}

static int16_t sl64_texcoord(float value, uint16_t extent)
{
    float converted = value * (float)extent * 32.0f;
    if (!isfinite(converted)) {
        return 0;
    }
    converted = sl64_clampf(converted, -32768.0f, 32767.0f);
    return (int16_t)lrintf(converted);
}

static int16_t sl64_vertex_coordinate(float value)
{
    if (!isfinite(value)) {
        return 0;
    }
    value = sl64_clampf(value, -32768.0f, 32767.0f);
    return (int16_t)lrintf(value);
}

static const Sl64TextureCache *sl64_batch_texture(
    const struct OoTGeometryBatch *batch)
{
    uint16_t index = batch->textureIndex;
    const Sl64TextureCache *texture;

    if (index == UINT16_MAX || index >= gSl64.render.textureCount) {
        return NULL;
    }
    texture = &gSl64.render.textures[index];
    /* One N64 TMEM load holds at most 4096 bytes. Larger source textures are
     * rendered shaded until a tiled uploader is implemented. */
    if (!texture->valid || texture->rgbaSize > 4096u) {
        return NULL;
    }
    return texture;
}

static uint32_t sl64_pass_triangles(uint8_t pass)
{
    uint32_t count = 0u;
    uint32_t index;

    if (gSl64.render.batchCount == 0u) {
        return pass == OOT_GEOMETRY_PASS_OPAQUE ?
               gSl64.render.triangleCount : 0u;
    }
    for (index = 0u; index < gSl64.render.batchCount; ++index) {
        const struct OoTGeometryBatch *batch = &gSl64.render.batches[index];
        if (batch->renderPass == pass &&
            batch->firstTriangle < gSl64.render.triangleCount) {
            uint32_t available = gSl64.render.triangleCount -
                                 batch->firstTriangle;
            count += batch->numTriangles < available ?
                     batch->numTriangles : available;
        }
    }
    return count;
}

static void sl64_fill_vertex(Vtx *destination, uint32_t triangle,
                             uint32_t outputVertex,
                             const Sl64TextureCache *texture)
{
    static const uint8_t reflectedOrder[3] = { 0u, 2u, 1u };
    uint32_t sourceVertex = triangle * 3u + reflectedOrder[outputVertex];
    const float *position = &gSl64.render.position[sourceVertex * 3u];
    const float *color = &gSl64.render.color[sourceVertex * 3u];
    const float *uv = &gSl64.render.uv[sourceVertex * 2u];

    destination->v.ob[0] = sl64_vertex_coordinate(
        -position[0] * gSl64.coordinateMap.hostUnitsPerOotUnit);
    destination->v.ob[1] = sl64_vertex_coordinate(
        position[1] * gSl64.coordinateMap.hostUnitsPerOotUnit);
    destination->v.ob[2] = sl64_vertex_coordinate(
        position[2] * gSl64.coordinateMap.hostUnitsPerOotUnit);
    destination->v.flag = 0u;
    destination->v.tc[0] = texture != NULL ?
                            sl64_texcoord(uv[0], texture->width) : 0;
    destination->v.tc[1] = texture != NULL ?
                            sl64_texcoord(uv[1], texture->height) : 0;
    destination->v.cn[0] = sl64_color(color[0]);
    destination->v.cn[1] = sl64_color(color[1]);
    destination->v.cn[2] = sl64_color(color[2]);
    destination->v.cn[3] = sl64_color(gSl64.render.alpha[sourceVertex]);
}

static Gfx *sl64_render_pass(uint8_t pass, struct GraphNode *node)
{
    struct OoTGeometryBatch fallback;
    uint32_t passTriangles = sl64_pass_triangles(pass);
    uint32_t batchCount = gSl64.render.batchCount;
    uint32_t commandCapacity;
    Gfx *head;
    Gfx *gfx;
    Vtx *vertices;
    uint32_t vertexCursor = 0u;
    uint32_t batchIndex;

    if (passTriangles == 0u || node == NULL) {
        return NULL;
    }
    node->flags = (node->flags & 0xffu) |
                  ((pass == OOT_GEOMETRY_PASS_TRANSLUCENT ?
                    LAYER_TRANSPARENT : LAYER_OPAQUE) << 8);
    commandCapacity = passTriangles * 2u +
                      (batchCount != 0u ? batchCount : 1u) * 20u + 16u;
    head = (Gfx *)alloc_display_list(commandCapacity * sizeof(*head));
    vertices = (Vtx *)alloc_display_list(
        passTriangles * 3u * sizeof(*vertices));
    if (head == NULL || vertices == NULL) {
        return NULL;
    }
    gfx = head;
    memset(&fallback, 0, sizeof(fallback));
    if (batchCount == 0u) {
        fallback.numTriangles = gSl64.render.triangleCount;
        fallback.textureIndex = UINT16_MAX;
        fallback.renderPass = OOT_GEOMETRY_PASS_OPAQUE;
        fallback.depthFlags = OOT_GEOMETRY_DEPTH_TEST |
                              OOT_GEOMETRY_DEPTH_WRITE;
        batchCount = 1u;
    }

    for (batchIndex = 0u; batchIndex < batchCount; ++batchIndex) {
        const struct OoTGeometryBatch *batch =
            gSl64.render.batchCount != 0u ?
            &gSl64.render.batches[batchIndex] : &fallback;
        const Sl64TextureCache *texture;
        uint32_t triangleCount;
        uint32_t triangleOffset;
        uint32_t triangle;

        if (batch->renderPass != pass ||
            batch->firstTriangle >= gSl64.render.triangleCount) {
            continue;
        }
        triangleCount = gSl64.render.triangleCount - batch->firstTriangle;
        if (triangleCount > batch->numTriangles) {
            triangleCount = batch->numTriangles;
        }
        texture = sl64_batch_texture(batch);
        if (batch->textureIndex != UINT16_MAX && texture == NULL) {
            gSl64.status.textureFallbacks++;
        }

        gDPPipeSync(gfx++);
        gSPClearGeometryMode(gfx++, G_LIGHTING | G_CULL_BOTH);
        gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
        if ((batch->triangleFlags & OOT_TRI_CULL_FRONT) != 0u) {
            gSPSetGeometryMode(gfx++, G_CULL_FRONT);
        } else if ((batch->triangleFlags & OOT_TRI_CULL_BACK) != 0u) {
            gSPSetGeometryMode(gfx++, G_CULL_BACK);
        }
        if (pass == OOT_GEOMETRY_PASS_TRANSLUCENT) {
            gDPSetRenderMode(gfx++, G_RM_AA_ZB_XLU_SURF,
                            G_RM_AA_ZB_XLU_SURF2);
        } else if ((batch->triangleFlags & OOT_TRI_ALPHA_TEST) != 0u) {
            gDPSetRenderMode(gfx++, G_RM_AA_ZB_TEX_EDGE,
                            G_RM_AA_ZB_TEX_EDGE2);
        } else {
            gDPSetRenderMode(gfx++, G_RM_AA_ZB_OPA_SURF,
                            G_RM_AA_ZB_OPA_SURF2);
        }
        if (batch->combineModeHi != 0u) {
            gfx->words.w0 = batch->combineModeHi;
            gfx->words.w1 = batch->combineModeLo;
            gfx++;
        } else if (texture != NULL) {
            gDPSetCombineMode(gfx++, G_CC_MODULATERGBA,
                             G_CC_MODULATERGBA);
        } else {
            gDPSetCombineMode(gfx++, G_CC_SHADE, G_CC_SHADE);
        }
        gDPSetPrimColor(gfx++, 0, 0,
                        sl64_color(batch->primitiveColor[0]),
                        sl64_color(batch->primitiveColor[1]),
                        sl64_color(batch->primitiveColor[2]),
                        sl64_color(batch->primitiveColor[3]));
        gDPSetEnvColor(gfx++, sl64_color(batch->environmentColor[0]),
                       sl64_color(batch->environmentColor[1]),
                       sl64_color(batch->environmentColor[2]),
                       sl64_color(batch->environmentColor[3]));
        if (texture != NULL) {
            gSPTexture(gfx++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_ON);
            gDPLoadTextureBlock(gfx++, texture->rgba, G_IM_FMT_RGBA,
                                G_IM_SIZ_32b, texture->width,
                                texture->height, 0,
                                texture->wrapS, texture->wrapT,
                                G_TX_NOMASK, G_TX_NOMASK,
                                G_TX_NOLOD, G_TX_NOLOD);
        } else {
            gSPTexture(gfx++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_OFF);
        }

        for (triangle = 0u; triangle < triangleCount; ++triangle) {
            uint32_t vertex;
            for (vertex = 0u; vertex < 3u; ++vertex) {
                sl64_fill_vertex(&vertices[vertexCursor + triangle * 3u +
                                             vertex],
                                 batch->firstTriangle + triangle, vertex,
                                 texture);
            }
        }
        for (triangleOffset = 0u; triangleOffset < triangleCount;
             triangleOffset += 10u) {
            uint32_t chunk = triangleCount - triangleOffset;
            uint32_t local;
            if (chunk > 10u) {
                chunk = 10u;
            }
            gSPVertex(gfx++, vertices + vertexCursor + triangleOffset * 3u,
                      chunk * 3u, 0);
            for (local = 0u; local < chunk; ++local) {
                uint32_t first = local * 3u;
                gSP1Triangle(gfx++, first, first + 1u, first + 2u, 0);
            }
        }
        vertexCursor += triangleCount * 3u;
    }
    gDPPipeSync(gfx++);
    gSPTexture(gfx++, 0xffff, 0xffff, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(gfx++, G_CULL_FRONT);
    gSPSetGeometryMode(gfx++, G_LIGHTING | G_CULL_BACK);
    gSPEndDisplayList(gfx++);
    return head;
}

Gfx *sl64_geo_render_opaque(s32 callContext, struct GraphNode *node,
                            void *context)
{
    (void)context;
    if (callContext != GEO_CONTEXT_RENDER ||
        !sl64_should_hide_mario_proxy(gMarioStates[0].marioObj)) {
        return NULL;
    }
    return sl64_render_pass(OOT_GEOMETRY_PASS_OPAQUE, node);
}

Gfx *sl64_geo_render_translucent(s32 callContext, struct GraphNode *node,
                                 void *context)
{
    (void)context;
    if (callContext != GEO_CONTEXT_RENDER ||
        !sl64_should_hide_mario_proxy(gMarioStates[0].marioObj)) {
        return NULL;
    }
    return sl64_render_pass(OOT_GEOMETRY_PASS_TRANSLUCENT, node);
}

void sl64_render_reset(void)
{
    uint32_t index;

    free(gSl64.render.position);
    free(gSl64.render.normal);
    free(gSl64.render.color);
    free(gSl64.render.uv);
    free(gSl64.render.alpha);
    free(gSl64.render.triTexture);
    free(gSl64.render.triFlags);
    free(gSl64.render.batches);
    for (index = 0u; index < gSl64.render.textureCount; ++index) {
        free(gSl64.render.textures[index].rgba);
    }
    free(gSl64.render.textures);
    memset(&gSl64.render, 0, sizeof(gSl64.render));
    gSl64.status.renderReady = 0u;
    gSl64.status.triangles = 0u;
    gSl64.status.batches = 0u;
}
