#include "ninja_binary.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(float) == 4, "Ninja assets use binary32");

typedef struct {
    const uint8_t *data;
    size_t size, top;
} Binary;

static uint32_t half(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t word(const uint8_t *p) { return half(p) | half(p + 2) << 16; }
static int32_t signed_word(uint32_t v)
{
    return v <= INT32_MAX ? (int32_t)v : -1 - (int32_t)(UINT32_MAX - v);
}
static float real(const uint8_t *p)
{
    uint32_t bits = word(p);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
static const uint8_t *relative(Binary b, uint32_t offset, size_t bytes)
{
    if (offset > INT32_MAX || offset > b.size - b.top ||
        bytes > b.size - b.top - offset) return NULL;
    return b.data + b.top + offset;
}
static int init_binary(Binary *b, const void *blob, size_t size, const char magic[3])
{
    if (!blob || size < 16 || size > 64u * 1024 * 1024 ||
        memcmp(blob, magic, 3)) return 0;
    b->data = blob;
    b->size = size;
    b->top = half(b->data + 4);
    return b->top >= 16 && b->top <= size;
}
static uint32_t flags(Binary b)
{
    uint32_t v = b.data[3];
    return v < 128 ? v : v | UINT32_C(0xffffff00);
}

void rx_model_destroy(RxModel *model)
{
    if (!model) return;
    free(model->nodes);
    free(model->chunks);
    free(model->textures);
    *model = (RxModel){0};
}

void rx_motion_destroy(RxMotion *motion)
{
    if (!motion) return;
    free(motion->tracks);
    *motion = (RxMotion){0};
}

const char *rx_decode_model(const void *blob, size_t bytes, RxModel *out)
{
    Binary b;
    if (!out || !init_binary(&b, blob, bytes, "MDL")) return "invalid MDL header";
    RxModel model = {.source = b.data, .source_bytes = b.size};
    uint32_t *chunk_offsets = NULL;
    unsigned char *parents = NULL;
    const char *error = NULL;
    uint32_t node_offset = word(b.data + 12);
    if (node_offset != UINT32_MAX) {
        model.flags = flags(b);
        model.node_count = half(b.data + 6);
        if (!(model.flags & 0x80)) return "basic Ninja models are not supported";
        if (!model.node_count || model.node_count > 4096) return "invalid node count";
        const uint8_t *nodes = relative(b, node_offset, (size_t)model.node_count * 52);
        if (!nodes) return "node table outside MDL";
        model.nodes = calloc(model.node_count, sizeof(*model.nodes));
        model.chunks = calloc(model.node_count, sizeof(*model.chunks));
        chunk_offsets = calloc(model.node_count, sizeof(*chunk_offsets));
        parents = calloc(model.node_count, 1);
        if (!model.nodes || !model.chunks || !chunk_offsets || !parents) {
            error = "cannot allocate model work"; goto done;
        }
        for (uint32_t i = 0; i < model.node_count; ++i) {
            const uint8_t *p = nodes + (size_t)i * 52;
            RxNode *node = &model.nodes[i];
            node->flags = word(p);
            for (unsigned axis = 0; axis < 3; ++axis) {
                node->position[axis] = real(p + 8 + axis * 4);
                node->angle[axis] = signed_word(word(p + 20 + axis * 4));
                node->scale[axis] = real(p + 32 + axis * 4);
                if (!isfinite(node->position[axis]) || !isfinite(node->scale[axis])) {
                    error = "nonfinite node transform"; goto done;
                }
            }
            RxNode **links[] = {&node->child, &node->sibling};
            for (unsigned k = 0; k < 2; ++k) {
                uint32_t link = word(p + 44 + k * 4);
                if (link == UINT32_MAX) continue;
                if (link < node_offset || (link - node_offset) % 52) {
                    error = "misaligned node link"; goto done;
                }
                uint32_t index = (link - node_offset) / 52;
                if (index <= i || index >= model.node_count || parents[index]++) {
                    error = "invalid ordered node hierarchy"; goto done;
                }
                *links[k] = &model.nodes[index];
            }
            uint32_t offset = word(p + 4);
            if (offset == UINT32_MAX) continue;
            uint32_t slot = 0;
            while (slot < model.chunk_count && chunk_offsets[slot] != offset) ++slot;
            node->model = &model.chunks[slot];
            if (slot < model.chunk_count) continue;
            chunk_offsets[model.chunk_count++] = offset;
            const uint8_t *raw = relative(b, offset, 24);
            if (!raw) { error = "chunk model outside MDL"; goto done; }
            RxChunk *chunk = node->model;
            float radius = real(raw + 20);
            if (!isfinite(radius)) { error = "nonfinite chunk radius"; goto done; }
            /* In the original loader, a negative radius marks raw offsets.
               A nonnegative radius with references is an already-realized
               record, whose process-local pointers are not valid disc data. */
            chunk->radius = fabsf(radius);
            for (unsigned k = 0; k < 3; ++k) {
                chunk->center[k] = real(raw + 8 + k * 4);
                if (!isfinite(chunk->center[k])) { error = "nonfinite chunk center"; goto done; }
            }
            uint32_t v = word(raw), p_off = word(raw + 4);
            if (v != UINT32_MAX) chunk->vertices = relative(b, v, 4);
            if (p_off != UINT32_MAX) chunk->polygons = relative(b, p_off, 2);
            if ((v != UINT32_MAX && (!chunk->vertices || radius >= 0)) ||
                (p_off != UINT32_MAX && (!chunk->polygons || radius >= 0))) {
                error = "invalid or already-relocated chunk references"; goto done;
            }
            if (chunk->vertices) chunk->vertex_extent = b.size - (size_t)(chunk->vertices - b.data);
        }
        for (uint32_t i = 1; i < model.node_count; ++i) {
            if (!parents[i]) { error = "unreachable model node"; goto done; }
        }
    }
    uint32_t texture_offset = word(b.data + 8);
    if (texture_offset != UINT32_MAX) {
        const uint8_t *list = relative(b, texture_offset, 8);
        if (!list || word(list + 4) > 65536) {
            error = "invalid texture list"; goto done;
        }
        model.has_texture_list = 1;
        model.texture_count = word(list + 4);
        const uint8_t *names = relative(b, word(list), (size_t)model.texture_count * 12);
        if (!names) { error = "texture table outside MDL"; goto done; }
        if (model.texture_count) {
            model.textures = calloc(model.texture_count, sizeof(*model.textures));
            if (!model.textures) { error = "cannot allocate textures"; goto done; }
        }
        for (uint32_t i = 0; i < model.texture_count; ++i) {
            const uint8_t *entry = names + i * 12;
            const uint8_t *name = relative(b, word(entry), 1);
            size_t available = name ? b.size - (size_t)(name - b.data) : 0;
            if (!name || !memchr(name, 0, available < 1024 ? available : 1024)) {
                error = "invalid texture filename"; goto done;
            }
            model.textures[i] = (RxTexture){(const char *)name, word(entry + 4), word(entry + 8)};
        }
    }
done:
    free(chunk_offsets);
    free(parents);
    if (error) rx_model_destroy(&model);
    else { rx_model_destroy(out); *out = model; }
    return error;
}

const char *rx_decode_motion(const void *blob, size_t bytes, RxMotion *out)
{
    Binary b;
    if (!out || !init_binary(&b, blob, bytes, "MTN")) return "invalid MTN header";
    const uint8_t *raw = relative(b, word(b.data + 8), 12);
    if (!raw) return "motion header outside MTN";
    if (half(raw + 8) != 3 || half(raw + 10) != 2) return "unsupported motion track format";
    RxMotion motion = {.source = b.data, .flags = flags(b), .node_count = half(b.data + 6),
                       .frame_count = word(raw + 4)};
    if (!motion.node_count || motion.node_count > 4096 ||
        !motion.frame_count || motion.frame_count > 65536) return "invalid motion counts";
    const uint8_t *table = relative(b, word(raw), (size_t)motion.node_count * 8);
    if (!table) return "motion track table outside MTN";
    uint32_t attributes = word(b.data + 12);
    if (attributes != UINT32_MAX) {
        motion.attributes = relative(b, attributes, (size_t)motion.frame_count * 2);
        if (!motion.attributes) return "motion attributes outside MTN";
    }
    motion.tracks = calloc(motion.node_count, sizeof(*motion.tracks));
    if (!motion.tracks) return "cannot allocate motion work";
    for (uint32_t i = 0; i < motion.node_count; ++i) {
        uint32_t pos = word(table + i * 8), angle = word(table + i * 8 + 4);
        if (pos != UINT32_MAX)
            motion.tracks[i].position = relative(b, pos, (size_t)motion.frame_count * 12);
        if (angle != UINT32_MAX)
            motion.tracks[i].angle = relative(b, angle, (size_t)motion.frame_count * 6);
        if ((pos != UINT32_MAX && !motion.tracks[i].position) ||
            (angle != UINT32_MAX && !motion.tracks[i].angle)) {
            rx_motion_destroy(&motion);
            return "motion keys outside MTN";
        }
    }
    rx_motion_destroy(out);
    *out = motion;
    return NULL;
}

const char *rx_motion_sample(const RxMotion *motion, uint32_t node,
                             uint32_t frame, RxSample *out)
{
    if (!motion || !motion->tracks || !out || node >= motion->node_count ||
        frame >= motion->frame_count) return "motion sample index out of range";
    RxSample sample = {0};
    const RxTracks *track = &motion->tracks[node];
    if (track->position) {
        sample.mask |= 1;
        for (unsigned k = 0; k < 3; ++k) {
            sample.position[k] = real(track->position + frame * 12 + k * 4);
            if (!isfinite(sample.position[k])) return "nonfinite position key";
        }
    }
    if (track->angle) {
        sample.mask |= 2;
        for (unsigned k = 0; k < 3; ++k) {
            uint32_t v = half(track->angle + frame * 6 + k * 2);
            sample.angle[k] = v < 32768 ? (int32_t)v : (int32_t)v - 65536;
        }
    }
    *out = sample;
    return NULL;
}
