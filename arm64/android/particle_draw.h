#pragma once
#include "room_draw.h"
typedef struct RxParticleDraw RxParticleDraw;
void rx_particle_close(RxParticleDraw **draw);
const char *rx_particle_present(RxParticleDraw **draw,const NJS_CNK_VIEW *view);

const char *rx_overlay_present(RxParticleDraw **draw,const NJS_CNK_VIEW *view);

const char *rx_callback_present(RxParticleDraw **draw,const NJS_CNK_VIEW *view);
const char *rx_message_present(RxParticleDraw **draw);
#include "message_runtime.h"
const char *rx_message_frame_present(RxParticleDraw**,const RxMessageFrame*);
const char *rx_fade_present(RxParticleDraw **draw);
#include "inventory_sprite.h"
const char *rx_inventory_sprite_present(RxParticleDraw**,const RxInventorySpriteFrame*);
