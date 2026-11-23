#pragma once

#include "ps2/guest_layout.h"
#include "room_scene_records.h"

typedef struct {
    const uint8_t *data;
    size_t bytes;
    uint32_t offset;
} RxRoomSpan;

enum {
    RX_CUTS, RX_LIGHTS, RX_ENEMIES, RX_OBJECTS, RX_ITEMS, RX_EFFECTS,
    RX_WALLS, RX_TRIGGERS, RX_FLOORS, RX_POSITIONS, RX_ROUTES, RX_ROUTE_LINKS,
    RX_EVENTS, RX_EVENT_CAMERAS, RX_MESSAGES, RX_EVENT_LIGHTS, RX_ROOM_TABLES
};

typedef struct {
    uint8_t flags, type;
    int8_t floor;
    uint8_t area_count;
    Ps2RoomCutArea32 *areas;
    float center[3], extent[3];
    Ps2RoomCamera32 cameras[2];
    uint8_t extra[256];
} RxRoomCut;

/* Decoded records own mutable storage; raw table views borrow the expanded RDT.
   The views still use disk strides and must not be cast to native work arrays. */
typedef struct {
    uint32_t sections[20];
    RxRoomSpan tables[RX_ROOM_TABLES];
    uint32_t counts[14], dumps[16], dummy[18];
    uint32_t flags, background_color, fog_color;
    float fog_near, fog_far, extent[3], ground[32], fog[128];
    int8_t ambient_modes[4];
    float ambient_r[4], ambient_g[4], ambient_b[4];
    RxRoomCut *cuts;
    RxRoomLight *lights, *event_lights;
    RxEventCamera *event_cameras;
} RxRoomHeader;

/* Initialize out to {0}. Success replaces it; failure leaves it unchanged. */
const char *rx_decode_room_header(const void *rdt, size_t bytes, RxRoomHeader *out);
void rx_room_header_destroy(RxRoomHeader *header);
