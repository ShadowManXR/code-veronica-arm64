#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float position[4][3]; /* Original packed GS origin/depth values. */
    uint32_t color[4][4]; /* Original raw RGBA words, including GS alpha. */
} RxScreenPolygon;
typedef struct {
    RxScreenPolygon *polygons;
    size_t count;
} RxEffectCallbacks;

/* Execute only validated original callbacks through the bounded 2D transport.
   Initialize out to {0}; success replaces it, failure preserves it. */
const char *rx_effect_callbacks_collect(RxEffectCallbacks *out);
void rx_effect_callbacks_destroy(RxEffectCallbacks *batch);
int rx_primitive_2d(uint64_t primitive,const void *data,unsigned count);
