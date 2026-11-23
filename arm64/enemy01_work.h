#pragma once
#include "types.h"
#include <stddef.h>
#include <stdint.h>

enum { RX_ENEMY01_SCALAR_BYTES=0xb0, RX_ENEMY01_CHILDREN=9 };
/* Borrowed room-owned storage. Original pointer cells remain zero in scalar;
   native pointers must never be written into its adjacent four-byte slots. */
typedef struct {
    _Alignas(16) unsigned char scalar[RX_ENEMY01_SCALAR_BYTES];
    BH_PWORK *children[RX_ENEMY01_CHILDREN];
    ATR_WORK *attributes[2];
} RxEnemy01Work;
const char *rx_enemy01_work_bind(BH_PWORK*,RxEnemy01Work*,size_t);
const char *rx_enemy01_work_check(const BH_PWORK*);
RxEnemy01Work *rx_enemy01_work_get(const BH_PWORK*);
BH_PWORK **rx_enemy01_child_slot(BH_PWORK*,unsigned offset);
ATR_WORK **rx_enemy01_attribute_slot(BH_PWORK*,unsigned offset);
const char *rx_enemy01_work_reinitialize(BH_PWORK*);
void rx_enemy01_work_reset(void);

/* Native game-arena registration survives room changes; release before free. */
const char *rx_enemy_arena_bind(void *base,size_t bytes);
void rx_enemy_arena_release(void *base);
unsigned char *rx_enemy_arena_preview(int bytes);
unsigned char *rx_enemy01_work_allocate(BH_PWORK*);
/* Trusted room loader borrows an initialized parent model/motion view. */
const char *rx_enemy01_model_bind(BH_PWORK*);
const char *rx_enemy01_link_preflight(BH_PWORK*,int joint,short id);
void rx_enemy01_link_record(BH_PWORK *parent,BH_PWORK *child,int joint,short id);
const char *rx_enemy01_link_check(const BH_PWORK*);
void rx_enemy01_links_reset(void);

int rx_enemy01_work_reserved(const BH_PWORK*);

int rx_enemy01_work_overlaps(const void*,size_t);
const ML_WORK *rx_enemy01_link_model(const BH_PWORK*);
const char *rx_enemy01_link_retire(BH_PWORK*);
