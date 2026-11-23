#include "room_binary.h"

#include <stdlib.h>
#include <string.h>

enum { MODEL_LIMIT = 4096, SKIN_MAGIC = 0x4e494b53, MASK_MAGIC = 0x4b53414d };

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int inside(size_t bytes, size_t offset, size_t width)
{
    return offset <= bytes && width <= bytes - offset;
}

static size_t next_boundary(size_t limit, size_t start, uint32_t candidate)
{
    return candidate > start && candidate < limit ? candidate : limit;
}

static void destroy_bank(RxMotion *motions, size_t slots)
{
    for (size_t i = 0; i < slots; ++i) rx_motion_destroy(&motions[i]);
    free(motions);
}

void rx_room_binary_destroy(RxRoomBinary *work)
{
    if (!work) return;
    if (work->enemies) {
        for (size_t i = 0; i < work->header.counts[2]; ++i) {
            RxRoomEnemy *enemy = &work->enemies[i];
            for (size_t j = 0; j < enemy->part_count; ++j) rx_model_destroy(&enemy->parts[j].model);
            free(enemy->parts);
            destroy_bank(enemy->motion.motions, enemy->motion.slots);
        }
    }
    if (work->props)
        for (size_t i = RX_FIRST_ROOM_PROP; i < work->header.counts[3]; ++i)
            rx_model_destroy(&work->props[i].model);
    if (work->items)
        for (size_t i = 0; i < work->header.counts[4]; ++i)
            rx_model_destroy(&work->items[i].model);
    free(work->enemies);
    free(work->props);
    free(work->items);
    rx_room_header_destroy(&work->header);
    rx_model_destroy(&work->root);
    destroy_bank(work->motions, work->motion_slots);
    *work = (RxRoomBinary){0};
}

/* Entries can be sparse, and each package ends before the next greater
   package offset even when the directory itself is out of order. */
static RxRoomSpan package(const uint8_t *data, size_t table, size_t count,
                          size_t index, size_t end)
{
    uint32_t start = word(data + table + index * 4);
    if (!start) return (RxRoomSpan){0};
    for (size_t i = 0; i < count; ++i)
        end = next_boundary(end, start, word(data + table + i * 4));
    return (RxRoomSpan){data + start, end - start, start};
}

static const char *read_block(RxRoomSpan *bank, RxRoomSpan *block, int *done)
{
    if (bank->bytes < 4) return "unterminated room asset bank";
    uint32_t length = word(bank->data);
    *done = length == UINT32_MAX;
    if (*done) return NULL;
    if (length > bank->bytes - 4) return "asset block exceeds room bank";
    *block = (RxRoomSpan){bank->data + 4, length, bank->offset + 4};
    bank->data += 4 + length;
    bank->bytes -= 4 + length;
    bank->offset += 4 + length;
    return NULL;
}

static const char *decode_bank(RxRoomSpan bank, RxMotion **motions, size_t *slots)
{
    if (!bank.data) return NULL;
    *motions = calloc(RX_ROOM_MOTION_SLOTS, sizeof(**motions));
    if (!*motions) return "cannot allocate room motion bank";
    for (;;) {
        RxRoomSpan block;
        int done;
        const char *error = read_block(&bank, &block, &done);
        if (error || done) return error;
        if (*slots == RX_ROOM_MOTION_SLOTS) return "too many room motion slots";
        RxMotion *slot = &(*motions)[(*slots)++];
        if (block.bytes) {
            error = rx_decode_motion(block.data, block.bytes, slot);
            if (error) return error;
        }
    }
}

static const char *decode_enemy_models(RxRoomSpan bank, RxRoomEnemy *enemy)
{
    enemy->parts = calloc(RX_ENEMY_MODEL_PARTS, sizeof(*enemy->parts));
    if (!enemy->parts) return "cannot allocate enemy model work";
    RxRoomSpan skin = {0};
    for (;;) {
        RxRoomSpan block;
        int done;
        const char *error = read_block(&bank, &block, &done);
        if (error) return error;
        if (done) {
            if (skin.data) return "enemy skin has no following model";
            return enemy->part_count ? NULL : "empty enemy model bank";
        }
        if (!block.bytes) continue;
        if (block.bytes < 4) return "truncated enemy model bank block";
        switch (word(block.data)) {
        case MASK_MAGIC:
            if (enemy->mask.data) return "duplicate enemy mask";
            enemy->mask = block;
            break;
        case SKIN_MAGIC:
            if (skin.data || block.bytes <= 4 || block.bytes % 4)
                return "invalid enemy skin block";
            skin = (RxRoomSpan){block.data + 4, block.bytes - 4, block.offset + 4};
            break;
        default:
            if (enemy->part_count == RX_ENEMY_MODEL_PARTS) return "too many enemy model parts";
            RxRoomModelPart *part = &enemy->parts[enemy->part_count++];
            part->skin = skin;
            skin = (RxRoomSpan){0};
            error = rx_decode_model(block.data, block.bytes, &part->model);
            if (error) return error;
            break;
        }
    }
}

