#pragma once

#include "ninja_binary.h"
#include "room_engine.h"

/* Convert one freshly bound native model in its mutable room backing.
   Validation completes before any flags or vertex coordinates are changed. */
const char *rx_room_skin_convert(RxEngineModelPart *part, const RxModel *source);

typedef struct {
    const int *weights;
    unsigned char *vertices;
    size_t vertex_count, owner;
} RxSkinMesh;

/* Claire's body has one skinned mesh with no skipped bones. */
const char *rx_room_skin_convert_mesh(RxEngineModelPart *part, const RxModel *source,
                                     RxSkinMesh *mesh);
