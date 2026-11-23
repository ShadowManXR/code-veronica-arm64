#pragma once
#include <stdint.h>

enum {
    RX_LIGHT_DISK_BYTES = 224, RX_CAMERA_KEY_DISK_BYTES = 128,
    RX_CAMERA_KEYS = 16, RX_EVENT_CAMERA_DISK_BYTES = 8 + RX_CAMERA_KEYS * RX_CAMERA_KEY_DISK_BYTES
};

/* Pointer-free prefix of original LGT_WORK, including its numeric work fields. */
typedef struct {
    uint32_t flags, type;
    int32_t animation_speed, link_flags, link_record, link_joint, source;
    float position[3], link_offset[3], direction[3];
    float specular, diffuse, ambient, color[3], near_range, far_range;
    int32_t inner_angle, outer_angle, angle[3];
    uint32_t mode;
    int32_t counters[4];
    float work_position[3], work_direction[3];
    float work_specular, work_diffuse, work_ambient, work_color[3], work_near, work_far;
    int32_t work_inner_angle, work_outer_angle, work_angle[3];
} RxLightValues;

typedef struct {
    RxLightValues values;
    void *linked_work, *extension, *render_light; // runtime-owned; never read from the disc
} RxRoomLight;

/* Pointer-free prefix of original CAM_KEYF_WORK. Next/link values are engine
   indices, while the trailing disc recp word is an unused authoring pointer. */
typedef struct {
    uint16_t flags;
    int16_t frame;
    float position[3];
    int16_t angle[3], perspective;
    uint32_t hidden_objects[8], hidden_lights[4], fog_color;
    float fog_near, fog_far;
    int16_t link_flags, link_record, link_joint, next;
    float link_offset[3], parameters[5];
} RxEventCameraKeyValues;

typedef struct {
    RxEventCameraKeyValues values;
    void *record; // initialized NULL; no consumer currently uses original recp
} RxEventCameraKey;

typedef struct {
    uint16_t flags, type;
    int16_t next, key_count;
    RxEventCameraKey keys[RX_CAMERA_KEYS];
} RxEventCamera;
