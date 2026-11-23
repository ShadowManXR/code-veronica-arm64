#pragma once
#include <stddef.h>
#include <stdint.h>
#define RX_MESSAGE_GLYPHS 512
typedef struct {float position[4][3],uv[4][2];uint32_t color[4][4];} RxMessagePolygon;
typedef struct {RxMessagePolygon polygons[RX_MESSAGE_GLYPHS];unsigned count;uint64_t tex0,texa;uintptr_t texture;} RxMessageFrame;
const char *rx_message_check(unsigned index);
const char *rx_message_tick(unsigned mode);
void rx_message_clear(void);
const RxMessageFrame *rx_message_frame(void);
int rx_message_collecting(void);
int rx_message_capture(uint64_t,const void*,unsigned);
/* Original bhDispItemName, independent of the live dialogue frame. Geometry
   is copied; the font atlas remains borrowed from the current game lifetime. */
const char *rx_message_collect_item_name(unsigned,float,float,RxMessageFrame*);
const char *rx_message_system_check(unsigned);
