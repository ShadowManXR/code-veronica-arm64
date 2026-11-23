#include "ninja_binary.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RECVX_ORIGINAL_LOADER_ORACLE
int rx_compare_original(const uint8_t *, size_t, const RxModel *,
                        const uint8_t *, size_t, const RxMotion *);
#endif

static void check(int ok, const char *reason)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", reason); exit(1); }
}

static uint8_t *read_blob(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    check(file != NULL, "fixture missing");
    check(!fseek(file, 0, SEEK_END), "fixture seek failed");
    long end = ftell(file);
    check(end >= 16 && end <= 64 * 1024 * 1024, "fixture size out of range");
    check(!fseek(file, 0, SEEK_SET), "fixture rewind failed");
    *size = (size_t)end;
    uint8_t *data = malloc(*size);
    check(data && fread(data, 1, *size, file) == *size, "fixture read failed");
    fclose(file);
    return data;
}

static uint32_t bits(float x)
{
    uint32_t u;
    memcpy(&u, &x, sizeof(u));
    return u;
}

static uint32_t word(const uint8_t *p)
{
    uint32_t u = 0;
    for (unsigned i = 0; i < 4; ++i) u |= (uint32_t)p[i] << (8 * i);
    return u;
}

static void put_word(uint8_t *p, uint32_t u)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(u >> (8 * i));
}

static size_t top(const uint8_t *blob) { return blob[4] | (size_t)blob[5] << 8; }
static int32_t offset(const void *ptr, const uint8_t *base)
{
    return ptr ? (int32_t)((const uint8_t *)ptr - base) : -1;
}

static void compare(FILE *reference, const char *format, ...)
{
    char actual[2304], expected[2304];
    va_list args;
    va_start(args, format);
    int chars = vsnprintf(actual, sizeof(actual), format, args);
    va_end(args);
    check(chars >= 0 && (size_t)chars < sizeof(actual), "record overflow");
    check(fgets(expected, sizeof(expected), reference) != NULL, "reference truncated");
    expected[strcspn(expected, "\r\n")] = 0;
    if (strcmp(actual, expected)) {
        fprintf(stderr, "Expected: %s\nActual:   %s\n", expected, actual);
        check(0, "decoded record differs from reference");
    }
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s MODEL MOTION EXPECTED\n", argv[0]);
        return 2;
    }
    FILE *reference = fopen(argv[3], "rb");
    check(reference != NULL, "reference missing");
    size_t model_size, motion_size;
    uint8_t *mdl = read_blob(argv[1], &model_size);
    uint8_t *mtn = read_blob(argv[2], &motion_size);
    uint8_t *bad = malloc(model_size > motion_size ? model_size : motion_size);
    check(bad != NULL, "malformed fixture allocation failed");
    RxModel model = {0};
    RxMotion motion = {0};
    memcpy(bad, mdl, model_size);
    const char *error = rx_decode_model(mdl, model_size, &model);
    check(!error, error);
    check(!memcmp(bad, mdl, model_size), "MDL was modified");
    memcpy(bad, mtn, motion_size);
    error = rx_decode_motion(mtn, motion_size, &motion);
    check(!error, error);
    check(!memcmp(bad, mtn, motion_size), "MTN was modified");

#ifdef RECVX_ORIGINAL_LOADER_ORACLE
    check(rx_compare_original(mdl, model_size, &model, mtn, motion_size, &motion),
          "output differs from original 32-bit binfunc.c");
    puts("PASS native decoded work matches original 32-bit binfunc.c");
