#pragma once
#include "types.h"
#include <stddef.h>
typedef struct {PARTS *parts;size_t count,end;} RxInventorySpriteGroup;
/* Entry, resources and initial state only. No StatusMain loop, actions or
   return-to-game path are admitted; a live inventory call must remain gated.
   Reset at arena creation/retirement, not as an inventory close operation. */
const RxInventorySpriteGroup *rx_inventory_status_groups(size_t *count);
const char *rx_inventory_status_begin(int *entered);
const char *rx_inventory_status_initialize(void);
const char *rx_inventory_status_check(void);
const S_WORK *rx_inventory_status_work(void);
void rx_inventory_status_reset(void);
/* Own a mutable copy of the bounded original member-145 PVP stream. */
const char *rx_inventory_status_load_resources(const void*,size_t);
/* Remaining shared case-1 tail; does not run StatusMain or item actions. */
const char *rx_inventory_status_finish_setup(void);
/* Original normal-status sort/icons/quantities/equipment only; no actions. */
const char *rx_inventory_status_build_items(void);
const char *rx_inventory_status_cursor_tick(void);
#include "message_runtime.h"
const char *rx_inventory_status_collect_name(RxMessageFrame*);
/* Original CursorMove/MainCommand only. Modes 4/8/32 are pending handoffs;
   item/file/map dispatch and gameplay return are not admitted here. */
const char *rx_inventory_status_input_tick(void);

/* Original portrait/box visibility only, not full SpriteOnOff dispatch. */
const char *rx_inventory_status_visibility(void);
