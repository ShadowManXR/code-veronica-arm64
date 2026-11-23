#pragma once

#include <stddef.h>
#include <stdint.h>

/* Native work records. References into the supplied immutable blob remain
   valid for its lifetime; all pointer-bearing tables are separately owned. */
typedef struct {
    const uint8_t *vertices, *polygons;
    size_t vertex_extent; // Bytes from the vertex reference to the end of its MDL.
    float center[3], radius;
} RxChunk;

typedef struct RxNode {
    uint32_t flags;
    RxChunk *model;
    float position[3], scale[3];
    int32_t angle[3];
    struct RxNode *child, *sibling;
} RxNode;

typedef struct {
    const char *filename;
    uint32_t attributes, texture;
} RxTexture;

typedef struct {
    uint32_t flags, node_count, chunk_count, texture_count;
    const uint8_t *source;
    size_t source_bytes;
    int has_texture_list;
    RxNode *nodes;
    RxChunk *chunks;
    RxTexture *textures;
} RxModel;

typedef struct {
    const uint8_t *position, *angle;
} RxTracks;

typedef struct {
    uint32_t flags, node_count, frame_count;
    const uint8_t *source;
    const uint8_t *attributes;
    RxTracks *tracks;
} RxMotion;

typedef struct {
    uint32_t mask;
    float position[3];
    int32_t angle[3];
} RxSample;

/* Pass an initialized output (e.g. {0}). NULL means success; on failure the
   returned static error describes the rejection and output remains unchanged.
   Destroy the work record before releasing the source bytes. */
const char *rx_decode_model(const void *blob, size_t bytes, RxModel *out);
const char *rx_decode_motion(const void *blob, size_t bytes, RxMotion *out);
const char *rx_motion_sample(const RxMotion *motion, uint32_t node,
                             uint32_t frame, RxSample *out);
void rx_model_destroy(RxModel *model);
void rx_motion_destroy(RxMotion *motion);