static const char *decode_prop(RxRoomSpan span, RxRoomModelPart *part)
{
    if (span.bytes >= 3 && !memcmp(span.data, "MDL", 3))
        return rx_decode_model(span.data, span.bytes, &part->model);
    if (span.bytes < 8 || word(span.data + 4) != SKIN_MAGIC)
        return "invalid prop model wrapper";
    uint32_t length = word(span.data);
    if (!length || length % 4 || length > span.bytes - 8)
        return "prop skin outside model package";
    part->skin = (RxRoomSpan){span.data + 8, length, span.offset + 8};
    return rx_decode_model(span.data + 8 + length, span.bytes - 8 - length, &part->model);
}

const char *rx_decode_room_binary(const void *rdt, size_t bytes, RxRoomBinary *out)
{
    if (!out) return "missing room work output";
    RxRoomBinary next = {0};
    const char *error = rx_decode_room_header(rdt, bytes, &next.header);
    if (error) return error;
    const uint8_t *data = rdt;
    const uint32_t *fields = next.header.sections;
    uint32_t models = fields[1], motions = fields[2];
    uint32_t enemies = next.header.counts[2], objects = next.header.counts[3];
    uint32_t items = next.header.counts[4];
    const uint8_t *enemy_table = next.header.tables[RX_ENEMIES].data;
    if (!models || !motions || enemies > MODEL_LIMIT) {
        error = "missing RDT binary tables or too many enemy records"; goto fail;
    }

    // bhSetRoom consumes root, owning enemies, objects from slot 4, then items.
    size_t model_count = 1 + (objects > RX_FIRST_ROOM_PROP ? objects - RX_FIRST_ROOM_PROP : 0) + items;
    for (size_t i = 0; i < enemies; ++i)
        model_count += (word(enemy_table + i * sizeof(Ps2RoomSpawn32)) & 1);
    size_t motion_count = enemies + 3; // enemy banks, room bank, mouth and lip
    size_t model_limit = bytes, motion_limit = bytes;
    for (size_t i = 0; i < sizeof(next.header.sections) / sizeof(*fields); ++i) {
        model_limit = next_boundary(model_limit, models, fields[i]);
        motion_limit = next_boundary(motion_limit, motions, fields[i]);
    }
    if (models == motions || model_count > MODEL_LIMIT ||
        !inside(model_limit, models, model_count * 4) ||
        !inside(motion_limit, motions, motion_count * 4)) {
        error = "truncated RDT binary tables"; goto fail;
    }
    for (size_t i = 0; i < model_count; ++i) {
        uint32_t p = word(data + models + i * 4);
        if (p < models + model_count * 4 || p >= model_limit) {
            error = "model package outside model section"; goto fail;
        }
    }
    for (size_t i = 0; i < motion_count; ++i) {
        uint32_t p = word(data + motions + i * 4);
        if (p && (p < motions + motion_count * 4 || p >= motion_limit)) {
            error = "motion package outside motion section"; goto fail;
        }
    }

    RxRoomSpan root = package(data, models, model_count, 0, model_limit);
    error = rx_decode_model(root.data, root.bytes, &next.root);
    if (error) goto fail;
    RxRoomSpan bank = package(data, motions, motion_count, enemies, motion_limit);
    error = decode_bank(bank, &next.motions, &next.motion_slots);
    if (error) goto fail;
    if (enemies) next.enemies = calloc(enemies, sizeof(*next.enemies));
    if (objects) next.props = calloc(objects, sizeof(*next.props));
    if (items) next.items = calloc(items, sizeof(*next.items));
    if ((enemies && !next.enemies) || (objects && !next.props) || (items && !next.items)) {
        error = "cannot allocate room actor records"; goto fail;
    }
    size_t model_index = 1;
    for (size_t i = 0; i < enemies; ++i) {
        RxRoomEnemy *enemy = &next.enemies[i];
        if (word(enemy_table + i * sizeof(Ps2RoomSpawn32)) & 1) {
            RxRoomSpan span = package(data, models, model_count, model_index++, model_limit);
            error = decode_enemy_models(span, enemy);
            if (error) goto fail;
        }
        bank = package(data, motions, motion_count, i, motion_limit);
        error = decode_bank(bank, &enemy->motion.motions, &enemy->motion.slots);
        if (error) goto fail;
    }
    for (size_t i = RX_FIRST_ROOM_PROP; i < objects; ++i) {
        RxRoomSpan span = package(data, models, model_count, model_index++, model_limit);
        error = decode_prop(span, &next.props[i]);
        if (error) goto fail;
    }
    for (size_t i = 0; i < items; ++i) {
        RxRoomSpan span = package(data, models, model_count, model_index++, model_limit);
        error = rx_decode_model(span.data, span.bytes, &next.items[i].model);
        if (error) goto fail;
    }
    rx_room_binary_destroy(out);
    *out = next;
    return NULL;
fail:
    rx_room_binary_destroy(&next);
    return error;
}
