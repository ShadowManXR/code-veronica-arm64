#pragma once

#include "ninja_binary.h"
#include "room_header.h"

enum { RX_ROOM_MOTION_SLOTS = 512, RX_ENEMY_MODEL_PARTS = 16, RX_FIRST_ROOM_PROP = 4 };

typedef struct {
    RxModel model;
    RxRoomSpan skin;  // Payload following the SKIN magic.
} RxRoomModelPart;

typedef struct {
    RxMotion *motions;
    size_t slots;
} RxRoomMotionBank;

typedef struct {
    RxRoomModelPart *parts;
    size_t part_count;
    RxRoomSpan mask;  // Complete MASK block, as consumed by the engine.
    RxRoomMotionBank motion;
} RxRoomEnemy;

typedef struct {
    RxRoomHeader header;
    RxModel root;
    RxMotion *motions;
    size_t motion_slots;
    RxRoomEnemy *enemies;
    RxRoomModelPart *props, *items;  // Indexed by original room record, including reserved prop slots.
} RxRoomBinary;

/* Prepare room and actor geometry/motions from one expanded RDT. The caller
   retains the immutable RDT until this work is destroyed. Empty motion slots
   remain zeroed. Initialize output to {0}; failure leaves it unchanged. */
const char *rx_decode_room_binary(const void *rdt, size_t bytes, RxRoomBinary *out);
void rx_room_binary_destroy(RxRoomBinary *work);
