#pragma once

#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "ps2_NinjaCnk.h"

enum {
    RX_CLIP_RIGHT = 1 << 0, RX_CLIP_LEFT = 1 << 1,
    RX_CLIP_BOTTOM = 1 << 2, RX_CLIP_TOP = 1 << 3,
    RX_CLIP_FAR = 1 << 4, RX_CLIP_NEAR = 1 << 5
};

typedef struct {
    float position[3], normal[3], uv[2], incident[3];
    float modulation[3], fog;
    unsigned clip_planes;
} RxRoomVertex;

typedef struct {
    float diffuse[3], ambient[3], alpha;
    uint64_t gs_alpha;
    unsigned color_mode;
} RxRoomMaterial;

typedef struct {
    const NJS_CNK_MODEL *model;
    const NJS_MATRIX *matrix;
    const RxRoomVertex *vertices;
    size_t count;
    unsigned texture, attributes, mode;
    unsigned ot_bucket;
    uint64_t suppressed_triangles; // Bit i suppresses the triangle ending at strip vertex i.
    int texture_filter;
    int clip_flag;
    int fan;
    RxRoomMaterial material;
} RxRoomStrip;

typedef int (*RxRoomSubmit)(const RxRoomStrip *strip, void *context);

/* Uses the active Ninja model-to-camera matrix stack (+Z forward).
   The sink consumes each packet synchronously, with order-table packets
   deferred until the end. Crossings can emit view-space fans. */
const char *rx_draw_room_chunks(ML_WORK *model, size_t bytes, int multi,
                               const NJS_CNK_VIEW *view,
                               RxRoomSubmit submit, void *context);
typedef struct {const void *data;size_t bytes;} RxDrawSpan;
typedef struct {
    const NJS_CNK_MODEL *model;
    const NJS_MATRIX *matrix; /* complete model-to-view transform */
    RxDrawSpan vertices,polygons;
} RxRoomDrawPart;
/* Caller selects Ninja textures/lighting as for rx_draw_room_chunks.
   Separate owned vertex/polygon ranges. All streams are checked before any
   submission; one shared ordering table spans the complete batch. */
const char *rx_draw_chunk_parts(const RxRoomDrawPart *parts,size_t count,const NJS_TEXLIST *textures,int multi,
                                const NJS_CNK_VIEW *view,RxRoomSubmit submit,void *context);
int rx_cnk_push_matrix(void);
int rx_cnk_draw_model(const NJS_CNK_MODEL *model, unsigned mode);
void rx_cnk_diffuse_ambient(const unsigned char *payload);
RxRoomMaterial rx_cnk_room_material(unsigned attributes, unsigned blend);
void rx_cnk_light_vertices(const float *vertices, VU1_STRIP_BUF *output, unsigned count);
