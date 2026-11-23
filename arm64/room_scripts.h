#pragma once

#include <stddef.h>

enum { RX_SCRIPT_ENTRY, RX_SCRIPT_FRAME, RX_SCRIPT_FIRST_EVENT };

typedef struct RxRoomScripts {
    unsigned char *data;
    size_t bytes;
    size_t slots;
} RxRoomScripts;

/* The storage stays with the room until dispatch has stopped. An empty
   section is valid. Failure leaves the caller's current view untouched. */
const char *rx_room_scripts_open(unsigned char *data, size_t bytes, RxRoomScripts *out);
unsigned char *rx_room_script(const RxRoomScripts *scripts, size_t index);
