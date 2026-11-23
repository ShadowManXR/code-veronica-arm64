#include "room_header.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Only pointer-free fixed-width records are copied as bytes. */
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error The room record copy helpers require little endian
#endif

_Static_assert(sizeof(RxLightValues) == 0xd4 &&
               offsetof(RxLightValues, mode) == 0x74 &&
               offsetof(RxLightValues, work_position) == 0x88,
               "LGT_WORK scalar disk prefix changed");
_Static_assert(sizeof(RxEventCameraKeyValues) == 0x7c &&
               offsetof(RxEventCameraKeyValues, hidden_objects) == 0x18 &&
               offsetof(RxEventCameraKeyValues, link_offset) == 0x5c,
               "CAM_KEYF_WORK scalar disk prefix changed");

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int in_range(size_t limit, uint32_t offset, uint64_t bytes)
{
    return offset <= limit && bytes <= limit - offset &&
           (!bytes || offset >= sizeof(Ps2Rdt32));
}

void rx_room_header_destroy(RxRoomHeader *header)
{
    if (!header) return;
    if (header->cuts) {
        for (uint32_t i = 0; i < header->counts[0]; ++i) free(header->cuts[i].areas);
        free(header->cuts);
    }
    free(header->lights);
    free(header->event_lights);
    free(header->event_cameras);
    *header = (RxRoomHeader){0};
}

static const char *decode_lights(RxRoomSpan span, uint32_t count, RxRoomLight **out)
{
    if (!count) return NULL;
    RxRoomLight *lights = calloc(count, sizeof(*lights));
    if (!lights) return "cannot allocate native room lights";
    for (uint32_t i = 0; i < count; ++i)
        memcpy(&lights[i].values, span.data + (size_t)i * RX_LIGHT_DISK_BYTES,
               sizeof(lights[i].values));
    *out = lights;
    return NULL;
}

static const char *decode_event_cameras(RxRoomSpan span, uint32_t count,
                                       RxEventCamera **out)
{
    if (!count) return NULL;
    RxEventCamera *cameras = calloc(count, sizeof(*cameras));
    if (!cameras) return "cannot allocate native event cameras";
    *out = cameras; // owner will unwind on failure
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *p = span.data + (size_t)i * RX_EVENT_CAMERA_DISK_BYTES;
        RxEventCamera *camera = &cameras[i];
        memcpy(&camera->flags, p, 2);
        memcpy(&camera->type, p + 2, 2);
        memcpy(&camera->next, p + 4, 2);
        memcpy(&camera->key_count, p + 6, 2);
        if (camera->key_count < 0 || camera->key_count > RX_CAMERA_KEYS)
            return "invalid event-camera key count";
        // cut.c follows next - 1 when the one-based link is nonzero.
        if (camera->next < 0 || (uint32_t)camera->next > count)
            return "event-camera next index outside table";
        for (size_t j = 0; j < RX_CAMERA_KEYS; ++j) {
            const uint8_t *key = p + 8 + j * RX_CAMERA_KEY_DISK_BYTES;
            memcpy(&camera->keys[j].values, key, sizeof(camera->keys[j].values));
        }
    }
    return NULL;
}

