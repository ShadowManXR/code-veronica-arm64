#pragma once
#include "effect_callbacks.h"
typedef struct {unsigned visible;RxScreenPolygon polygon;} RxFadeFrame;
const char *rx_fade_check(void);
const char *rx_fade_tick(void);
void rx_fade_clear(void);
const RxFadeFrame *rx_fade_frame(void);
int rx_fade_collecting(void);
int rx_fade_capture(uint64_t,const void*,unsigned);
