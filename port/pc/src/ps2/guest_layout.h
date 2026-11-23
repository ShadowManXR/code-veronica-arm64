/* Fixed-width counterparts of ninjacnk.h's original 32-bit asset records.
 * Use these for disc/EE data; do not cast them to native NJS_* structs on ARM64. */
#pragma once
#include "guest_memory.h"
typedef struct {
    Ps2Address vlist, plist;
    float center[3], radius;
} Ps2ChunkModel32;
typedef struct {
    uint32_t evalflags;
    Ps2Address model;
    float position[3];
    int32_t angle[3];
    float scale[3];
    Ps2Address child, sibling;
} Ps2ChunkObject32;
typedef struct { uint32_t flags, objects; Ps2Address data, nodes, textures, work; } Ps2ModelWork32;
typedef struct { uint32_t flags, objects, frames; Ps2Address data, tracks, attributes; } Ps2MotionWork32;
typedef struct { Ps2Address data; uint32_t frames; uint16_t type, interpolation; } Ps2Motion32;
typedef struct { Ps2Address tracks[2]; } Ps2MotionTracks32;
typedef struct { float position[3]; int32_t angle[3]; uint32_t mask; } Ps2MotionSample;
typedef struct { Ps2Address names; uint32_t count; } Ps2TextureList32;
typedef struct { Ps2Address filename; uint32_t attributes, texture; } Ps2TextureName32;
typedef struct {
    uint8_t type, attributes;
    uint16_t size, index_offset, count;
    uint8_t padding[56];
} Ps2SkinVertexHeader32;
typedef struct { uint32_t nodes, groups, vertices; } Ps2SkinStats;
typedef struct {
    float version;
    uint32_t flags, data[2], offsets[20];
    char username[32];
} Ps2Rdt32;
typedef struct {
    uint8_t flags, type, id;
    int8_t floor;
    uint32_t attributes;
    float position[3], extent[3];
    uint8_t parameters[4];
} Ps2RoomAttribute32;
typedef struct { float position[3]; int32_t yaw; } Ps2RoomPosition32;
typedef struct {
    uint32_t flags;
    uint16_t id, type;
    int8_t floor;
    uint8_t model_version, work_number;
    int8_t parameter;
    float position[3];
    int16_t angle_x, angle_z, angle_y, angle_speed;
    int8_t hidden[4];
} Ps2RoomSpawn32;
typedef struct {
    uint16_t flags;
    uint8_t light_clip;
    int8_t speed;
    float position[3], length, width, height, depth, y[4], aim_speed;
    int32_t angle[3], look_angle[2];
    int16_t look_z[4];
    int8_t angle_speed, filter, filter_rate, reserved;
    int32_t perspective;
    uint32_t hidden_objects[16], hidden_lights[8], fog_color;
    float fog_near, fog_far;
} Ps2RoomCamera32;
typedef struct {
    uint32_t attributes;
    int8_t floor, reserved[2], attribute_type;
    float height, dummy, min_x, min_z, max_x, max_z;
} Ps2RoomCutArea32;
typedef struct {
    uint8_t flags, type;
    int8_t floor;
    uint8_t area_count;
    Ps2Address areas;
    float center[3], extent[3];
    Ps2RoomCamera32 cameras[2];
    uint8_t extra[256];
} Ps2RoomCut32;
typedef struct {
    Ps2Address pointers[16], dumps[16];
    int32_t counts[14], dummy[18];
    uint32_t flags, background_color, fog_color;
    float fog_near, fog_far, extent[3], ground[32];
    uint32_t model[6];
    float fog[128];
    int8_t ambient_modes[4];
    float ambient_r[4], ambient_g[4], ambient_b[4];
} Ps2RoomHeader32;
#if defined(__cplusplus)
#define PS2_LAYOUT_ASSERT static_assert
#else
#define PS2_LAYOUT_ASSERT _Static_assert
#endif
PS2_LAYOUT_ASSERT(sizeof(Ps2Address) == 4, "EE address slots must remain 32-bit");
PS2_LAYOUT_ASSERT(sizeof(Ps2ChunkModel32) == 24, "NJS_CNK_MODEL disc layout changed");
PS2_LAYOUT_ASSERT(offsetof(Ps2ChunkModel32, center) == 8, "Model center offset changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2ChunkObject32) == 52, "NJS_CNK_OBJECT disc layout changed");
PS2_LAYOUT_ASSERT(offsetof(Ps2ChunkObject32, child) == 44, "Object child offset changed");
PS2_LAYOUT_ASSERT(sizeof(float) == 4, "Game floats must remain 32-bit");
PS2_LAYOUT_ASSERT(sizeof(Ps2ModelWork32) == 24, "ML_WORK layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2MotionWork32) == 24, "MN_WORK layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2Motion32) == 12, "NJS_MOTION layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2MotionTracks32) == 8, "NJS_MDATA2_MOD layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2TextureList32) == 8 && sizeof(Ps2TextureName32) == 12, "Texture layouts changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2SkinVertexHeader32) == 64, "HDR_PS layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2Rdt32) == 128, "RDT_WORK layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2RoomAttribute32) == 36, "ATR_WORK layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2RoomPosition32) == 16, "POS layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2RoomSpawn32) == 36 && offsetof(Ps2RoomSpawn32,angle_y)==28, "ETTY_WORK layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2RoomCamera32) == 196, "CAM_WRK layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2RoomCutArea32) == 32, "CUT_WRK layout changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2RoomCut32) == 680, "CUT_WORK layout changed");
PS2_LAYOUT_ASSERT(offsetof(Ps2RoomCut32, cameras) == 32, "Cut camera offset changed");
PS2_LAYOUT_ASSERT(sizeof(Ps2RoomHeader32) == 1004, "ROM_WORK layout changed");
PS2_LAYOUT_ASSERT(offsetof(Ps2RoomHeader32, counts) == 128, "Room counts offset changed");
PS2_LAYOUT_ASSERT(offsetof(Ps2RoomHeader32, model) == 416, "Room model offset changed");
#undef PS2_LAYOUT_ASSERT