const char *rx_decode_room_header(const void *rdt, size_t bytes, RxRoomHeader *out)
{
    if (!rdt || !out || bytes < sizeof(Ps2Rdt32) || bytes > 64u * 1024 * 1024)
        return "invalid RDT extent";
    const uint8_t *data = rdt;
    float version;
    memcpy(&version, data, sizeof(version));
    if (!isfinite(version) || version < 1.72f) return "unsupported RDT version";
    RxRoomHeader next = {0};
    for (size_t i = 0; i < 20; ++i) {
        uint32_t off = word(data + offsetof(Ps2Rdt32, offsets) + i * 4);
        if (off && !in_range(bytes, off, 1)) return "RDT section outside room";
        next.sections[i] = off;
    }
    const uint32_t header = next.sections[0];
    size_t rom_end = bytes;
    for (size_t i = 1; i < 20; ++i) {
        if (next.sections[i] > header && next.sections[i] < rom_end)
            rom_end = next.sections[i];
    }
    if (!in_range(rom_end, header, sizeof(Ps2RoomHeader32)))
        return "ROM header outside its section";

    Ps2RoomHeader32 disk;
    memcpy(&disk, data + header, sizeof(disk));
    for (size_t i = 0; i < 14; ++i) {
        if (disk.counts[i] < 0 || disk.counts[i] > 65536) return "invalid ROM table count";
        next.counts[i] = (uint32_t)disk.counts[i];
    }
    if (next.counts[0] > 1024) return "too many room cuts";
    static const unsigned count_for[RX_ROOM_TABLES] =
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 0, 11, 12, 13};
    static const unsigned stride[RX_ROOM_TABLES] = {
        sizeof(Ps2RoomCut32), RX_LIGHT_DISK_BYTES,
        sizeof(Ps2RoomSpawn32), sizeof(Ps2RoomSpawn32), sizeof(Ps2RoomSpawn32), 68,
        sizeof(Ps2RoomAttribute32), sizeof(Ps2RoomAttribute32), sizeof(Ps2RoomAttribute32),
        sizeof(Ps2RoomPosition32), sizeof(Ps2RoomAttribute32), 0, 0,
        RX_EVENT_CAMERA_DISK_BYTES, 4, RX_LIGHT_DISK_BYTES
    };
    for (size_t i = 0; i < RX_ROOM_TABLES; ++i) {
        // Original bhSetRoom ignores the stale authoring address of ROM.evtp.
        uint32_t off = i == RX_EVENTS ? next.sections[3] : disk.pointers[i];
        uint64_t n = next.counts[count_for[i]], size = n * stride[i];
        size_t limit = rom_end;
        if (i == RX_ROUTE_LINKS) size = n * n;
        if (i == RX_EVENTS) {
            if (!off) continue;
            limit = bytes;
            for (size_t j = 0; j < 20; ++j) {
                if (next.sections[j] > off && next.sections[j] < limit)
                    limit = next.sections[j];
            }
            size = limit - off;
        }
        if (i == RX_MESSAGES && n) size = (n + 1) * 4; // offset table, not text payload
        if (!in_range(limit, off, size) || (size && i != RX_EVENTS &&
            off < header + sizeof(Ps2RoomHeader32))) return "ROM table outside its section";
        next.tables[i] = (RxRoomSpan){size ? data + off : NULL, (size_t)size, off};
    }
    memcpy(next.dumps, disk.dumps, sizeof(next.dumps)); // not relocated by bhSetRoom
    memcpy(next.dummy, disk.dummy, sizeof(next.dummy));
    next.flags = disk.flags;
    next.background_color = disk.background_color;
    next.fog_color = disk.fog_color;
    next.fog_near = disk.fog_near;
    next.fog_far = disk.fog_far;
    memcpy(next.extent, disk.extent, sizeof(next.extent));
    memcpy(next.ground, disk.ground, sizeof(next.ground));
    next.ground[31] = 0; // original post-copy initialization
    memcpy(next.fog, disk.fog, sizeof(next.fog));
    memcpy(next.ambient_modes, disk.ambient_modes, sizeof(next.ambient_modes));
    memcpy(next.ambient_r, disk.ambient_r, sizeof(next.ambient_r));
    memcpy(next.ambient_g, disk.ambient_g, sizeof(next.ambient_g));
    memcpy(next.ambient_b, disk.ambient_b, sizeof(next.ambient_b));
    // The disc's ML_WORK words are not native pointers. The bounded model
    // decoder constructs room model work independently.
    if (next.counts[0]) {
        next.cuts = calloc(next.counts[0], sizeof(*next.cuts));
        if (!next.cuts) return "cannot allocate native cuts";
        uint32_t cut_start = next.tables[RX_CUTS].offset;
        uint32_t cut_table_end = cut_start + (uint32_t)next.tables[RX_CUTS].bytes;
        size_t cut_end = rom_end;
        for (size_t i = RX_LIGHTS; i < RX_ROOM_TABLES; ++i) {
            uint32_t offset = next.tables[i].offset;
            if (next.tables[i].bytes && offset > cut_start && offset < cut_end)
                cut_end = offset;
        }
        if (cut_table_end > cut_end) {
            rx_room_header_destroy(&next);
            return "cut table overlaps another ROM table";
        }
        for (uint32_t i = 0; i < next.counts[0]; ++i) {
            Ps2RoomCut32 raw;
            memcpy(&raw, next.tables[RX_CUTS].data + (size_t)i * sizeof(raw), sizeof(raw));
            RxRoomCut *cut = &next.cuts[i];
            cut->flags = raw.flags;
            cut->type = raw.type;
            cut->floor = raw.floor;
            cut->area_count = raw.area_count;
            memcpy(cut->center, raw.center, sizeof(cut->center));
            memcpy(cut->extent, raw.extent, sizeof(cut->extent));
            memcpy(cut->cameras, raw.cameras, sizeof(cut->cameras));
            memcpy(cut->extra, raw.extra, sizeof(cut->extra));
            if (!raw.area_count) continue;
            size_t area_bytes = (size_t)raw.area_count * sizeof(Ps2RoomCutArea32);
            if (raw.areas < cut_table_end || !in_range(cut_end, raw.areas, area_bytes)) {
                rx_room_header_destroy(&next);
                return "cut area list outside its section";
            }
            cut->areas = malloc(area_bytes);
            if (!cut->areas) {
                rx_room_header_destroy(&next);
                return "cannot allocate native cut areas";
            }
            memcpy(cut->areas, data + raw.areas, area_bytes);
        }
    }
    const char *error = decode_lights(next.tables[RX_LIGHTS], next.counts[1], &next.lights);
    if (!error)
        error = decode_lights(next.tables[RX_EVENT_LIGHTS], next.counts[13], &next.event_lights);
    if (!error)
        error = decode_event_cameras(next.tables[RX_EVENT_CAMERAS], next.counts[11], &next.event_cameras);
    if (error) {
        rx_room_header_destroy(&next);
        return error;
    }
    rx_room_header_destroy(out);
    *out = next;
    return NULL;
}
