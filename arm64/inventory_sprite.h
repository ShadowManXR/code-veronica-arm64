#pragma once
#include <stddef.h>
#include <stdint.h>
enum {RX_INVENTORY_SPRITE_LIMIT=16};
typedef struct {
    unsigned width,height,bank;
    uint64_t tex0,texa,tex1,clamp;
    uint32_t *rgba;
} RxInventorySpriteImage;
typedef struct {
    float position[4][3],uv[4][2];
    uint32_t color[4][4];
    unsigned image;
} RxInventorySpritePolygon;
typedef struct {
    RxInventorySpritePolygon polygons[RX_INVENTORY_SPRITE_LIMIT];
    RxInventorySpriteImage images[RX_INVENTORY_SPRITE_LIMIT];
    unsigned count,image_count;
} RxInventorySpriteFrame;
/* Source sprite dispatch only, not SpriteOnOff/StatusMain or a usable screen.
   Initialize output to zero; rejection preserves its previous owned frame. */
const char *rx_inventory_sprite_collect(unsigned group,unsigned index,RxInventorySpriteFrame*);
void rx_inventory_sprite_destroy(RxInventorySpriteFrame*);
/* Original normal-status opening animation only; no menu/action dispatch. */
const char *rx_inventory_sprite_tick(void);
int rx_inventory_sprite_collecting(void);
int rx_inventory_sprite_capture(uint64_t,const void*,unsigned);

/* Call once after display motion, in original BGFadeIn/BGFadeOut order. */
const char *rx_inventory_sprite_fade_tick(void);
