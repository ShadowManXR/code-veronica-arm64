#pragma once
#include "room_engine.h"
typedef struct RxShadowDraw RxShadowDraw;
void rx_shadow_close(RxShadowDraw **draw);
const char *rx_shadow_present(RxEngineEffects *effects, RxShadowDraw **draw, const NJS_CNK_VIEW *view);
