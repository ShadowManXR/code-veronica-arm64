#pragma once
#include "types.h"
#include <stddef.h>
typedef struct {
    unsigned char *scalar;
    size_t bytes;
    NJS_CNK_OBJECT *objects;
    O_WORK *world;
    unsigned short kind;
} RxEnemy01PartView;
const RxEnemy01PartView *rx_enemy01_part_view(const BH_PWORK*);
NJS_CNK_OBJECT **rx_enemy01_part_objects_slot(BH_PWORK*);
O_WORK **rx_enemy01_part_world_slot(BH_PWORK*);
unsigned char *rx_enemy01_part_allocate(BH_PWORK*);
const char *rx_enemy01_part_check(const BH_PWORK*);
const char *rx_enemy01_part_initialize(BH_PWORK*);
const char *rx_enemy01_part_attach(BH_PWORK*,unsigned first,unsigned count);
int rx_enemy01_part_reserved(const BH_PWORK*);
int rx_enemy01_parts_overlap(const void*,size_t);
void rx_enemy01_parts_reset(void);
/* Internal retirement steps: link owner checks references before forgetting. */
const char *rx_enemy01_part_release_check(const BH_PWORK*);
void rx_enemy01_part_forget(BH_PWORK*);
