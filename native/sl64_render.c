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
    {
        float alpha = sl64_clampf(frame->interpolationAlpha, 0.0f, 1.0f);
        size_t axis;
        for (axis = 0u; axis < 3u; ++axis) {
            float offset = frame->link.velocity[axis] * alpha;
            gSl64.render.presentationOffset[axis] =
                isfinite(offset) ? offset : 0.0f;
        }
    }
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

static float sl64_alpha_source(uint32_t source, float combined,
                               float texture, float shade,
                               const struct OoTGeometryBatch *batch)
{
    switch (source & 7u) {
    case 0u: return combined;
    case 1u:
    case 2u: return texture;
    case 3u: return batch->primitiveColor[3];
    case 4u: return shade;
    case 5u: return batch->environmentColor[3];
    case 6u: return 1.0f;
    default: return 0.0f;
    }
}

static float sl64_alpha_cycle(uint32_t a, uint32_t b, uint32_t c,
                              uint32_t d, float combined, float texture,
                              float shade,
                              const struct OoTGeometryBatch *batch)
{
    float av = sl64_alpha_source(a, combined, texture, shade, batch);
    float bv = sl64_alpha_source(b, combined, texture, shade, batch);
    float cv = sl64_alpha_source(c, combined, texture, shade, batch);
    float dv = sl64_alpha_source(d, combined, texture, shade, batch);
    return sl64_clampf((av - bv) * cv + dv, 0.0f, 1.0f);
}

static float sl64_material_alpha(const struct OoTGeometryBatch *batch,
                                 float vertexAlpha)
{
    uint32_t high = batch->combineModeHi;
    uint32_t low = batch->combineModeLo;
    float first;

    if (high == 0u && low == 0u) {
        return vertexAlpha;
    }
    /* Evaluate the captured two-cycle alpha expression with texture alpha
     * factored out as 1. The host combiner supplies the real texel alpha;
     * this preserves primitive/environment fades and vertex shade alpha. */
    first = sl64_alpha_cycle((high >> 12) & 7u, (low >> 12) & 7u,
                             (high >> 9) & 7u, (low >> 9) & 7u,
                             0.0f, 1.0f, vertexAlpha, batch);
    return sl64_alpha_cycle((low >> 21) & 7u, (low >> 3) & 7u,
                            (low >> 18) & 7u, low & 7u,
                            first, 1.0f, vertexAlpha, batch);
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

static float sl64_vertex_coordinate(float value)
{
    if (!isfinite(value)) {
        return 0.0f;
    }
    /* CoopDX's desktop Vtx uses float positions. Narrowing world-space Link
     * geometry to the original N64 s16 type destroys sub-unit motion and can
     * clamp valid host coordinates into screen-spanning triangles. */
    return value;
}

static const Sl64TextureCache *sl64_batch_texture(
    const struct OoTGeometryBatch *batch)
{
    uint16_t index = batch->textureIndex;
    const Sl64TextureCache *texture;
    size_t texels;

    if (index == UINT16_MAX || index >= gSl64.render.textureCount) {
        return NULL;
    }
    texture = &gSl64.render.textures[index];
    texels = (size_t)texture->width * texture->height;
    /* The cache is expanded RGBA32, so its byte count is not the original
     * TMEM footprint. CoopDX's load-block command accepts 2048 texels; this
     * includes OoT's 64x32 CI8 eye and shield textures after conversion. */
    if (!texture->valid || texels > 2048u) {
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
                             const Sl64TextureCache *texture,
                             const struct OoTGeometryBatch *batch)
{
    static const uint8_t reflectedOrder[3] = { 0u, 2u, 1u };
    uint32_t sourceVertex = triangle * 3u + reflectedOrder[outputVertex];
    const float *position = &gSl64.render.position[sourceVertex * 3u];
    const float *color = &gSl64.render.color[sourceVertex * 3u];
    const float *uv = &gSl64.render.uv[sourceVertex * 2u];
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float offsetZ = 0.0f;

    if (batch->sourceKind == OOT_GEOMETRY_SOURCE_LINK) {
        offsetX = gSl64.render.presentationOffset[0];
        offsetY = gSl64.render.presentationOffset[1];
        offsetZ = gSl64.render.presentationOffset[2];
    }

    destination->v.ob[0] = sl64_vertex_coordinate(
        -(position[0] + offsetX) *
            gSl64.coordinateMap.hostUnitsPerOotUnit);
    destination->v.ob[1] = sl64_vertex_coordinate(
        (position[1] + offsetY) *
            gSl64.coordinateMap.hostUnitsPerOotUnit);
    destination->v.ob[2] = sl64_vertex_coordinate(
        (position[2] + offsetZ) *
            gSl64.coordinateMap.hostUnitsPerOotUnit);
    destination->v.flag = 0u;
    destination->v.tc[0] = texture != NULL ?
                            sl64_texcoord(uv[0], texture->width) : 0;
    destination->v.tc[1] = texture != NULL ?
                            sl64_texcoord(uv[1], texture->height) : 0;
    destination->v.cn[0] = sl64_color(color[0]);
    destination->v.cn[1] = sl64_color(color[1]);
    destination->v.cn[2] = sl64_color(color[2]);
    destination->v.cn[3] = sl64_color(sl64_material_alpha(
        batch, gSl64.render.alpha[sourceVertex]));
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

    if (pass == OOT_GEOMETRY_PASS_OPAQUE) {
        gSl64.status.textureFallbacks = 0u;
    }
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
        gSPClearGeometryMode(
            gfx++, G_LIGHTING | G_CULL_BOTH | G_FOG | G_TEXTURE_GEN |
                       G_TEXTURE_GEN_LINEAR | G_LOD | G_PACKED_NORMALS_EXT |
                       G_LIGHT_MAP_EXT | G_LIGHTING_ENGINE_EXT |
                       G_CULL_INVERT_EXT | G_FRESNEL_COLOR_EXT |
                       G_FRESNEL_ALPHA_EXT);
        gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH | G_ZBUFFER);
        gDPSetCycleType(gfx++, G_CYC_1CYCLE);
        gDPSetAlphaCompare(gfx++, G_AC_NONE);
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
        /* liboot has already baked lighting and limb material tints into the
         * exported vertex colors. Replaying OoT's raw combiner would apply
         * those colors twice and can select N64-only inputs that CoopDX does
         * not provide. Use the host's stable texture/shade combinations. */
        if (texture != NULL) {
            gDPSetCombineMode(gfx++, G_CC_MODULATERGBA,
                             G_CC_MODULATERGBA);
        } else {
            gDPSetCombineMode(gfx++, G_CC_SHADE, G_CC_SHADE);
        }
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
                                 texture, batch);
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