#endif

    const uint8_t *md_base = mdl + top(mdl), *mt_base = mtn + top(mtn);
    compare(reference, "MDL %" PRIu32 " %" PRIu32 " %" PRIu32,
            model.flags, model.node_count, model.texture_count);
    for (uint32_t i = 0; i < model.node_count; ++i) {
        const RxNode *n = &model.nodes[i];
        compare(reference, "NODE %u %u %d %d %d %u %u %u %u %u %u %u %u %u",
                i, n->flags, n->model ? (int)(n->model - model.chunks) : -1,
                n->child ? (int)(n->child - model.nodes) : -1,
                n->sibling ? (int)(n->sibling - model.nodes) : -1,
                bits(n->position[0]), bits(n->position[1]), bits(n->position[2]),
                (uint32_t)n->angle[0], (uint32_t)n->angle[1], (uint32_t)n->angle[2],
                bits(n->scale[0]), bits(n->scale[1]), bits(n->scale[2]));
    }
    for (uint32_t i = 0; i < model.chunk_count; ++i) {
        const RxChunk *c = &model.chunks[i];
        compare(reference, "CHUNK %u %d %d %u %u %u %u", i,
                offset(c->vertices, md_base), offset(c->polygons, md_base),
                bits(c->center[0]), bits(c->center[1]), bits(c->center[2]), bits(c->radius));
    }
    for (uint32_t i = 0; i < model.texture_count; ++i) {
        const RxTexture *t = &model.textures[i];
        const uint8_t *name = (const uint8_t *)t->filename;
        char label[2049];
        size_t j = 0;
        for (; name[j]; ++j) {
            label[j * 2] = "0123456789abcdef"[name[j] >> 4];
            label[j * 2 + 1] = "0123456789abcdef"[name[j] & 15];
        }
        label[j * 2] = 0;
        compare(reference, "TEX %u %d %u %u %s", i, offset(name, md_base),
                t->attributes, t->texture, label);
    }
    compare(reference, "MTN %u %u %u %d", motion.flags, motion.node_count,
            motion.frame_count, offset(motion.attributes, mt_base));
    for (uint32_t i = 0; i < motion.node_count; ++i) {
        compare(reference, "TRACK %u %d %d", i, offset(motion.tracks[i].position, mt_base),
                offset(motion.tracks[i].angle, mt_base));
        uint32_t frames[] = {0, motion.frame_count / 2, motion.frame_count - 1};
        for (unsigned k = 0; k < 3; ++k) {
            RxSample s;
            error = rx_motion_sample(&motion, i, frames[k], &s);
            check(!error, error);
            compare(reference, "KEY %u %u %u %u %u %u %d %d %d",
                    i, frames[k], s.mask, bits(s.position[0]), bits(s.position[1]),
                    bits(s.position[2]), s.angle[0], s.angle[1], s.angle[2]);
        }
    }
    check(fgetc(reference) == EOF && !ferror(reference), "unexpected trailing reference records");
    fclose(reference);

    RxNode *saved_nodes = model.nodes;
    RxTracks *saved_tracks = motion.tracks;
    memcpy(bad, mdl, model_size);
    uint32_t root = word(mdl + 12);
    size_t first = top(mdl) + root;
    put_word(bad + first + 52 + 4, word(mdl + first + 4));
    RxModel shared = {0};
    error = rx_decode_model(bad, model_size, &shared);
    check(!error, error);
    check(shared.nodes[0].model && shared.nodes[0].model == shared.nodes[1].model,
          "shared chunk identity lost");
    rx_model_destroy(&shared);
    memcpy(bad, mdl, model_size);
    put_word(bad + top(mdl) + root + 44, root);
    check(rx_decode_model(bad, model_size, &model) && model.nodes == saved_nodes,
          "cyclic model link accepted or output replaced");
    memcpy(bad, mdl, model_size);
    size_t list = top(mdl) + word(mdl + 8);
    put_word(bad + list, UINT32_MAX);
    check(rx_decode_model(bad, model_size, &model) && model.nodes == saved_nodes,
          "invalid texture table accepted or output replaced");
    memcpy(bad, mtn, motion_size);
    size_t header = top(mtn) + word(mtn + 8);
    size_t tracks = top(mtn) + word(mtn + header);
    put_word(bad + tracks, (uint32_t)(motion_size - top(mtn) - 1));
    check(rx_decode_motion(bad, motion_size, &motion) && motion.tracks == saved_tracks,
          "out-of-range position keys accepted or output replaced");
    check(rx_decode_model(mdl, 15, &model) && rx_decode_motion(mtn, 15, &motion),
          "truncated header accepted");
    RxSample sample = {.mask = 99};
    check(rx_motion_sample(&motion, 0, motion.frame_count, &sample) && sample.mask == 99,
          "out-of-range frame accepted or output changed");

    printf("PASS RM_0000: %u nodes, %u chunks, %u textures, %u sampled keys from %u-frame motion\n",
           model.node_count, model.chunk_count, model.texture_count, motion.node_count * 3,
           motion.frame_count);
    puts("PASS immutable source, shared chunk identity, malformed ranges, cyclic links and atomic failure");
    if ((uintptr_t)mdl > UINT32_MAX && (uintptr_t)mtn > UINT32_MAX)
        puts("PASS model and motion source allocations both lie above 4 GiB");
    rx_model_destroy(&model);
    rx_motion_destroy(&motion);
    free(mdl);
    free(mtn);
    free(bad);
    return 0;
}
