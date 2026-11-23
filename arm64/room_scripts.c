#include "room_scripts.h"

#include <stdint.h>

static uint32_t offset_at(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

const char *rx_room_scripts_open(unsigned char *data, size_t bytes, RxRoomScripts *out)
{
    if (!bytes) {
        *out = (RxRoomScripts){0};
        return NULL;
    }
    if (!data || bytes <= 2 * sizeof(uint32_t)) return "truncated room script table";

    // The first bytecode offset bounds the table. Later entries can point to
    // earlier bytecode, and sparse event entries use zero.
    size_t table_end = offset_at(data);
    if (table_end < 2 * sizeof(uint32_t) || table_end >= bytes)
        return "invalid room entry script offset";
    for (size_t i = 0; i + sizeof(uint32_t) <= table_end; i += sizeof(uint32_t)) {
        uint32_t offset = offset_at(data + i);
        if (offset >= bytes || (offset && offset < i + sizeof(uint32_t)))
            return "room script offset outside bytecode";
        if (offset && offset < table_end) table_end = offset;
    }
    if (table_end % sizeof(uint32_t)) return "misaligned room script table end";
    *out = (RxRoomScripts){data, bytes, table_end / sizeof(uint32_t)};
    return NULL;
}

unsigned char *rx_room_script(const RxRoomScripts *scripts, size_t index)
{
    if (!scripts || index >= scripts->slots) return NULL;
    size_t offset = offset_at(scripts->data + index * sizeof(uint32_t));
    if (offset < scripts->slots * sizeof(uint32_t) || offset >= scripts->bytes) return NULL;
    return scripts->data + offset;
}
