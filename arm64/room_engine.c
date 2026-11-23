#include "room_engine.h"
#include "main.h"
#include "face_control_runtime.h"
#include "player_control_runtime.h"
#include "face_sampling.h"
#include "face_mesh.h"
#include "face_pose.h"
#include "room_binary.h"
#include "room_scripts.h"
#include "room_skin.h"
#include "MdlPut.h"
#include "Motion.h"
#include "subpl.h"
#include "eneset.h"
#include "ps2_dummy.h"
#include "ps2_NaMatrix.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(ETTY_WORK) == sizeof(Ps2RoomSpawn32) &&
               offsetof(ETTY_WORK, px) == offsetof(Ps2RoomSpawn32, position),
               "Native spawn layout differs from disk");
_Static_assert(sizeof(EF_WRK) == 68 && offsetof(EF_WRK, param) == 64,
               "Native effect layout differs from disk");
_Static_assert(sizeof(ATR_WORK) == sizeof(Ps2RoomAttribute32) &&
               offsetof(ATR_WORK, px) == offsetof(Ps2RoomAttribute32, position),
               "Native collision layout differs from disk");
_Static_assert(sizeof(POS) == sizeof(Ps2RoomPosition32), "Native position layout differs from disk");
_Static_assert(sizeof(CUT_WRK) == sizeof(Ps2RoomCutArea32), "Native cut-area layout differs from disk");
_Static_assert(sizeof(CAM_WRK) == sizeof(Ps2RoomCamera32), "Native camera layout differs from disk");
_Static_assert(offsetof(LGT_WORK, waz) + sizeof(int) == sizeof(RxLightValues),
               "Native light scalar prefix differs from disk");
_Static_assert(offsetof(CAM_KEYF_WORK, prm_4) + sizeof(float) == sizeof(RxEventCameraKeyValues),
               "Native event-camera scalar prefix differs from disk");
_Static_assert(offsetof(ROM_WORK, dmy00) - offsetof(ROM_WORK, cut_n) ==
               sizeof(((RxRoomHeader *)0)->counts) &&
               offsetof(ROM_WORK, flg) - offsetof(ROM_WORK, dmy00) ==
               sizeof(((RxRoomHeader *)0)->dummy), "Native room scalar layout differs from disk");

typedef struct {
    RxEngineModelPart part;
    NJS_CNK_OBJECT *nodes;
    NJS_CNK_MODEL *chunks;
    NJS_TEXLIST texlist;
    NJS_TEXNAME *textures;
} EngineModel;

typedef struct {
    MN_WORK *motions;
    NJS_MDATA2_MOD *tracks;
} EngineMotionBank;

typedef struct {
    EngineModel *models;
    size_t part_count;
    O_WORK *script_work; /* Borrow original arena work after model allocation. */
    unsigned char script_links[64];
    unsigned char *mask;
    size_t mask_bytes;
    int32_t *face_control;
    RxFaceMesh *face_mesh;
    RxFacePose *face_pose;
    EngineMotionBank motion;
    RxMotion *motion_source;
    size_t motion_slots;
} EngineEnemy;

struct RxEngineRoom {
    ROM_WORK rom;
    unsigned char *rdt;
    size_t bytes;
    size_t model_bytes;
    RxRoomScripts scripts;
    CUT_WORK *cuts;
    CUT_WRK **areas;
    unsigned char *area_counts;
    size_t cut_count;
    LGT_WORK *lights, *event_lights;
    EVC_WORK *cameras;
    EngineModel root, *props, *items;
    EngineMotionBank room_motion;
    RxMotion *room_motion_source;
    size_t room_motion_slots;
    EngineEnemy *enemies;
    size_t enemy_count, prop_count, item_count;
    O_WRK *sound_objects,*sound_items;
    O_WORK *sound_prop_work[32],*sound_item_work[32];
    unsigned char prop_links[32][64],item_links[32][64];
    RxEngineRoomTexture textures[256];
    size_t texture_count;
    MN_WORK *empty_motion[128];
    unsigned char *mouth, *lip;
    unsigned char *messages;
    size_t message_bytes;
    unsigned message_count;
};

typedef struct {
    NJS_POINT4 position, normal;
} SkinVertex;

struct RxEnginePlayer {
    unsigned char *package, *pictures;
    EngineModel body;
    RxModel source;
    RxMotion motion[RX_ROOM_MOTION_SLOTS];
    EngineMotionBank native_motion;
    size_t motion_slots;
    RxPlayerClipSource clips[RX_PLAYER_CLIPS];
    const unsigned char *translation[RX_PLAYER_CLIPS];
    unsigned char *translation_bank;
    size_t translation_bytes;
    unsigned char *locomotion_blocks[6];
    size_t locomotion_block_bytes[6];
    RxSkinMesh skin;
    SkinVertex *rest;
    NJS_MATRIX *partial;
    NJS_MATRIX *script_matrices;
    int world_pose;
};

enum { WEAPON_MOTION_FIRST = 100, WEAPON_MOTION_SLOTS = 100 };

struct RxEngineWeapon {
    unsigned char *package, *pictures[2];
    EngineModel models[2];
    RxModel source[2];
    RxMotion motions[WEAPON_MOTION_SLOTS];
    size_t motion_slots;
    EngineMotionBank native_motion;
};

struct RxEngineEffects {
    unsigned char *package;
    RxModel source[4];
    EngineModel model[4];
    RxShadowMesh shadow[2];
    unsigned char *pictures;
};

static void free_models(EngineModel *models, size_t count)
{
    if (!models) return;
    for (size_t i = 0; i < count; ++i) {
        free(models[i].nodes);
        free(models[i].chunks);
        free(models[i].textures);
    }
}

static void free_bank(EngineMotionBank *bank)
{
    free(bank->motions);
    free(bank->tracks);
}

void rx_engine_room_destroy(RxEngineRoom *room)
{
    if (!room) return;
    if (room->enemies) {
        for (size_t i = 0; i < room->enemy_count; ++i) {
            EngineEnemy *enemy = &room->enemies[i];
            free_models(enemy->models, enemy->part_count);
            free(enemy->models);
            free_bank(&enemy->motion);
            for(size_t j=0;j<enemy->motion_slots;++j)rx_motion_destroy(enemy->motion_source+j);
            free(enemy->motion_source);
            free(enemy->face_control);
            rx_face_pose_destroy(enemy->face_pose);
            rx_face_mesh_destroy(enemy->face_mesh);
        }
    }
    free_models(&room->root, 1);
    free_models(room->props, room->prop_count);
    free_models(room->items, room->item_count);
    free(room->enemies);
    free(room->props);
    free(room->items);
    free_bank(&room->room_motion);
    for(size_t i=0;i<room->room_motion_slots;++i)rx_motion_destroy(room->room_motion_source+i);
    free(room->room_motion_source);
    for (size_t i = 0; i < 128; ++i) free(room->empty_motion[i]);
    if (room->areas)
        for (size_t i = 0; i < room->cut_count; ++i) free(room->areas[i]);
    free(room->areas);
    free(room->area_counts);
    free(room->cuts);
    free(room->lights);
    free(room->event_lights);
    free(room->cameras);
    free(room->rdt);
    free(room);
}

ROM_WORK *rx_engine_room_rom(RxEngineRoom *room) { return &room->rom; }
MN_WORK *rx_engine_room_motions(RxEngineRoom *room) { return room->room_motion.motions; }

int32_t *rx_engine_enemy_face_control(RxEngineRoom *room,size_t index,size_t *bytes)
{
    int32_t *words=room&&index<room->enemy_count?room->enemies[index].face_control:NULL;
    if(bytes)*bytes=words?RX_FACE_CONTROL_BYTES:0;
    return words;
}

RxEngineModelPart *rx_engine_enemy_model(RxEngineRoom *room, size_t enemy, size_t part)
{
    if (enemy >= room->enemy_count || part >= room->enemies[enemy].part_count) return NULL;
    return &room->enemies[enemy].models[part].part;
}

RxEngineModelPart *rx_engine_prop_model(RxEngineRoom *room, size_t index)
{
    if (index < RX_FIRST_ROOM_PROP || index >= room->prop_count) return NULL;
    return &room->props[index].part;
}

RxEngineModelPart *rx_engine_item_model(RxEngineRoom *room, size_t index)
{
    return index < room->item_count ? &room->items[index].part : NULL;
}

unsigned char *rx_engine_enemy_mask(RxEngineRoom *room, size_t index, size_t *bytes)
{
    if (index >= room->enemy_count) {
        *bytes = 0;
        return NULL;
    }
    *bytes = room->enemies[index].mask_bytes;
    return room->enemies[index].mask;
}

MN_WORK *rx_engine_enemy_motions(RxEngineRoom *room, size_t enemy)
{
    return enemy < room->enemy_count ? room->enemies[enemy].motion.motions : NULL;
}

/* Every nonnull reference was checked by the decoder against the shadow RDT. */
static void *mutable_data(RxEngineRoom *room, const void *p)
{
    return p ? room->rdt + ((const unsigned char *)p - room->rdt) : NULL;
}

static int aligned(const void *p, size_t alignment)
{
    return (uintptr_t)p % alignment == 0;
}

static const char *bind_cuts(RxEngineRoom *room, const RxRoomHeader *header)
{
    size_t count = header->counts[0];
    if (!count) return NULL;
    room->cut_count = count;
    room->cuts = calloc(count, sizeof(*room->cuts));
    room->areas = calloc(count, sizeof(*room->areas));
    room->area_counts = calloc(count, sizeof(*room->area_counts));
    if (!room->cuts || !room->areas || !room->area_counts) return "cannot allocate engine camera cuts";
    for (size_t i = 0; i < count; ++i) {
        const RxRoomCut *src = &header->cuts[i];
        CUT_WORK *cut = &room->cuts[i];
        cut->flg = src->flags;
        cut->type = src->type;
        cut->flr_no = src->floor;
        cut->ctab_n = src->area_count;
        room->area_counts[i] = src->area_count;
        cut->cx = src->center[0];
        cut->cy = src->center[1];
        cut->cz = src->center[2];
        cut->cw = src->extent[0];
        cut->ch = src->extent[1];
        cut->cd = src->extent[2];
        memcpy(cut->cam, src->cameras, sizeof(cut->cam));
        memcpy(cut->exd, src->extra, sizeof(cut->exd));
        if (src->area_count) {
            size_t bytes = src->area_count * sizeof(*cut->cuttp);
            room->areas[i] = malloc(bytes);
            if (!room->areas[i]) return "cannot allocate engine cut areas";
            for (size_t j = 0; j < src->area_count; ++j) {
                CUT_WRK area;
                memcpy(&area, &src->areas[j], sizeof(area));
                room->areas[i][j] = area;
            }
            cut->cuttp = room->areas[i];
        }
    }
    room->rom.cutp = room->cuts;
    return NULL;
}

static const char *bind_lights(const RxRoomLight *src, size_t count, LGT_WORK **out)
{
    if (!count) return NULL;
    *out = calloc(count, sizeof(**out));
    if (!*out) return "cannot allocate engine room lights";
    for (size_t i = 0; i < count; ++i)
        memcpy(&(*out)[i], &src[i].values, sizeof(src[i].values));
    return NULL;
}

static const char *bind_cameras(RxEngineRoom *room, const RxRoomHeader *header)
{
    size_t count = header->counts[11];
    if (!count) return NULL;
    room->cameras = calloc(count, sizeof(*room->cameras));
    if (!room->cameras) return "cannot allocate engine event cameras";
    for (size_t i = 0; i < count; ++i) {
        const RxEventCamera *src = &header->event_cameras[i];
        EVC_WORK *cam = &room->cameras[i];
        cam->flg = src->flags;
        cam->type = src->type;
        cam->nxt_no = src->next;
        cam->keyf_n = src->key_count;
        for (size_t j = 0; j < RX_CAMERA_KEYS; ++j)
            memcpy(&cam->keyf[j], &src->keys[j].values, sizeof(src->keys[j].values));
    }
    room->rom.evcp = room->cameras;
    return NULL;
}

static const char *bind_header(RxEngineRoom *room, const RxRoomHeader *header)
{
    ROM_WORK *rom = &room->rom;
    memcpy((unsigned char *)rom + offsetof(ROM_WORK, cut_n), header->counts, sizeof(header->counts));
    memcpy((unsigned char *)rom + offsetof(ROM_WORK, dmy00), header->dummy, sizeof(header->dummy));
    rom->flg = header->flags;
    rom->bak_col = header->background_color;
    rom->fog_col = header->fog_color;
    rom->fog_nr = header->fog_near;
    rom->fog_fr = header->fog_far;
    rom->w = header->extent[0];
    rom->h = header->extent[1];
    rom->d = header->extent[2];
    rom->amb_rom = header->ambient_modes[0];
    rom->amb_chr = header->ambient_modes[1];
    rom->amb_obj = header->ambient_modes[2];
    rom->amb_itm = header->ambient_modes[3];
    memcpy(rom->grand, header->ground, sizeof(rom->grand));
    memcpy(rom->fog, header->fog, sizeof(rom->fog));
    memcpy(rom->amb_r, header->ambient_r, sizeof(rom->amb_r));
    memcpy(rom->amb_g, header->ambient_g, sizeof(rom->amb_g));
    memcpy(rom->amb_b, header->ambient_b, sizeof(rom->amb_b));

    /* Pointer-free tables retain their disk stride in the private mutable copy. */
#define BIND(field, index, type) \
    do { \
        rom->field = mutable_data(room, header->tables[index].data); \
        if (!aligned(rom->field, _Alignof(type))) return "unaligned ROM " #field " table"; \
    } while (0)
    BIND(enep, RX_ENEMIES, ETTY_WORK);
    BIND(objp, RX_OBJECTS, ETTY_WORK);
    BIND(itmp, RX_ITEMS, ETTY_WORK);
    BIND(effp, RX_EFFECTS, EF_WRK);
    BIND(walp, RX_WALLS, ATR_WORK);
    BIND(etcp, RX_TRIGGERS, ATR_WORK);
    BIND(flrp, RX_FLOORS, ATR_WORK);
    BIND(posp, RX_POSITIONS, POS);
    BIND(rutp, RX_ROUTES, ATR_WORK);
    BIND(ruttp, RX_ROUTE_LINKS, unsigned char);
    BIND(mesp, RX_MESSAGES, unsigned int);
    if(header->counts[12]){
        size_t start=header->tables[RX_MESSAGES].offset,end=room->bytes;
        for(unsigned i=0;i<20;++i)if(header->sections[i]>start&&header->sections[i]<end)end=header->sections[i];
        for(unsigned i=0;i<RX_ROOM_TABLES;++i)if(header->tables[i].bytes&&header->tables[i].offset>start&&header->tables[i].offset<end)end=header->tables[i].offset;
        room->messages=room->rdt+start;room->message_bytes=end-start;room->message_count=header->counts[12];
    }
#undef BIND
    RxRoomSpan events = header->tables[RX_EVENTS];
    const char *error = rx_room_scripts_open(mutable_data(room, events.data), events.bytes, &room->scripts);
    if (error) return error;
    rom->evtp = &room->scripts;
    error = bind_cuts(room, header);
    if (!error) error = bind_lights(header->lights, header->counts[1], &room->lights);
    if (!error) error = bind_lights(header->event_lights, header->counts[13], &room->event_lights);
    if (!error) error = bind_cameras(room, header);
    rom->lgtp = room->lights;
    rom->evlp = room->event_lights;
    return error;
}

/* Decoded references here point into an owned, writable asset copy. */
static const char *bind_model(const RxModel *src, EngineModel *out)
{
    if (src->node_count) out->nodes = calloc(src->node_count, sizeof(*out->nodes));
    if (src->chunk_count) out->chunks = calloc(src->chunk_count, sizeof(*out->chunks));
    if (src->texture_count) out->textures = calloc(src->texture_count, sizeof(*out->textures));
    if ((src->node_count && !out->nodes) || (src->chunk_count && !out->chunks) ||
        (src->texture_count && !out->textures)) return "cannot allocate engine model work";
    for (size_t i = 0; i < src->chunk_count; ++i) {
        const RxChunk *chunk = &src->chunks[i];
        NJS_CNK_MODEL *model = &out->chunks[i];
        model->vlist = (void *)chunk->vertices;
        model->plist = (void *)chunk->polygons;
        if (!aligned(model->vlist, _Alignof(Sint32)) || !aligned(model->plist, _Alignof(Sint16)))
            return "unaligned chunk stream";
        model->center = (NJS_POINT3){chunk->center[0], chunk->center[1], chunk->center[2]};
        model->r = chunk->radius;
    }
    for (size_t i = 0; i < src->node_count; ++i) {
        const RxNode *node = &src->nodes[i];
        NJS_CNK_OBJECT *obj = &out->nodes[i];
        obj->evalflags = node->flags;
        memcpy(obj->pos, node->position, sizeof(obj->pos));
        memcpy(obj->ang, node->angle, sizeof(obj->ang));
        memcpy(obj->scl, node->scale, sizeof(obj->scl));
        if (node->model) obj->model = &out->chunks[node->model - src->chunks];
        if (node->child) obj->child = &out->nodes[node->child - src->nodes];
        if (node->sibling) obj->sibling = &out->nodes[node->sibling - src->nodes];
    }
    for (size_t i = 0; i < src->texture_count; ++i) {
        const RxTexture *tex = &src->textures[i];
        out->textures[i] = (NJS_TEXNAME){(void *)tex->filename, tex->attributes, tex->texture};
    }
    out->texlist = (NJS_TEXLIST){out->textures, src->texture_count};
    out->part.model = (ML_WORK){
        .flg = src->flags, .obj_num = src->node_count, .datP = (void *)src->source,
        .objP = out->nodes, .texP = src->has_texture_list ? &out->texlist : NULL
    };
    out->part.authored_textures = out->part.model.texP;
    out->part.model_bytes=src->source_bytes;
    return NULL;
}

static const char *bind_motions(const RxMotion *motions,
                               size_t slots, EngineMotionBank *out)
{
    out->motions = calloc(RX_ROOM_MOTION_SLOTS, sizeof(*out->motions));
    if (!out->motions) return "cannot allocate engine motion bank";
    size_t track_count = 0;
    for (size_t i = 0; i < slots; ++i) track_count += motions[i].node_count;
    if (track_count) {
        out->tracks = calloc(track_count, sizeof(*out->tracks));
        if (!out->tracks) return "cannot allocate engine motion tracks";
    }
    NJS_MDATA2_MOD *tracks = out->tracks;
    for (size_t i = 0; i < slots; ++i) {
        const RxMotion *motion = &motions[i];
        if (!motion->source) continue;
        for (size_t j = 0; j < motion->node_count; ++j) {
            tracks[j].p[0] = (void *)motion->tracks[j].position;
            tracks[j].p[1] = (void *)motion->tracks[j].angle;
            if (!aligned(tracks[j].p[0], _Alignof(float)) || !aligned(tracks[j].p[1], _Alignof(short)))
                return "unaligned motion keys";
        }
        unsigned short *attributes = (void *)motion->attributes;
        if (!aligned(attributes, _Alignof(unsigned short))) return "unaligned motion attributes";
        out->motions[i] = (MN_WORK){
            motion->flags, motion->node_count, motion->frame_count,
            (void *)motion->source, tracks, attributes
        };
        tracks += motion->node_count;
    }
    return NULL;
}

static const char *bind_part(RxEngineRoom *room, const RxRoomModelPart *src, EngineModel *out)
{
    out->part.skin = mutable_data(room, src->skin.data);
    out->part.skin_bytes = src->skin.bytes;
    if (!aligned(out->part.skin, _Alignof(int))) return "unaligned engine skin payload";
    const char *error = bind_model(&src->model, out);
    return error ? error : rx_room_skin_convert(&out->part, &src->model);
}

static const RxChunk *face_chunk(const RxModel *model,int id)
{
    if(id<0||!model->node_count)return NULL;
    const RxNode *node=model->nodes,*stack[4096];size_t depth=0;unsigned ordinal=0,visited=0;
    while(node&&visited++<model->node_count) {
        if(node->model&&ordinal++==(unsigned)id)return node->model;
        if(node->sibling){if(depth==4096)return NULL;stack[depth++]=node->sibling;}
        if(node->child)node=node->child;
        else node=depth?stack[--depth]:NULL;
    }
    return NULL;
}
RxFaceMesh *rx_engine_enemy_face_mesh(RxEngineRoom *room,size_t index)
{return room&&index<room->enemy_count?room->enemies[index].face_mesh:NULL;}
RxFacePose *rx_engine_enemy_face_pose(RxEngineRoom *room,size_t index)
{return room&&index<room->enemy_count?room->enemies[index].face_pose:NULL;}
static const char *bind_actors(RxEngineRoom *room, const RxRoomBinary *src)
{
    room->enemy_count = src->header.counts[2];
    room->prop_count = src->header.counts[3];
    room->item_count = src->header.counts[4];
    if (room->enemy_count) room->enemies = calloc(room->enemy_count, sizeof(*room->enemies));
    if (room->prop_count) room->props = calloc(room->prop_count, sizeof(*room->props));
    if (room->item_count) room->items = calloc(room->item_count, sizeof(*room->items));
    if ((room->enemy_count && !room->enemies) || (room->prop_count && !room->props) ||
        (room->item_count && !room->items)) return "cannot allocate engine actor tables";
    for (size_t i = 0; i < room->enemy_count; ++i) {
        const RxRoomEnemy *enemy = &src->enemies[i];
        EngineEnemy *out = &room->enemies[i];
        out->mask = mutable_data(room, enemy->mask.data);
        out->mask_bytes = enemy->mask.bytes;
        /* bhInitMask obtains a cleared 1092-byte scalar allocation for IDs >90.
           Stage that storage independently; native facial contexts are pending. */
        if(room->rom.enep[i].id>90) {
            out->face_control=calloc(RX_FACE_CONTROL_WORDS,sizeof(*out->face_control));
            if(!out->face_control)return "Cannot allocate original facial control storage";
        }
        out->part_count = enemy->part_count;
        if (out->part_count) {
            out->models = calloc(out->part_count, sizeof(*out->models));
            if (!out->models) return "cannot allocate engine enemy models";
        }
        for (size_t j = 0; j < out->part_count; ++j) {
            const char *error = bind_part(room, &enemy->parts[j], &out->models[j]);
            if (error) return error;
        }
        if(out->mask) {
            if(out->mask_bytes<64||!out->part_count)return "Facial MASK lacks its actor model";
            const RxChunk *chunk=face_chunk(&enemy->parts[0].model,(signed char)out->mask[52]);
            const char *error=rx_face_mesh_create(out->mask,out->mask_bytes,chunk,&out->face_mesh);
            if(!error)error=rx_face_mesh_aux_prepare(out->face_mesh,&enemy->parts[0].model,&out->models[0].part.model);
            if(error)return error;
        }
        if (enemy->motion.motions) {
            const char *error = bind_motions(enemy->motion.motions, enemy->motion.slots, &out->motion);
            if (error) return error;
        }
    }
    for (size_t i = RX_FIRST_ROOM_PROP; i < room->prop_count; ++i) {
        const char *error = bind_part(room, &src->props[i], &room->props[i]);
        if (error) return error;
    }
    for (size_t i = 0; i < room->item_count; ++i) {
        const char *error = bind_part(room, &src->items[i], &room->items[i]);
        if (error) return error;
    }
    return NULL;
}

const char *rx_engine_room_load(const void *rdt, size_t bytes, RxEngineRoom **out)
{
    if (!out || !rdt || bytes < sizeof(Ps2Rdt32) || bytes > 64u * 1024 * 1024)
        return "invalid engine RDT extent";
    RxEngineRoom *next = calloc(1, sizeof(*next));
    if (!next) return "cannot allocate engine room";
    next->rdt = malloc(bytes);
    if (!next->rdt) {
        rx_engine_room_destroy(next);
        return "cannot allocate mutable engine RDT";
    }
    memcpy(next->rdt, rdt, bytes);
    next->bytes = bytes;
    RxRoomBinary decoded = {0};
    const char *error = rx_decode_room_binary(next->rdt, bytes, &decoded);
    if (!error) error = bind_header(next, &decoded.header);
    if (!error) error = bind_model(&decoded.root, &next->root);
    if (!error) {
        next->rom.mdl = next->root.part.model;
        next->model_bytes = decoded.root.source_bytes;
    }
    if (!error) error = bind_motions(decoded.motions, decoded.motion_slots, &next->room_motion);
    if (!error) error = bind_actors(next, &decoded);
    if(!error){
        next->room_motion_source=decoded.motions;next->room_motion_slots=decoded.motion_slots;
        decoded.motions=NULL;decoded.motion_slots=0;
        for(size_t i=0;i<next->enemy_count;++i){
            next->enemies[i].motion_source=decoded.enemies[i].motion.motions;
            next->enemies[i].motion_slots=decoded.enemies[i].motion.slots;
            decoded.enemies[i].motion=(RxRoomMotionBank){0};
        }
    }
    rx_room_binary_destroy(&decoded);
    if (error) {
        rx_engine_room_destroy(next);
        return error;
    }
    rx_engine_room_destroy(*out);
    *out = next;
    return NULL;
}

enum { ROOT_TEX, ENEMY_TEX, PROP_TEX, ITEM_TEX, EFFECT_TEX };

static uint32_t room_word(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned effect_half(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }

static int effect_range(const RxModel *model, const void *pointer, size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer, start=(uintptr_t)model->source;
    return p>=start && p-start<=model->source_bytes && bytes<=model->source_bytes-(p-start);
}

static const char *decode_shadow_mesh(const RxModel *model, const RxChunk *chunk, RxShadowMesh *out)
{
    const unsigned char *v=chunk->vertices, *p=chunk->polygons;
    if (!effect_range(model,v,8) || !effect_range(model,p,6) || v[0]!=34 || p[0]!=56)
        return "unsupported original shadow modifier format";
    unsigned base=effect_half(v+4), vertices=effect_half(v+6);
    size_t vbytes=4+(size_t)effect_half(v+2)*4;
    unsigned triangles=effect_half(p+4)&0x3fff, extra=effect_half(p+4)>>14;
    size_t pbytes=4+(size_t)effect_half(p+2)*2;
    if (!vertices || !triangles || vbytes!=8+(size_t)vertices*12 ||
        pbytes<6+(size_t)triangles*(6+extra*2) || !effect_range(model,v,vbytes+4) ||
        !effect_range(model,p,pbytes+2) || v[vbytes]!=255 || p[pbytes]!=255)
        return "truncated original shadow modifier mesh";
    float (*positions)[3]=malloc((size_t)vertices*sizeof(*positions));
    unsigned short *indices=malloc((size_t)triangles*3*sizeof(*indices));
    if (!positions || !indices) { free(positions); free(indices); return "cannot allocate shadow geometry"; }
    memcpy(positions,v+8,(size_t)vertices*sizeof(*positions));
    const char *error=NULL;
    for (unsigned i=0;i<vertices;++i)
        for (unsigned j=0;j<3;++j)
            if (!isfinite(positions[i][j])) error="nonfinite original shadow vertex";
    for (unsigned i=0;i<triangles;++i) {
        const unsigned char *triangle=p+6+(size_t)i*(6+extra*2);
        for (unsigned j=0;j<3;++j) {
            unsigned index=effect_half(triangle+j*2);
            if (index<base || index-base>=vertices) error="shadow triangle exceeds vertex work";
            indices[i*3+j]=(unsigned short)(index-base);
        }
    }
    if (error) { free(positions); free(indices); return error; }
    *out=(RxShadowMesh){positions,indices,vertices,triangles};
    return NULL;
}

void rx_engine_effects_destroy(RxEngineEffects *effects)
{
    if (!effects) return;
    for (unsigned i=0;i<2;++i) {
        free((void *)effects->shadow[i].positions);
        free((void *)effects->shadow[i].indices);
    }
    free_models(effects->model,4);
    for (unsigned i=0;i<4;++i) rx_model_destroy(&effects->source[i]);
    free(effects->package);
    free(effects);
}

static int validate_texture_block(const unsigned char *,size_t,unsigned *);

int rx_engine_validate_palette_pictures(const unsigned char *data,size_t bytes,unsigned expected)
{
    if (!validate_texture_block(data,bytes,NULL)) return 0;
    unsigned palette_entries=0,registered=0;
    const unsigned char *palette=NULL;
    while (room_word(data)!=UINT32_MAX) {
        size_t payload=room_word(data+4);const unsigned char *picture=data+32;
        if (room_word(data)==0x00494c50) {
            palette_entries=payload?room_word(picture):0;palette=picture+4;
        } else {
            unsigned format=picture[0x90],type=picture[0x93];
            if (picture[5]!=1 || (type!=4 && type!=5)) return 0;
            if (format>=5 && format<9) {
                if (!palette || !palette_entries) return 0;
                size_t colors=type==4?(size_t)palette_entries*128:(size_t)palette_entries*1024;
                if (room_word(picture+0x84)<colors) return 0;
                for (unsigned i=0;i<palette_entries;++i) {
                    uint32_t bank=room_word(palette+i*8+4);
                    unsigned destination=((bank>>8)&255)*16+(bank>>16);
                    if (((bank>>8)&255)==255 || destination+(type==4?16:256)>1024) return 0;
                    registered+=(bank&255)==0;
                }
            } else ++registered;
        }
        data+=32+payload;
    }
    return registered==expected;
}

static int validate_common_pictures(const unsigned char *data,size_t bytes)
{return rx_engine_validate_palette_pictures(data,bytes,40);}

const char *rx_engine_effects_load(const void *data, size_t bytes, RxEngineEffects **out)
{
    if (!data || bytes<16 || bytes>64u*1024*1024 || !out)
        return "invalid common effect model package";
    RxEngineEffects *effects=calloc(1,sizeof(*effects));
    if (effects) effects->package=malloc(bytes);
    if (!effects || !effects->package) {
        rx_engine_effects_destroy(effects);
        return "cannot allocate common effect model package";
    }
    memcpy(effects->package,data,bytes);
    const char *error=NULL;
    size_t offset=0;
    for (unsigned i=0;i<4 && !error;++i) {
        if (offset>bytes || bytes-offset<4) { error="truncated common effect size table"; break; }
        size_t length=room_word(effects->package+offset);
        offset+=4;
        if (length<16 || length>bytes-offset) { error="common effect model exceeds package"; break; }
        error=rx_decode_model(effects->package+offset,length,&effects->source[i]);
        if (!error) error=bind_model(&effects->source[i],&effects->model[i]);
        offset+=length;
    }
    if (!error) {
        size_t pictures=(offset+4+31)&~(size_t)31;
        if (pictures>bytes || !validate_common_pictures(effects->package+pictures,bytes-pictures))
            error="invalid original common effect texture package";
        else effects->pictures=effects->package+pictures;
    }
    if (!error && (effects->source[1].chunk_count!=2 || effects->source[1].node_count!=2))
        error="unexpected original shadow model count";
    for (unsigned i=0;!error && i<2;++i) {
        const RxNode *node=&effects->source[1].nodes[i];
        if (!node->model) error="missing original shadow model";
        else error=decode_shadow_mesh(&effects->source[1],node->model,&effects->shadow[i]);
    }
    if (error) { rx_engine_effects_destroy(effects); return error; }
    rx_engine_effects_destroy(*out);
    *out=effects;
    return NULL;
}

unsigned char *rx_engine_effect_pictures(RxEngineEffects *effects)
{
    return effects?effects->pictures:NULL;
}

ML_WORK *rx_engine_effect_model(RxEngineEffects *effects, unsigned id)
{
    static const unsigned ids[]={0,1,7,13};
    for (unsigned i=0;effects && i<4;++i)
        if (ids[i]==id) return &effects->model[i].part.model;
    return NULL;
}

const RxShadowMesh *rx_engine_shadow_mesh(RxEngineEffects *effects, unsigned version)
{
    return effects && version<2 ? &effects->shadow[version] : NULL;
}

static int validate_texture_block(const unsigned char *data, size_t bytes, unsigned *textures)
{
    unsigned count = 0;
    while (bytes >= 4) {
        uint32_t magic = room_word(data);
        if (magic == UINT32_MAX) {
            if (textures) *textures = count;
            return 1;
        }
        if (bytes < 32 || (magic != 0x324d4954 && magic != 0x00494c50)) return 0;
        size_t payload = room_word(data + 4);
        if (payload > bytes - 32 || payload % 4) return 0;
        const unsigned char *picture = data + 32;
        if (magic == 0x324d4954) {
            if (payload < 256 || room_word(picture) != magic) return 0;
            uint64_t total = (uint64_t)room_word(picture + 0x80) + 128;
            uint64_t pixels = (uint64_t)room_word(picture + 0x84) + room_word(picture + 0x88) + 256;
            if (total > payload || pixels > total || total % 4) return 0;
            ++count;
        } else if (payload && (payload < 4 || room_word(picture) > 27 ||
                               room_word(picture) > (payload - 4) / 8)) {
            return 0;
        }
        data += 32 + payload;
        bytes -= 32 + payload;
    }
    return 0;
}

void rx_engine_player_destroy(RxEnginePlayer *player)
{
    if (!player) return;
    free(player->body.part.model.owP);
    free_models(&player->body, 1);
    for (size_t i = 0; i < player->motion_slots; ++i)
        rx_motion_destroy(&player->motion[i]);
    free_bank(&player->native_motion);
    rx_model_destroy(&player->source);
    free(player->rest);
    free(player->partial);
    free(player->script_matrices);
    free(player->package);
    free(player);
}

static unsigned char *player_block(unsigned char **at, const unsigned char *end, size_t *size)
{
    if (end - *at < 4) return NULL;
    *size = room_word(*at);
    *at += 4;
    if (*size > (size_t)(end - *at)) return NULL;
    unsigned char *block = *at;
    *at += *size;
    return block;
}

void rx_engine_weapon_destroy(RxEngineWeapon *weapon)
{
    if (!weapon) return;
    for (unsigned i = 0; i < 2; ++i) {
        free(weapon->models[i].part.model.owP);
        rx_model_destroy(&weapon->source[i]);
    }
    free_models(weapon->models, 2);
    for (size_t i = 0; i < weapon->motion_slots; ++i)
        rx_motion_destroy(&weapon->motions[i]);
    free_bank(&weapon->native_motion);
    free(weapon->package);
    free(weapon);
}

static const char *load_weapon_model(RxEngineWeapon *weapon, unsigned side,
                                     unsigned char **at, const unsigned char *end)
{
    size_t model_bytes;
    unsigned char *model = player_block(at, end, &model_bytes);
    if (!model || end - *at < 4) return "truncated original weapon model package";
    const char *error = NULL;
    if (model_bytes) {
        error = rx_decode_model(model, model_bytes, &weapon->source[side]);
        if (!error) error = bind_model(&weapon->source[side], &weapon->models[side]);
        if (error) return error;
        ML_WORK *work = &weapon->models[side].part.model;
        work->owP = calloc(work->obj_num, sizeof(*work->owP));
        if (!work->owP) return "cannot allocate native weapon matrices";
    }

    // bhReadWeaponData aligns marked PVP blocks to 64 bytes from the
    // archive's aligned transfer buffer, before either hand's textures.
    size_t texture_bytes = room_word(*at);
    *at += 4;
    size_t offset = (size_t)(*at - weapon->package);
    if (texture_bytes & 0x80000000u) offset = (offset + 63) & ~(size_t)63;
    texture_bytes &= 0x7fffffffu;
    size_t bytes = (size_t)(end - weapon->package);
    if (offset > bytes || texture_bytes > bytes - offset || offset % 4)
        return "original weapon texture package exceeds its archive member";
    *at = weapon->package + offset;
    if (texture_bytes) {
        unsigned count;
        NJS_TEXLIST *list = weapon->models[side].part.model.texP;
        if (!list || !validate_texture_block(*at, texture_bytes, &count) || count != list->nbTexture)
            return "original weapon texture count does not match its model";
        weapon->pictures[side] = *at;
        weapon->models[side].part.model.flg |= 0x200;
    } else if (weapon->models[side].texlist.nbTexture) {
        return "original weapon model has no texture payload";
    }
    *at += texture_bytes;
    return NULL;
}

static const char *load_weapon_motions(RxEngineWeapon *weapon, unsigned char *data,
                                       size_t bytes, unsigned player_nodes)
{
    const unsigned char *end = data + bytes;
    if (!bytes) return NULL;
    while (end - data >= 4 && room_word(data) != UINT32_MAX) {
        size_t size;
        unsigned char *block = player_block(&data, end, &size);
        if (!block || weapon->motion_slots == WEAPON_MOTION_SLOTS)
            return "invalid original weapon motion bank";
        RxMotion *motion = &weapon->motions[weapon->motion_slots++];
        if (!size) continue;
        const char *error = rx_decode_motion(block, size, motion);
        if (error) return error;
        if (motion->node_count != player_nodes || motion->frame_count >= 32768)
            return "original weapon motion does not match the player skeleton or frame range";
    }
    if (end - data < 4) return "unterminated original weapon motion bank";
    return bind_motions(weapon->motions, weapon->motion_slots, &weapon->native_motion);
}

const char *rx_engine_weapon_load(const void *data, size_t bytes, unsigned player_nodes,
                                  RxEngineWeapon **out)
{
    if (!data || !bytes || !player_nodes || !out || *out)
        return "invalid original weapon package request";
    RxEngineWeapon *weapon = calloc(1, sizeof(*weapon));
    if (weapon) weapon->package = malloc(bytes);
    if (!weapon || !weapon->package) {
        rx_engine_weapon_destroy(weapon);
        return "cannot allocate original weapon package";
    }
    memcpy(weapon->package, data, bytes);
    unsigned char *at = weapon->package;
    const unsigned char *end = at + bytes;
    const char *error = NULL;
    for (unsigned side = 0; !error && side < 2; ++side)
        error = load_weapon_model(weapon, side, &at, end);
    if (!error) {
        size_t size;
        unsigned char *motions = player_block(&at, end, &size);
        error = motions ? load_weapon_motions(weapon, motions, size, player_nodes) :
                          "truncated original weapon motion package";
    }
    if (error) rx_engine_weapon_destroy(weapon);
    else *out = weapon;
    return error;
}

NJS_TEXLIST *rx_engine_weapon_textures(RxEngineWeapon *weapon, unsigned side, unsigned char **data)
{
    if (side >= 2) return NULL;
    if (data) *data = weapon->pictures[side];
    return weapon->models[side].part.model.texP;
}

void rx_engine_weapon_bind(RxEngineWeapon *weapon, BH_PWORK *actor, O_WRK objects[2])
{
    // Player base motions occupy 0..99; weapon motions begin at 100.
    // Room event motions from 200 onward retain their existing owners.
    memset(actor->mnwP + WEAPON_MOTION_FIRST, 0, WEAPON_MOTION_SLOTS * sizeof(MN_WORK));
    if (weapon->motion_slots)
        memcpy(actor->mnwP + WEAPON_MOTION_FIRST, weapon->native_motion.motions,
               weapon->motion_slots * sizeof(MN_WORK));
    for (unsigned side = 0; side < 2; ++side) {
        O_WRK *object = &objects[side];
        object->mdl[0] = weapon->models[side].part.model;
        object->mlwP = &object->mdl[0];
        object->mdl_n = object->mlwP->objP ? 1 : 0;
        object->mdl_no = 0;
        object->mbp[0] = object->mlwP->objP;
        object->txp[0] = object->mlwP->texP;
    }
}

const char *rx_engine_weapon_draw(RxEngineWeapon *weapon, unsigned side,
                                  const NJS_CNK_VIEW *view, RxRoomSubmit submit, void *context)
{
    if (side >= 2) return "invalid original weapon hand";
    ML_WORK *model = &weapon->models[side].part.model;
    if (!model->objP) return NULL;
    return rx_draw_room_chunks(model, weapon->source[side].source_bytes, 1, view, submit, context);
}

static const char *load_player_body(RxEnginePlayer *player, unsigned char *data,
                                    size_t bytes)
{
    const unsigned char *end = data + bytes;
    size_t skin_bytes, model_bytes;
    unsigned char *skin = player_block(&data, end, &skin_bytes);
    if (!skin || skin_bytes < 8 || skin_bytes % 4 || memcmp(skin, "SKIN", 4))
        return "missing original player skin";
    unsigned char *mdl = player_block(&data, end, &model_bytes);
    if (!mdl) return "missing original player body";
    const char *error = rx_decode_model(mdl, model_bytes, &player->source);
    if (!error) error = bind_model(&player->source, &player->body);
    if (error) return error;

    RxEngineModelPart *body = &player->body.part;
    body->skin = (int *)(skin + 4);
    body->skin_bytes = skin_bytes - 4;
    if (!body->model.texP || !body->model.texP->nbTexture)
        return "missing original player texture names";
    error = rx_room_skin_convert_mesh(body, &player->source, &player->skin);
    if (error) return error;

    body->model.owP = calloc(player->source.node_count, sizeof(O_WORK));
    player->partial = calloc(player->source.node_count, sizeof(NJS_MATRIX));
    player->script_matrices = calloc(player->source.node_count, sizeof(NJS_MATRIX));
    size_t vertex_bytes = player->skin.vertex_count * sizeof(SkinVertex);
    player->rest = malloc(vertex_bytes);
    if (!body->model.owP || !player->rest || !player->partial || !player->script_matrices)
        return "cannot allocate player pose buffers";
    memcpy(player->rest, player->skin.vertices, vertex_bytes);
    return NULL;
}

static unsigned char *player_bank_slot(unsigned char *data, size_t bytes,
                                       unsigned slot, size_t *block_bytes)
{
    const unsigned char *end = data + bytes;
    for (unsigned i = 0; i <= slot; ++i) {
        unsigned char *block = player_block(&data, end, block_bytes);
        if (!block) break;
        if (i == slot) return block;
    }
    return NULL;
}

static float player_translation(const unsigned char *data)
{
    uint32_t bits = room_word(data);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static const char *load_player_motion_bank(RxEnginePlayer *player, unsigned char *at, size_t bytes)
{
    const unsigned char *end = at + bytes;
    while (end - at >= 4 && room_word(at) != UINT32_MAX) {
        size_t length;
        unsigned char *block = player_block(&at, end, &length);
        if (!block || player->motion_slots == RX_ROOM_MOTION_SLOTS)
            return "invalid original player motion bank";
        RxMotion *motion = &player->motion[player->motion_slots++];
        if (!length) continue;
        const char *error = rx_decode_motion(block, length, motion);
        if (error) return error;
        if (motion->frame_count >= 32768)
            return "original player motion exceeds the fixed-point frame range";
    }
    if (end - at < 4 || !player->motion_slots) return "unterminated original player motion bank";
    return bind_motions(player->motion, player->motion_slots, &player->native_motion);
}

static const char *load_player_clip(RxEnginePlayer *player, unsigned clip, RxPlayerClipSource source,
                                   unsigned char *translations, size_t translation_bytes, int publish)
{
    size_t bytes;
    if(source.translation_slot< -1)return "Invalid original player translation selector";
    if (source.motion_slot >= player->motion_slots || !player->motion[source.motion_slot].source)
        return "missing original player motion";
    RxMotion *motion = &player->motion[source.motion_slot];
    if (motion->node_count != player->source.node_count)
        return "original locomotion clip does not match the player skeleton";
    if (source.translation_slot < 0) {
        if(publish){player->clips[clip]=source;player->translation[clip]=NULL;}
        return NULL;
    }

    unsigned char *block = player_bank_slot(translations, translation_bytes, (unsigned)source.translation_slot, &bytes);
    if (!block || bytes / 4 <= motion->frame_count || bytes % 4)
        return "missing original player translation frames";
    for (unsigned i = 0; i < motion->frame_count; ++i) {
        if (!isfinite(player_translation(block + ((size_t)i + 1) * 4)))
            return "nonfinite original player translation";
    }
    if(publish){player->clips[clip]=source;player->translation[clip]=block+4;}
    return NULL;
}

const char *rx_engine_player_clips(RxEnginePlayer *player,
                                  const RxPlayerClipSource clips[RX_PLAYER_CLIPS], int publish)
{
    if(!player||!clips||!player->translation_bank)return "Missing native player clip source";
    for(unsigned i=0;i<RX_PLAYER_CLIPS;++i) {
        const char *error=load_player_clip(player,i,clips[i],player->translation_bank,player->translation_bytes,0);
        if(error)return error;
    }
    if(publish)for(unsigned i=0;i<RX_PLAYER_CLIPS;++i)
        load_player_clip(player,i,clips[i],player->translation_bank,player->translation_bytes,1);
    return NULL;
}

static const char *load_player_package(RxEnginePlayer *player, size_t bytes,
                                       const RxPlayerClipSource clips[RX_PLAYER_CLIPS])
{
    unsigned char *at = player->package;
    const unsigned char *end = at + bytes;
    size_t model_bytes, motion_bytes, translation_bytes;
    unsigned char *models = player_block(&at, end, &model_bytes);
    unsigned char *motions = models ? player_block(&at, end, &motion_bytes) : NULL;
    unsigned char *translations = motions ? player_block(&at, end, &translation_bytes) : NULL;
    if (!translations || end - at < 4)
        return "truncated original player package";
    size_t pictures = room_word(at);
    at += 4;
    size_t offset = (size_t)(at - player->package);
    if (pictures & 0x80000000u) offset = (offset + 31) & ~(size_t)31;
    pictures &= 0x7fffffffu;
    unsigned texture_count;
    if (offset > bytes || pictures > bytes - offset ||
        offset % 4 || !validate_texture_block(player->package + offset, pictures, &texture_count))
        return "invalid original player texture package";
    player->pictures = player->package + offset;

    const char *error = load_player_body(player, models, model_bytes);
    if (!error && texture_count != player->body.texlist.nbTexture)
        return "original player texture count does not match its model";
    if (!error) error = load_player_motion_bank(player, motions, motion_bytes);
    player->translation_bank=translations;player->translation_bytes=translation_bytes;
    if(!error)for(unsigned i=0;i<6;++i){size_t size;
        unsigned char *block=player_bank_slot(translations,translation_bytes,i,&size);
        if(!block||size<4||size%4)return "Invalid original locomotion translation bank";
        player->locomotion_blocks[i]=block;player->locomotion_block_bytes[i]=size;
    }
    if(!error)error=rx_engine_player_clips(player,clips,1);
    return error;
}

const char *rx_engine_player_load(const void *data, size_t bytes,
                                  const RxPlayerClipSource clips[RX_PLAYER_CLIPS],
                                  RxEnginePlayer **out)
{
    if (!data || !bytes || !out || *out || !clips)
        return "invalid original player package request";
    RxEnginePlayer *player = calloc(1, sizeof(*player));
    if (player) player->package = malloc(bytes);
    if (!player || !player->package) {
        rx_engine_player_destroy(player);
        return "cannot allocate original player package";
    }
    memcpy(player->package, data, bytes);
    const char *error = load_player_package(player, bytes, clips);
    if (error) rx_engine_player_destroy(player);
    else *out = player;
    return error;
}

NJS_TEXLIST *rx_engine_player_textures(RxEnginePlayer *player, unsigned char **data)
{
    if (data) *data = player->pictures;
    return player->body.part.model.texP;
}

static NJS_POINT3 skin_lerp(NJS_POINT3 a, NJS_POINT3 b, float weight)
{
    return (NJS_POINT3){a.x + (b.x - a.x) * weight,
                        a.y + (b.y - a.y) * weight,
                        a.z + (b.z - a.z) * weight};
}

unsigned rx_engine_player_frames(const RxEnginePlayer *player, RxPlayerClip clip)
{
    return player->motion[player->clips[clip].motion_slot].frame_count;
}

float rx_engine_player_speed(const RxEnginePlayer *player, RxPlayerClip clip, unsigned frame)
{
    const unsigned char *translation = player->translation[clip];
    return translation && frame < rx_engine_player_frames(player, clip) ?
           player_translation(translation + (size_t)frame * 4) : 0.0f;
}

void rx_engine_player_bind(RxEnginePlayer *player, BH_PWORK *actor)
{
    actor->mdl[0] = player->body.part.model;
    actor->mdl_n = 1;
    actor->mlwP = &actor->mdl[0];
    actor->mbp[0] = actor->mlwP->objP;
    actor->txp[0] = actor->mlwP->texP;
    actor->skp[0] = player->body.part.skin;
    actor->mnwP = actor->mnwPb = player->native_motion.motions;
    actor->mtx=(void *)actor->mtxbuf;
    njUnitMatrix(actor->mtx);
}

const char *rx_engine_player_pose(RxEnginePlayer *player, BH_PWORK *actor,
                                  RxPlayerClip clip, int fixed_frame, int step)
{
    if (fixed_frame < 0 || (unsigned)fixed_frame / 65536 >= rx_engine_player_frames(player, clip) ||
        step < -1 || step > 1)
        return "invalid original player motion frame";
    actor->mtn_no = player->clips[clip].motion_slot;
    actor->frm_no = fixed_frame;
    actor->mtn_add = step * 65536;
    return rx_engine_player_motion(player, actor, 0);
}

static const char *player_skin(RxEnginePlayer *player,NJS_MATRIX *matrices)
{
    static const float levels[] = {.1f,.2f,.3f,.4f,.5f,.6f,.7f,.8f,.9f,1};
    ML_WORK *body=&player->body.part.model;
    for (unsigned i = 0; i < player->source.node_count; ++i) {
        const NJS_CNK_OBJECT *node = &body->objP[i];
        memcpy(player->partial[i], matrices[i], sizeof(NJS_MATRIX));
        njRotXYZ(&player->partial[i], -(node->ang[0] & 0xffff),
                 -(node->ang[1] & 0xffff), -(node->ang[2] & 0xffff));
    }

    // Apply npCalcSkin's two-matrix blend in actor-local space.
    for (size_t i = 0; i < player->skin.vertex_count; ++i) {
        uint32_t encoded = (uint32_t)player->skin.weights[i];
        unsigned joint = encoded >> 24;
        float weight = levels[(encoded >> 16) & 255];
        const SkinVertex *rest = &player->rest[i];
        NJS_POINT3 position = {rest->position.x, rest->position.y, rest->position.z};
        NJS_POINT3 normal = {rest->normal.x, rest->normal.y, rest->normal.z};
        NJS_POINT3 p, n;
        njCalcPoint(&matrices[joint], &position, &p);
        njCalcVector(&matrices[joint], &normal, &n);
        if (weight != 1.0f) {
            NJS_POINT3 base_p, base_n;
            njCalcPoint(&player->partial[joint], &position, &base_p);
            njCalcVector(&player->partial[joint], &normal, &base_n);
            p = skin_lerp(base_p, p, weight);
            n = skin_lerp(base_n, n, weight);
        }
        SkinVertex vertex = {{p.x, p.y, p.z, rest->position.w}, {n.x, n.y, n.z, rest->normal.w}};
        memcpy(player->skin.vertices + i * sizeof(vertex), &vertex, sizeof(vertex));
    }
    return NULL;
}

const char *rx_engine_player_motion(RxEnginePlayer *player, BH_PWORK *actor, int mode)
{
    ML_WORK *body = &player->body.part.model;
    if (!actor->mnwP || (unsigned)actor->mtn_no >= RX_ROOM_MOTION_SLOTS ||
        !actor->mlwP || actor->mlwP->objP != body->objP)
        return "invalid native player motion binding";
    const MN_WORK *motion = &actor->mnwP[actor->mtn_no];
    if (!motion->datP || motion->obj_num != player->source.node_count || actor->frm_no < 0 ||
        (unsigned)actor->frm_no / 65536 >= motion->frm_num || ((mode & 2) && !actor->mtn_tp))
        return "invalid original player motion state";
    bhSetMotion(actor, actor->mtn_add, mode, actor->mtn_tp);
    bhCalcTree(NULL, body);
    player->world_pose=0;
    for(unsigned i=0;i<player->source.node_count;++i)memcpy(player->script_matrices[i],body->owP[i].mtx,sizeof(NJS_MATRIX));
    return player_skin(player,player->script_matrices);
}

const char *rx_engine_player_draw(RxEnginePlayer *player, const NJS_CNK_VIEW *view,
                                  RxRoomSubmit submit, void *context)
{
    ML_WORK body = player->body.part.model;
    if (body.objP[player->skin.owner].evalflags & 8) return NULL;
    NJS_CNK_OBJECT mesh = {.evalflags = 7, .model = body.objP[player->skin.owner].model};
    body.objP = &mesh;
    body.obj_num = 1;
    return rx_draw_room_chunks(&body, player->source.source_bytes, 1, view, submit, context);
}

static const char *stage_texture(RxEngineRoom *room, ML_WORK *model, unsigned type,
                                 unsigned id, unsigned version, unsigned ordinal, size_t available)
{
    for (size_t i = 0; i < room->texture_count; ++i) {
        const RxEngineRoomTexture *slot = &room->textures[i];
        if (slot->type == type && slot->id == id && slot->version == version && slot->ordinal == ordinal) {
            if (model) {
                model->texP = slot->list;
                model->flg &= ~0x200u;
            }
            return NULL;
        }
    }
    if (room->texture_count >= available) return "missing room texture block";
    RxEngineRoomTexture *slot = &room->textures[room->texture_count++];
    slot->type = type;
    slot->id = id;
    slot->version = version;
    slot->ordinal = ordinal;
    slot->effect = type == EFFECT_TEX ? (int)id : -1;
    slot->list = model ? model->texP : NULL;
    if (model && type != ROOT_TEX) model->flg |= 0x200u;
    return NULL;
}

const char *rx_engine_room_stage(RxEngineRoom *room)
{
    ROM_WORK *rom = &room->rom;
    if (room->enemy_count > 128 || room->prop_count > 32 || room->item_count > 32)
        return "room actor count exceeds original work arrays";
    size_t table = room_word(room->rdt + 32);
    size_t limit = room->bytes;
    for (size_t i = 0; i < 20; ++i) {
        size_t next = room_word(room->rdt + 16 + i * 4);
        if (next > table && next < limit) limit = next;
    }
    if (!table || limit - table < 4) return "missing room texture directory";
    size_t count = room_word(room->rdt + table);
    if (count > sizeof(room->textures) / sizeof(*room->textures) || count > (limit - table - 4) / 4)
        return "truncated room texture directory";
    size_t payload = table + (count + 1) * 4;
    for (size_t i = 0; i < count; ++i) {
        size_t data = room_word(room->rdt + table + 4 * (i + 1));
        if (data < payload || data > limit || limit - data < 4 || data % 4)
            return "room texture block outside its section";
        size_t end = limit;
        for (size_t j = 0; j < count; ++j) {
            size_t next = room_word(room->rdt + table + 4 * (j + 1));
            if (next > data && next < end) end = next;
        }
        if (!validate_texture_block(room->rdt + data, end - data, NULL))
            return "room texture packet exceeds its block";
        room->textures[i].data = room->rdt + data;
    }
    const char *error = stage_texture(room, &rom->mdl, ROOT_TEX, 0, 0, 0, count);
    if (error) return error;
    for (size_t i = 0; i < room->enemy_count; ++i) {
        const ETTY_WORK *record = &rom->enep[i];
        EngineEnemy *enemy = &room->enemies[i];
        if (record->id >= 128 || (enemy->part_count && (record->id >= 100 || record->mdlver >= 16)))
            return "enemy ID or version exceeds original cache";
        if (!enemy->motion.motions && !room->empty_motion[record->id]) {
            room->empty_motion[record->id] = calloc(RX_ROOM_MOTION_SLOTS, sizeof(MN_WORK));
            if (!room->empty_motion[record->id]) return "cannot allocate empty enemy motion bank";
        }
        unsigned textured = 0;
        NJS_TEXLIST *previous = NULL;
        for (size_t j = 0; j < enemy->part_count; ++j) {
            ML_WORK *model = &enemy->models[j].part.model;
            if (model->texP) {
                error = stage_texture(room, model, ENEMY_TEX, record->id, record->mdlver, textured++, count);
                if (error) return error;
                previous = model->texP;
            } else {
                model->flg &= ~0x200u;
                model->texP = previous;
            }
        }
    }
    for (size_t i = RX_FIRST_ROOM_PROP; i < room->prop_count; ++i) {
        ML_WORK *model = &room->props[i].part.model;
        if (rom->objp[i].id >= 1300) return "prop ID exceeds original cache";
        if (model->texP) {
            error = stage_texture(room, model, PROP_TEX, rom->objp[i].id, 0, 0, count);
            if (error) return error;
        }
    }
    for (size_t i = 0; i < room->item_count; ++i) {
        ML_WORK *model = &room->items[i].part.model;
        if (rom->itmp[i].id >= 200) return "item ID exceeds original cache";
        if (model->texP) {
            error = stage_texture(room, model, ITEM_TEX, rom->itmp[i].id, 0, 0, count);
            if (error) return error;
        }
    }
    for (int i = 0; i < rom->eff_n; ++i) {
        unsigned id = rom->effp[i].id;
        if (id >= 450) return "effect ID exceeds original cache";
        if ((id >= 30 && id < 100) || id >= 400) {
            error = stage_texture(room, NULL, EFFECT_TEX, id, 0, 0, count);
            if (error) return error;
        }
    }
    if (room->texture_count != count) return "unused room texture blocks";
    size_t motion_table = room_word(room->rdt + 24);
    size_t face_index=(room->enemy_count+1)*4;
    if(motion_table>room->bytes || face_index>room->bytes-motion_table ||
       room->bytes-motion_table-face_index<8)return "Missing room facial bank offsets";
    size_t mouth = room_word(room->rdt + motion_table + (room->enemy_count + 1) * 4);
    size_t lip = room_word(room->rdt + motion_table + (room->enemy_count + 2) * 4);
    if(mouth>=room->bytes || lip>=room->bytes)return "Room facial bank offset exceeds RDT";
    if(mouth){error=rx_face_bank_validate(room->rdt+mouth,room->bytes-mouth,0,NULL);if(error)return error;}
    if(lip){error=rx_face_bank_validate(room->rdt+lip,room->bytes-lip,1,NULL);if(error)return error;}
    for(size_t i=0;i<room->enemy_count;++i)if(room->enemies[i].face_mesh){
        error=rx_face_mesh_aux_init(room->enemies[i].face_mesh);if(error)return error;
        error=rx_face_pose_create(room->enemies[i].face_mesh,&room->enemies[i].models[0].part,&room->enemies[i].face_pose);if(error)return error;
    }
    room->mouth = mouth ? room->rdt + mouth : NULL;
    room->lip = lip ? room->rdt + lip : NULL;
    return NULL;
}

const RxEngineRoomTexture *rx_engine_room_textures(RxEngineRoom *room, size_t *count)
{
    *count = room->texture_count;
    return room->textures;
}

MN_WORK *rx_engine_room_empty_enemy_motion(RxEngineRoom *room, unsigned id)
{
    return id < 128 ? room->empty_motion[id] : NULL;
}

void rx_engine_room_face_data(RxEngineRoom *room, unsigned char **mouth, unsigned char **lip)
{
    *mouth = room->mouth;
    *lip = room->lip;
}
void rx_engine_room_face_spans(RxEngineRoom *room,const void **mouth,size_t *mouth_bytes,const void **lip,size_t *lip_bytes)
{
    *mouth=room->mouth;*lip=room->lip;
    *mouth_bytes=room->mouth?room->bytes-(size_t)(room->mouth-room->rdt):0;
    *lip_bytes=room->lip?room->bytes-(size_t)(room->lip-room->rdt):0;
}

size_t rx_engine_room_model_bytes(const RxEngineRoom *room) { return room->model_bytes; }

#define SCRIPT_PREFIX(f) _Static_assert(offsetof(BH_PWORK,f)==offsetof(O_WRK,f),"Native linked prefix changed: " #f)
SCRIPT_PREFIX(mtx);SCRIPT_PREFIX(mtxbuf);SCRIPT_PREFIX(lkwkp);SCRIPT_PREFIX(lkono);SCRIPT_PREFIX(lox);SCRIPT_PREFIX(mlwP);SCRIPT_PREFIX(type);
#undef SCRIPT_PREFIX
MN_WORK *rx_engine_player_motion_bank(RxEnginePlayer *player){return player?player->native_motion.motions:NULL;}
unsigned char *rx_engine_player_translation_bank(RxEnginePlayer *player){return player?player->translation_bank:NULL;}
const char *rx_engine_player_locomotion_translation_check(RxEnginePlayer *p,unsigned slot,unsigned frame)
{
    if(!p||!p->translation_bank||sys->plzmtp!=p->translation_bank||slot>=6)
        return "Locomotion requires its owned native translation bank";
    /* The original getter traverses every preceding size word. Validate each
       retained header at its owned address before that unbounded getter runs. */
    for(unsigned i=0;i<=slot;++i)if(!p->locomotion_blocks[i]||room_word(p->locomotion_blocks[i]-4)!=p->locomotion_block_bytes[i])
        return "Locomotion translation traversal has a modified native block header";
    size_t bytes=p->locomotion_block_bytes[slot];unsigned char *block=p->locomotion_blocks[slot];
    if((size_t)frame+1>=bytes/4)
        return "Locomotion translation frame/header exceeds its retained native block";
    float speed=player_translation(block+((size_t)frame+1)*4);
    if(!isfinite(speed)||fabsf(speed)>16)return "Invalid original locomotion translation speed";
    return NULL;
}
const char *rx_engine_player_walk_translation_check(RxEnginePlayer *p,unsigned slot,unsigned frame)
{
    if(slot>=3)return "Walking translation selector exceeds its native family";
    return rx_engine_player_locomotion_translation_check(p,slot,frame);
}
static const char *script_clip(RxEnginePlayer *p,unsigned slot)
{
    if(slot>=p->motion_slots)return "Script motion exceeds original player bank";
    const RxMotion *source=p->motion+slot;const MN_WORK *native=p->native_motion.motions+slot;
    size_t offset=0;for(unsigned i=0;i<slot;++i)offset+=p->motion[i].node_count;
    if(!source->source||!source->frame_count||source->frame_count>=32768||source->node_count!=p->source.node_count||
       native->datP!=source->source||native->obj_num!=source->node_count||native->frm_num!=source->frame_count||
       native->flg!=source->flags||native->atrP!=(void *)source->attributes||
       native->md2P!=p->native_motion.tracks+offset||(native->flg&0x30))return "Invalid or unported native script motion format";
    for(unsigned i=0;i<source->node_count;++i)if(native->md2P[i].p[0]!=source->tracks[i].position||native->md2P[i].p[1]!=source->tracks[i].angle)return "Foreign native script motion track";
    return NULL;
}
static int script_transform(const BH_PWORK *a)
{
    const float values[]={a->px,a->py,a->pz,a->lox,a->loy,a->loz};
    if(a->mtx!=(void *)a->mtxbuf)return 0;
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i)if(!isfinite(values[i]))return 0;
    return 1;
}
extern int lNaMatMatrixStuckCnt,lNaMatMatrixStuckMax;
const char *rx_engine_player_script_motion_check(RxEnginePlayer *p,RxEngineWeapon *weapon,BH_PWORK *a,unsigned requested,unsigned update,int runtime)
{
    if(!p||!a||a->mlwP!=a->mdl||a->mlwP->objP!=p->body.part.model.objP||a->mlwP->owP!=p->body.part.model.owP||
       a->mlwP->obj_num!=p->source.node_count||a->mnwPb!=p->native_motion.motions||!script_transform(a)||(a->flg&0x80))return "Script motion requires owned unlinked player model work";
    const char *error=script_clip(p,requested);if(error)return error;
    if(runtime&&(error=script_clip(p,a->mtn_no)))return error;
    if(!pNaMatMatrixStuckPtr||lNaMatMatrixStuckCnt<0||lNaMatMatrixStuckMax-lNaMatMatrixStuckCnt<(int)p->source.node_count+66)return "Insufficient native script matrix stack";
    /* Original source reads task/model state only through these bounded owned views.
       Keep non-hand/non-shadow links gated until their publication contracts are migrated. */
    for(unsigned i=0;i<p->source.node_count;++i){
        for(unsigned j=0;j<3;++j)if(!isfinite(a->mlwP->objP[i].pos[j]))return "Nonfinite native script model node";
        for(unsigned j=0;j<16;++j)if(!isfinite(a->mlwP->owP[i].mtx[j]))return "Nonfinite native script joint matrix";
    }
    if(update!=0&&update!=4&&update!=6&&update!=7)return NULL;
    if(sys->ewk_n<0||sys->ewk_n>128||rom->obj_n<0||rom->obj_n>32||rom->itm_n<0||rom->itm_n>32||
       (rom->obj_n&&!sys->obwp)||(rom->itm_n&&!sys->itwp))return "Script link traversal exceeds native work pools";
    const struct {BH_PWORK *base;unsigned count;size_t stride;int objects;} pools[]={
        {ene,(unsigned)sys->ewk_n,sizeof(*ene),0},{(BH_PWORK *)sys->obwp,(unsigned)rom->obj_n,sizeof(O_WRK),1},
        {(BH_PWORK *)sys->itwp,(unsigned)rom->itm_n,sizeof(O_WRK),0},{(BH_PWORK *)eff,512,sizeof(O_WRK),0}};
    for(unsigned pool=0;pool<4;++pool)for(unsigned i=0;i<pools[pool].count;++i){
        BH_PWORK *child=(BH_PWORK *)((unsigned char *)pools[pool].base+i*pools[pool].stride);
        if(child==a||(child->flg&0x81)!=0x81||child->lkwkp!=(unsigned char *)a)continue;
        if(pool==3&&child->id==1&&child->type==1&&child->mlwP==&sys->efm[1]&&(child->flg&0x200080)==0x200080){
            if(child->lkono<0||(unsigned)child->lkono>=a->mlwP->obj_num||!script_transform(child))return "Invalid original shadow link work";
            continue;
        }
        if(!pools[pool].objects||i>=2||!weapon)return "Unported non-hand/non-shadow script link target";
        const ML_WORK *owned=&weapon->models[i].part.model;
        if(child->mlwP!=child->mdl||child->mlwP->objP!=owned->objP||child->mlwP->owP!=owned->owP||child->mlwP->obj_num!=owned->obj_num||
           owned->obj_num>64||child->lkono<0||(unsigned)child->lkono>=a->mlwP->obj_num||!script_transform(child))return "Foreign or invalid original hand link work";
    }
    return NULL;
}

const char *rx_engine_room_camera_init_check(RxEngineRoom *r)
{
    if(!r||rom->cutp!=r->cuts||rom->cut_n!=(int)r->cut_count||!r->cut_count||
       rom->mdl.objP!=r->root.part.model.objP||rom->mdl.obj_num!=r->root.part.model.obj_num||
       rom->lgtp!=r->lights||rom->lgt_n!=r->rom.lgt_n||rom->evlp!=r->event_lights||rom->evl_n!=r->rom.evl_n)
        return "Camera initialization requires its owned native room banks";
    if(!plp||cmat!=&crmat||cam.mtx!=cmmat||cam.mtxb!=cmmat+1||!pNaMatMatrixStuckPtr||
       lNaMatMatrixStuckCnt<0||lNaMatMatrixStuckMax-lNaMatMatrixStuckCnt<16)
        return "Camera initialization requires native player/view matrices";
    if(!(sys->sp_flg&0x40))return "Camera initialization requires the original cut task";
    if((unsigned)sys->stg_no>=10)return "Invalid native room camera stage";
    /* RM clip tables ignore rcase. Keep authored enlarged culling volumes
       gated until their host viewport/culling publication is migrated. */
    static const unsigned char custom[][2]={{0,7},{0,9},{0,15},{1,0},{1,6},{3,8},{3,12},{3,16},{5,54},{5,63},{5,66},{5,68},{6,3},{6,9},{7,3},{7,7},{7,8},{7,9},{7,20},{7,22},{9,3},{9,5},{9,12},{9,15},{9,18},{9,32}};
    if(sys->ts_flg&0x200)for(unsigned i=0;i<sizeof(custom)/sizeof(*custom);++i)
        if(sys->stg_no==custom[i][0]&&(unsigned char)sys->rom_no==custom[i][1])return "Original room-camera custom clip volume is not ported yet";
    if(sys->gm_flg&0x40)return "Original player-eye camera initialization is not ported yet";
    if(cam.camver<0||cam.camver>1||cam.ncut<0||cam.ncut>=rom->cut_n||plp->flr_no< -2||plp->flr_no>29)
        return "Camera initialization has an invalid cut, version or floor";
    const float position[]={plp->px,plp->py,plp->pz,plp->gpx,plp->gpy,plp->gpz,cam.ofx,cam.ofy,cam.ofz};
    for(unsigned i=0;i<sizeof(position)/sizeof(*position);++i)if(!isfinite(position[i]))return "Nonfinite native camera initialization position";
    for(unsigned i=0;i<32;++i)if(!isfinite(rom->grand[i]))return "Nonfinite native camera floor table";
    if(rom->mdl.obj_num>512||rom->lgt_n<1||rom->lgt_n>256||rom->evl_n<0||rom->evl_n>256)
        return "Camera initialization hide lists exceed native banks";
    for(size_t i=0;i<r->cut_count;++i){
        CUT_WORK *cut=r->cuts+i;
        if(cut->cuttp!=r->areas[i]||cut->ctab_n!=r->area_counts[i])return "Foreign native camera area bank";
        for(unsigned j=0;j<cut->ctab_n;++j){CUT_WRK *area=cut->cuttp+j;
            if(area->atr_tp<0||area->atr_tp>4||!isfinite(area->minx)||!isfinite(area->maxx)||!isfinite(area->minz)||!isfinite(area->maxz)||area->minx>area->maxx||area->minz>area->maxz||(area->atr_tp&&(area->minx==area->maxx||area->minz==area->maxz)))
                return "Invalid native camera cut area";
        }
        for(unsigned v=0;v<2;++v){CAM_WRK *key=&cut->cam[v];
            const float values[]={cut->cx,cut->cz,cut->cw,cut->cd,key->px,key->py,key->pz,key->ln,key->w,key->h,key->d,key->y0,key->y1,key->y2,key->y3,key->am_spd,key->fog_nr,key->fog_fr};
            for(unsigned j=0;j<sizeof(values)/sizeof(*values);++j)if(!isfinite(values[j]))return "Nonfinite native room camera key";
            if(!(key->flg&0x20)&&key->pers<=0)return "Invalid native room camera perspective";
            if(key->spd<0||key->aa_spd<0)return "Invalid native active camera speed";
            if((key->y0||key->y1||key->y2||key->y3||key->laz0||key->laz1||key->laz2||key->laz3||(key->flg&0x20))&&(!cut->cw||!cut->cd))return "Degenerate native active camera extent";
        }
    }
    /* Keep unvalidated light links and animations gated. Room hide masks can
       enable any authored light, so validate the whole owned bank. */
    for(int i=0;i<rom->lgt_n;++i){LGT_WORK *light=rom->lgtp+i;
        const float values[]={light->px,light->py,light->pz,light->vx,light->vy,light->vz,light->r,light->g,light->b,light->nr,light->fr,light->lx,light->ly,light->lz,light->wpx,light->wpy,light->wpz,light->wvx,light->wvy,light->wvz,light->wr,light->wg,light->wb,light->wnr,light->wfr};
        for(unsigned j=0;j<sizeof(values)/sizeof(*values);++j)if(!isfinite(values[j]))return "Nonfinite native room light";
        if(light->lkflg==1&&(light->lkono<0||(unsigned)light->lkono>=plp->mlwP->obj_num))return "Invalid player-linked room light joint";
        if(light->lkflg>1)return "Original non-player linked room light is not ported yet";
        if(light->type!=0&&light->type!=1&&light->type!=4&&light->type!=100)return "Original animated room light type is not ported yet";
    }
    return NULL;
}

const char *rx_engine_room_actor_model_check(RxEngineRoom *r,BH_PWORK *actor)
{
    if(!r||rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count||r->enemy_count>128)
        return "Actor model recalculation requires its owned native room mapping";
    if(!actor)return NULL; /* Static bank preflight has no selected task yet. */
    size_t record;
    for(record=0;record<r->enemy_count;++record)
        if((rom->enep[record].flg&1)&&rom->enep[record].wrk_no<128&&actor==ene+rom->enep[record].wrk_no)break;
    if(record==r->enemy_count||actor->id!=rom->enep[record].id||actor->id==1)
        return "Actor model recalculation requires an owned non-companion task target";
    EngineEnemy *owned=r->enemies+record;
    if(!owned->part_count)return "Missing owned actor model";
    const ML_WORK *model=owned->face_pose?rx_face_pose_model(owned->face_pose):&owned->models[0].part.model;
    if(actor->mlwP!=actor->mdl||actor->mlwP->objP!=model->objP||!owned->script_work||
       actor->mlwP->owP!=owned->script_work||actor->mlwP->obj_num!=model->obj_num||!model->obj_num||model->obj_num>64||
       !script_transform(actor)||(actor->flg&0x80))return "Foreign or linked native actor model/matrix";
    if(!pNaMatMatrixStuckPtr||lNaMatMatrixStuckCnt<0||lNaMatMatrixStuckMax-lNaMatMatrixStuckCnt<(int)model->obj_num+2)
        return "Insufficient native actor model matrix stack";
    if(actor->flg&0x1000)for(unsigned k=0;k<16;++k)
        if(!isfinite(((float *)actor->mtx)[k]))return "Nonfinite retained actor root matrix";
    int depth=1;
    for(unsigned node=0;node<model->obj_num;++node){
        const NJS_CNK_OBJECT *object=model->objP+node;
        unsigned shape=(object->child!=NULL)|((object->sibling!=NULL)<<1);
        if(shape!=owned->script_links[node])return "Foreign native actor model hierarchy";
        if(shape&2)++depth;
        if(!(shape&1))--depth;
        if(depth<0||(!depth&&node+1<model->obj_num))return "Unbalanced native actor model hierarchy";
        for(unsigned axis=0;axis<3;++axis)if(!isfinite(object->pos[axis]))return "Nonfinite native actor model node";
        for(unsigned k=0;k<16;++k)if(!isfinite(owned->script_work[node].mtx[k]))return "Nonfinite native actor model joint matrix";
    }
    return depth?"Unbalanced native actor model hierarchy":NULL;
}

const char *rx_engine_room_interaction_light_check(RxEngineRoom *r)
{
    if(!r||rom->lgtp!=r->lights||rom->lgt_n!=r->rom.lgt_n||rom->lgt_n<2)
        return "Original interaction light requires its owned room light bank";
    return NULL;
}

const char *rx_engine_room_script_motion_check(RxEngineRoom *r,const unsigned char *p,BH_PWORK *actor)
{
    if((p[3]!=1&&!(p[1]==34&&(p[3]==2||p[3]==3)))||p[7]!=6)return "Original room motion actor/interpolation mode is not ported yet";
    if(!r||!r->room_motion_source||sys->rmthp!=r->room_motion.motions)return "Room script motion requires its owned native bank";
    unsigned slot=p[4];
    if(actor){
        size_t record;
        if(rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count)return "Foreign native room actor mapping";
        for(record=0;record<r->enemy_count;++record)if((rom->enep[record].flg&1)&&rom->enep[record].wrk_no<128&&actor==&ene[rom->enep[record].wrk_no])break;
        if(record==r->enemy_count||actor->id!=rom->enep[record].id||actor->id==1)return "Original room motion requires an owned non-companion enemy";
        EngineEnemy *owned=r->enemies+record;
        const ML_WORK *model=owned->face_pose?rx_face_pose_model(owned->face_pose):&owned->models[0].part.model;
        if(!owned->part_count||actor->mlwP!=actor->mdl||actor->mlwP->objP!=model->objP||
           actor->mlwP->owP!=owned->script_work||!owned->script_work||actor->mlwP->obj_num!=model->obj_num||
           actor->mtx!=(void *)actor->mtxbuf||(actor->flg&0x80))return "Foreign native room-motion actor model/matrix";
        if(p[3]==1)slot=(actor->mtn_no&~255u)|slot;
    }
    if(slot>=r->room_motion_slots)return "Room motion selector exceeds owned bank";
    const RxMotion *source=r->room_motion_source+slot;const MN_WORK *native=r->room_motion.motions+slot;
    size_t offset=0;for(unsigned i=0;i<slot;++i)offset+=r->room_motion_source[i].node_count;
    if(!source->source||!source->node_count||source->node_count>64||!source->frame_count||source->frame_count>=32768||
       native->datP!=source->source||native->obj_num!=source->node_count||native->frm_num!=source->frame_count||native->flg!=source->flags||
       native->md2P!=r->room_motion.tracks+offset||native->atrP!=(void *)source->attributes)return "Foreign or invalid native room-motion clip";
    unsigned frame=p[1]==34?(unsigned)p[8]|(unsigned)p[9]<<8:0;
    if(frame>=source->frame_count)return "Room motion frame exceeds its owned clip";
    unsigned next=frame+1<source->frame_count?frame+1:0;
    if(!(source->flags&0x30)&&!source->tracks[0].position)return "Original fast room motion lacks root translation";
    for(unsigned i=0;i<source->node_count;++i){
        if(native->md2P[i].p[0]!=source->tracks[i].position||native->md2P[i].p[1]!=source->tracks[i].angle)return "Foreign native room-motion track";
        if(source->tracks[i].position)for(unsigned f=0;f<2;++f)for(unsigned k=0;k<3;++k){float value;memcpy(&value,source->tracks[i].position+(f?next:frame)*12+k*4,4);if(!isfinite(value))return "Nonfinite selected room-motion key";}
    }
    if(!actor)return NULL;
    const ML_WORK *face=rx_face_control_model(actor);
    if(actor->mlwP->obj_num>64||source->node_count>actor->mlwP->obj_num||!script_transform(actor))return "Room motion exceeds its native actor skeleton";
    if(face&&(face->objP!=actor->mlwP->objP||face->owP!=actor->mlwP->owP||face->obj_num!=31||source->node_count>22))return "Room motion exceeds its owned facial skeleton";
    int evaluate=p[6]==0||p[6]==2||p[6]==5||p[6]==6;
    /* Explicit-frame modes 2/3 overwrite the full clip number and clear the
       actor's pending blend when the original interpolation operand is 6. */
    if(evaluate&&actor->hokan_count&&p[3]==1)return "Original room-motion pose blending is not ported yet";
    if(!pNaMatMatrixStuckPtr||lNaMatMatrixStuckCnt<0||lNaMatMatrixStuckMax-lNaMatMatrixStuckCnt<(int)actor->mlwP->obj_num+2)return "Insufficient native room-motion matrix stack";
    for(unsigned i=0;i<actor->mlwP->obj_num;++i){
        /* The original facial setup inserts four nodes, then protects all
           nine face nodes. Motion consumes 22 tracks across the 31 entries. */
        if((actor->mlwP->owP[i].flg&4)!=(face&&i>=6&&i<=14?4u:0u))return "Original sparse node-remap motion is not ported yet";
        for(unsigned k=0;k<3;++k)if(!isfinite(actor->mlwP->objP[i].pos[k]))return "Nonfinite room-motion model node";
        for(unsigned k=0;k<16;++k)if(!isfinite(actor->mlwP->owP[i].mtx[k]))return "Nonfinite room-motion joint matrix";
    }
    if(p[6]!=0&&p[6]!=4&&p[6]!=6&&p[6]!=7)return NULL;
    if(sys->ewk_n<0||sys->ewk_n>128||rom->obj_n!=(int)r->prop_count||rom->itm_n!=(int)r->item_count||
       r->prop_count>32||r->item_count>32||sys->obwp!=r->sound_objects||sys->itwp!=r->sound_items)
        return "Room motion link traversal exceeds owned native pools";
    const struct {BH_PWORK *base;unsigned count;size_t stride;} pools[]={
        {ene,(unsigned)sys->ewk_n,sizeof(*ene)},{(BH_PWORK *)sys->obwp,(unsigned)rom->obj_n,sizeof(O_WRK)},
        {(BH_PWORK *)sys->itwp,(unsigned)rom->itm_n,sizeof(O_WRK)},{(BH_PWORK *)eff,512,sizeof(O_WRK)}};
    for(unsigned pool=0;pool<4;++pool)for(unsigned i=0;i<pools[pool].count;++i){
        BH_PWORK *child=(BH_PWORK *)((unsigned char *)pools[pool].base+i*pools[pool].stride);
        if(child==actor||(child->flg&0x81)!=0x81||child->lkwkp!=(unsigned char *)actor)continue;
        if(!script_transform(child)||child->lkono<0||(unsigned)child->lkono>=actor->mlwP->obj_num)
            return "Invalid original room actor link transform/joint";
        if(pool==3){
            /* This pass updates effect root matrices only, without traversing
               their models. Keep the known original shadow family bounded. */
            if(child->id!=1||child->type!=1||child->mlwP!=&sys->efm[1]||(child->flg&0x200080)!=0x200080)
                return "Original room actor linked effect family is not ported yet";
            continue;
        }
        if(pool==0|| (pool==1&&i<4))return "Original room actor linked character/hand model is not ported yet";
        unsigned item=pool==2;
        const ML_WORK *model=&(item?r->items:r->props)[i].part.model;
        O_WORK *work=(item?r->sound_item_work:r->sound_prop_work)[i];
        const unsigned char *links=(item?r->item_links:r->prop_links)[i];
        if(!work||!model->obj_num||model->obj_num>64||child->mlwP!=child->mdl||
           child->mlwP->objP!=model->objP||child->mlwP->owP!=work||child->mlwP->obj_num!=model->obj_num)
            return "Foreign original room actor linked model work";
        if(lNaMatMatrixStuckMax-lNaMatMatrixStuckCnt<(int)model->obj_num+2)
            return "Insufficient native linked-model matrix stack";
        int depth=1;
        for(unsigned node=0;node<model->obj_num;++node){
            const NJS_CNK_OBJECT *object=model->objP+node;
            unsigned shape=(object->child!=NULL)|((object->sibling!=NULL)<<1);
            if(shape!=links[node])return "Foreign original room linked-model hierarchy";
            if(shape&2)++depth;
            if(!(shape&1))--depth;
            if(depth<0||(!depth&&node+1<model->obj_num))return "Unbalanced original room linked-model hierarchy";
            for(unsigned axis=0;axis<3;++axis)if(!isfinite(object->pos[axis]))return "Nonfinite original room linked-model node";
            for(unsigned k=0;k<16;++k)if(!isfinite(work[node].mtx[k]))return "Nonfinite original room linked-model matrix";
        }
        if(depth)return "Unbalanced original room linked-model hierarchy";
    }
    return NULL;
}

const char *rx_engine_room_retain_actor_work(RxEngineRoom *r)
{
    if(!r||rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count)return "Cannot retain foreign native room actor work";
    for(size_t i=0;i<r->enemy_count;++i)if(rom->enep[i].flg&1){
        if(rom->enep[i].wrk_no>=128)return "Native room actor slot exceeds work pool";
        BH_PWORK *a=&ene[rom->enep[i].wrk_no];EngineEnemy *owned=r->enemies+i;
        if(!owned->part_count||a->id!=rom->enep[i].id||a->mlwP!=a->mdl||a->mlwP->objP!=owned->models[0].part.model.objP||
           a->mlwP->obj_num!=owned->models[0].part.model.obj_num||!a->mlwP->owP)return "Missing owned native room actor matrices";
        uintptr_t address=(uintptr_t)a->mlwP->owP,end=(uintptr_t)sys->memp;
        if(address%_Alignof(O_WORK)||address>end||a->mlwP->obj_num>(end-address)/sizeof(O_WORK))return "Native room actor matrices exceed retained arena";
        if(owned->script_work&&owned->script_work!=a->mlwP->owP)return "Native room actor matrix owner is occupied";
        owned->script_work=a->mlwP->owP;
        if(owned->face_pose){
            /* bhFinishRoom calls bhInitMask after keeping the original work.
               fmCnkInitContext expands/flat-packs the hierarchy and replaces
               that work. The native face owner has already staged both. */
            const ML_WORK *face=rx_face_pose_model(owned->face_pose);
            if(!face||face!=rx_face_control_model(a)||face->obj_num!=31||!face->objP||!face->owP)return "Missing owned native facial actor model";
            a->mdl[0]=*face;
            owned->script_work=face->owP;
            bhCalcTree(NULL,a->mlwP);
        }
        if(a->mlwP->obj_num<=64)for(unsigned node=0;node<a->mlwP->obj_num;++node)
            owned->script_links[node]=(a->mlwP->objP[node].child!=NULL)|((a->mlwP->objP[node].sibling!=NULL)<<1);
    }
    if(rom->obj_n!=(int)r->prop_count||rom->itm_n!=(int)r->item_count||r->prop_count>32||r->item_count>32)return "Native sound object/item pool exceeds room owner";
    r->sound_objects=sys->obwp;r->sound_items=sys->itwp;
    /* bhFirstGameStart allocates both fixed 32-entry pools before the RDT.
       Retain their complete native extent, including unused original slots. */
    for(unsigned family=0;family<2;++family){
        uintptr_t base=(uintptr_t)(family?r->sound_items:r->sound_objects),end=(uintptr_t)sys->memp;
        if(!freemem||base<(uintptr_t)freemem||base%_Alignof(O_WRK)||base>end||32>(end-base)/sizeof(O_WRK))return "Original native object/item pool exceeds its owned arena";
    }
    for(unsigned family=0;family<2;++family){O_WRK *base=family?r->sound_items:r->sound_objects;size_t count=family?r->item_count:r->prop_count;EngineModel *models=family?r->items:r->props;O_WORK **work=family?r->sound_item_work:r->sound_prop_work;
        if(count&&!base)return "Missing native sound object pool";
        for(size_t i=family?0:4;i<count;++i){if(!models[i].part.model.objP||!models[i].part.model.obj_num)continue;O_WRK *a=base+i;
            if(a->mlwP!=a->mdl||a->mlwP->objP!=models[i].part.model.objP||a->mlwP->obj_num!=models[i].part.model.obj_num||!a->mlwP->owP)return "Foreign native sound object model work";
            uintptr_t address=(uintptr_t)a->mlwP->owP,end=(uintptr_t)sys->memp;
            if(address%_Alignof(O_WORK)||address>end||a->mlwP->obj_num>(end-address)/sizeof(O_WORK))return "Native sound object matrices exceed arena";
            work[i]=a->mlwP->owP;
            if(a->mlwP->obj_num<=64)for(unsigned node=0;node<a->mlwP->obj_num;++node)
                (family?r->item_links:r->prop_links)[i][node]=
                    (a->mlwP->objP[node].child!=NULL)|((a->mlwP->objP[node].sibling!=NULL)<<1);
        }
    }
    return NULL;
}

const char *rx_engine_room_item_speed_check(RxEngineRoom *r,unsigned slot)
{
    if(!r||!r->sound_items||sys->itwp!=r->sound_items||rom->itmp!=r->rom.itmp||rom->itm_n!=(int)r->item_count||slot>=32)
        return "Item speed requires its original owned 32-entry native pool";
    return NULL;
}

extern void (*Subpl_tbl[7])(BH_PWORK*);
extern void (*subpl_init_tbl[120])(BH_PWORK*);
extern void (*subpl_mv_tbl[1])(BH_PWORK*);
extern void (*subpl_mv00_tbl[15])(BH_PWORK*);
extern void (*subpl_mv01_tbl[15])(BH_PWORK*);
static int owned_enemy_bank(const RxEngineRoom*,unsigned);
static const char *normal_actor_tick_check(RxEngineRoom *r,unsigned record,BH_PWORK *a)
{
    int initialize=a->mode0==0,reset=!initialize&&a->mode3==0;
    if(a->id<=90||a->id>=100||a->mode2||a->ct2||(a->flg&0x1080)||
       (!initialize&&(a->mode0!=1||a->mode3>1))||!owned_enemy_bank(r,a->id)||a->mnwP!=sys->emtp[a->id])
        return "Unported original normal actor controller/bank/link state";
    if(initialize){
        if(Subpl_tbl[0]!=init_subpl||subpl_init_tbl[a->id-40]!=em60_init)return "Foreign original normal actor initializer";
    }else if(Subpl_tbl[1]!=move_subpl||subpl_mv_tbl[0]!=bhEne_Event2||subpl_mv00_tbl[0]!=mv00_subpl0)
        return "Foreign original normal actor callback";
    const char *error=rx_engine_room_actor_model_check(r,a);if(error)return error;
    unsigned slot=initialize?0:reset?a->mode1:a->mtn_no;
    EngineEnemy *owner=NULL;
    for(size_t i=0;i<r->enemy_count;++i)if(r->rom.enep[i].id==a->id&&r->enemies[i].motion.motions==a->mnwP){owner=r->enemies+i;break;}
    const RxMotion *source=NULL;
    if(owner){
        if(slot>=owner->motion_slots)return "Normal actor clip exceeds its retained enemy bank";
        source=owner->motion_source+slot;
    }else if(a->mnwP!=r->empty_motion[a->id]||slot>=RX_ROOM_MOTION_SLOTS)return "Foreign original empty actor motion bank";
    const MN_WORK *motion=a->mnwP+slot;
    int empty=!source||!source->source;
    if(empty){
        const MN_WORK zero={0};if(memcmp(motion,&zero,sizeof(zero)))return "Modified original empty actor motion entry";
    }else{
        size_t offset=0;for(unsigned i=0;i<slot;++i)offset+=owner->motion_source[i].node_count;
        if(!source->node_count||source->node_count>64||!source->frame_count||source->frame_count>=32768||
           motion->datP!=source->source||motion->obj_num!=source->node_count||motion->frm_num!=source->frame_count||motion->flg!=source->flags||
           motion->md2P!=owner->motion.tracks+offset||motion->atrP!=(void *)source->attributes)
            return "Foreign original normal actor motion header/tracks";
        unsigned frame=initialize?0:(unsigned)a->frm_no>>16,next=frame+1<source->frame_count?frame+1:0;
        unsigned md=initialize?0x20:a->mtn_md&~(reset?0x100u:0u);
        int count=initialize?0:reset?8:a->hokan_count,step=initialize?32768:(int)a->mtn_add;
        if((!initialize&&a->frm_no<0)||frame>=source->frame_count||(md&~0x28u)||(!initialize&&a->mtn_tp)||
           count<0||count>8||(!initialize&&!reset&&a->hokan_rate>65536))return "Unported normal actor frame/blend/remap state";
        unsigned eligible=0;for(unsigned node=0;node<a->mlwP->obj_num;++node)eligible+=!(a->mlwP->owP[node].flg&4);
        if(source->node_count>eligible||(!(source->flags&0x30)&&!source->tracks[0].position))return "Normal actor motion exceeds its native mapping or lacks its root key";
        int64_t period=(int64_t)source->frame_count*65536,clock=initialize?0:a->frm_no,future=clock+step;
        if(step>period||step< -period||future>INT32_MAX||future<INT32_MIN)return "Normal actor step exceeds original single-wrap clock";
        for(unsigned node=0;node<source->node_count;++node){
            if(motion->md2P[node].p[0]!=source->tracks[node].position||motion->md2P[node].p[1]!=source->tracks[node].angle)
                return "Foreign original normal actor motion key";
            if(source->tracks[node].position)for(unsigned neighbor=0;neighbor<2;++neighbor)for(unsigned axis=0;axis<3;++axis){float value;
                memcpy(&value,source->tracks[node].position+(size_t)(neighbor?next:frame)*12+axis*4,4);
                if(!isfinite(value)||fabsf(value)>1000000)return "Invalid original normal actor translation key";
            }
        }
    }
    const float root[]={a->px,a->py,a->pz};for(unsigned i=0;i<3;++i)if(fabsf(root[i])>1000000)return "Normal actor transform exceeds native range";
    int face=r->enemies[record].face_pose!=NULL;
    for(unsigned node=0;node<a->mlwP->obj_num;++node){
        if((a->mlwP->owP[node].flg&~6u)||((a->mlwP->owP[node].flg&4)!=(face&&node>=6&&node<=14?4u:0u)))
            return "Unported normal actor matrix override or node mapping";
        for(unsigned axis=0;axis<3;++axis)if(fabsf(a->mlwP->objP[node].pos[axis])>1000000)return "Normal actor node exceeds native range";
    }
    return a->mskp?rx_face_control_tick_check(a):NULL;
}
static const char *actor_tick_check(RxEngineRoom *r,unsigned record,BH_PWORK *a)
{
    if(a->mode0==0||(a->mode0==1&&!a->mode2&&!a->ct2&&a->mode3<=1))return normal_actor_tick_check(r,record,a);
    /* MotionPauseSet leaves modes 3/4 untouched when SubCtr.128 restores the
       enemy bank. The original callback 0 deliberately skips bhSetMotion in
       these states, but bhSubpl still publishes the current model. */
    if(a->id>90&&a->id<160&&a->mode0==1&&!a->mode2&&!a->ct2&&(a->mode3==3||a->mode3==4)){
        if((a->flg&0x1080)||Subpl_tbl[1]!=move_subpl||subpl_mv_tbl[0]!=bhEne_Event2||subpl_mv00_tbl[0]!=mv00_subpl0||
           !owned_enemy_bank(r,a->id)||a->mnwP!=sys->emtp[a->id])return "Foreign original held actor callback/bank/link state";
        const char *error=rx_engine_room_actor_model_check(r,a);if(error)return error;
        const float root[]={a->px,a->py,a->pz};
        for(unsigned k=0;k<3;++k)if(fabsf(root[k])>1000000)return "Held actor transform exceeds native range";
        for(unsigned node=0;node<a->mlwP->obj_num;++node){
            if(a->mlwP->owP[node].flg&~6u)return "Held actor matrix override is not ported yet";
            for(unsigned k=0;k<3;++k)if(fabsf(a->mlwP->objP[node].pos[k])>1000000)return "Held actor node exceeds native range";
        }
        if(a->mskp){error=rx_face_control_tick_check(a);if(error)return error;}
        return NULL;
    }
    if(a->id<=90||a->id>=160||(a->flg&0x1080)||a->mode3>1||a->ct2!=6||a->mnwP!=sys->rmthp||
       a->mtn_tp||(a->mtn_md!=0x20&&a->mtn_md!=0x28)||a->frm_no<0)
        return "Original continuous actor mode/bank/link state is not ported yet";
    if(a->mode0==1){
        if(a->mode2||Subpl_tbl[1]!=move_subpl||subpl_mv_tbl[0]!=bhEne_Event2||subpl_mv00_tbl[6]!=mv00_subpl0_1)
            return "Foreign or unsupported original actor motion callback";
        if(a->mode3&&a->hokan_count)return "Continuous actor pose blending is not ported yet";
    }else if(a->mode0==5||a->mode0==6){
        if(Subpl_tbl[a->mode0]!=em_sce||subpl_mv01_tbl[6]!=mv00_subpl2_0)return "Foreign original frozen actor callback";
    }else return "Original continuous actor controller is not ported yet";
    unsigned clip=a->mode3==0?a->mode1:a->mtn_no,frame=(unsigned)a->frm_no>>16;
    if(clip>255||frame>65535)return "Continuous actor clip/frame exceeds native selector";
    unsigned char command[]={0x69,34,0,2,clip,0,3,6,frame,frame>>8};
    const char *error=rx_engine_room_script_motion_check(r,command,a);if(error)return error;
    const MN_WORK *motion=sys->rmthp+clip;int step=(int)a->mtn_add;
    int64_t period=(int64_t)motion->frm_num*65536,next=(int64_t)a->frm_no+step;
    if(a->mode0==1&&(step>period||step< -period||next>INT32_MAX||next<INT32_MIN))return "Continuous actor motion exceeds original single-wrap range";
    const float root[]={a->px,a->py,a->pz};for(unsigned i=0;i<3;++i)if(fabsf(root[i])>1000000)return "Continuous actor transform exceeds native range";
    for(unsigned i=0;i<a->mlwP->obj_num;++i){const NJS_CNK_OBJECT *node=a->mlwP->objP+i;
        unsigned links=(node->child!=NULL)|((node->sibling!=NULL)<<1);
        if(links!=r->enemies[record].script_links[i]||(a->mlwP->owP[i].flg&~6u))return "Foreign continuous actor hierarchy or matrix override";
        for(unsigned k=0;k<3;++k)if(fabsf(node->pos[k])>1000000)return "Continuous actor node exceeds native range";
    }
    unsigned neighbor=frame+1<motion->frm_num?frame+1:0;
    for(unsigned i=0;i<motion->obj_num;++i)if(motion->md2P[i].p[0])for(unsigned n=0;n<2;++n){const float *key=(const float*)motion->md2P[i].p[0]+(n?neighbor:frame)*3;
        for(unsigned k=0;k<3;++k)if(fabsf(key[k])>1000000)return "Continuous actor key exceeds native range";}
    if(a->mskp){const char *error=rx_face_control_tick_check(a);if(error)return error;}
    return NULL;
}
static BH_PWORK **enemy_task_selection;
static unsigned enemy_task_count;
static const char *enemy_task_fault;
const char *bhStepNativeEnemyController(BH_PWORK *a)
{
    unsigned i;
    for(i=0;i<enemy_task_count;++i)if(enemy_task_selection[i]==a)break;
    if(i==enemy_task_count)return enemy_task_fault="Enemy callback requires a preflighted native task";
    if(a->id>90&&a->mskp){enemy_task_fault=rx_face_control_tick(a);if(enemy_task_fault)return enemy_task_fault;}
    bhSubpl(a);
    return NULL;
}
const char *rx_engine_room_actors_tick(RxEngineRoom *r)
{
    if(!r||rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count||r->enemy_count>128)
        return "Enemy task requires its native room owner";
    const char *player_error=rx_player_control_check();if(player_error)return player_error;
    if(enemy_task_selection)return "Reentrant native enemy task";
    /* The source resets player contact offsets even when its task is paused. */
    if(!(sys->sp_flg&2)){bhControlEnemy();return NULL;}
    if((unsigned)sys->ewk_n>128)return "Enemy task extent exceeds its native pool";
    unsigned records[128];
    for(unsigned i=0;i<128;++i)records[i]=UINT_MAX;
    for(unsigned i=0;i<r->enemy_count;++i)if(rom->enep[i].flg&1){
        unsigned slot=rom->enep[i].wrk_no;
        if(slot>=128||records[slot]!=UINT_MAX)return "Duplicate or invalid native enemy task mapping";
        records[slot]=i;
    }
    BH_PWORK *selected[128];unsigned count=0,objects=0;
    /* Preflight in the source driver's pool order, including holes and dormant
       actors. Its linked-parent condition is evaluated before the active bit. */
    for(unsigned slot=0;slot<128;++slot){
        BH_PWORK *a=ene+slot;
        if(slot>=(unsigned)sys->ewk_n){if(a->flg&1)return "Active enemy lies beyond the original task extent";continue;}
        if(a->stflg&0x41000000)continue;
        if(a->flg&0x80)return "Original linked enemy task condition is not ported yet";
        if(!(a->flg&1))continue;
        if(records[slot]==UINT_MAX)return "Enemy task contains an unowned actor";
        const char *error=actor_tick_check(r,records[slot],a);if(error)return error;
        if(a->comb_timeout==INT_MIN)return "Original enemy combo timer would underflow";
        if((a->flg&0x80000000)&&++objects>sizeof(sys->en_obj)/sizeof(sys->en_obj[0]))
            return "Original enemy object list exceeds its native capacity";
        selected[count++]=a;
    }
    enemy_task_fault=NULL;enemy_task_selection=selected;enemy_task_count=count;
    bhControlEnemy();
    enemy_task_selection=NULL;enemy_task_count=0;
    return enemy_task_fault;
}


size_t rx_engine_room_actor_count(const RxEngineRoom *r){return r?r->enemy_count:0;}
NJS_TEXLIST *rx_engine_room_actor_textures(RxEngineRoom *r,size_t record)
{
    if(!r||record>=r->enemy_count||!r->enemies[record].face_pose)return NULL;
    return rx_face_pose_model(r->enemies[record].face_pose)->texP;
}
static int actor_visible(const BH_PWORK *a)
{
    return (a->flg&1)&&!(a->flg&0x80000000)&&!(a->stflg&0x41000000)&&
        ((sys->st_flg&0x20)||!(a->mdflg&0x200))&&!(a->mdflg&1);
}
static const char *actor_skin_check(RxEngineRoom *r,size_t i,BH_PWORK **out)
{
    *out=NULL;
    if(!r||rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count||i>=r->enemy_count)
        return "Actor skin publication requires its native room owner";
    if(!(rom->enep[i].flg&1))return NULL;
    if(rom->enep[i].wrk_no>=128)return "Actor skin work mapping exceeds native pool";
    BH_PWORK *a=ene+rom->enep[i].wrk_no;if(!actor_visible(a))return NULL;
    EngineEnemy *owned=r->enemies+i;
    if(a->id!=rom->enep[i].id||!owned->face_pose||a->mlwP!=a->mdl||a->mlwP->texP!=rx_face_pose_model(owned->face_pose)->texP||
       a->draw_tp||a->mdl_no||!(a->mdflg&0x100)||!(a->mdflg&0x20)||(a->mdflg&0x406)||
       (a->flg&0x80)||(sys->st_flg&0x100)||(sys->gm_flg&0x10000c0))
        return "Original actor draw/morph/culling/mirror mode is not ported yet";
    const char *error=rx_face_pose_skin_check(owned->face_pose,a);if(error)return error;
    *out=a;return NULL;
}
const char *rx_engine_room_actors_publish(RxEngineRoom *r)
{
    if(!r||rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count)return "Actor skin publication requires its native room owner";
    BH_PWORK *selected[128];unsigned count=0;
    for(size_t i=0;i<r->enemy_count;++i){BH_PWORK *a;const char *error=actor_skin_check(r,i,&a);if(error)return error;
        if(a){if(count==128)return "Actor skin batch exceeds native pool";
            for(unsigned j=0;j<count;++j)if(selected[j]==a)return "Duplicate actor skin work mapping";selected[count++]=a;}}
    for(size_t i=0;i<r->enemy_count;++i){BH_PWORK *a;actor_skin_check(r,i,&a);
        if(a)rx_face_pose_skin(r->enemies[i].face_pose,a);}
    return NULL;
}
const char *rx_engine_room_actor_draw(RxEngineRoom *r,size_t record,const NJS_MATRIX *view_matrix,
                                    const NJS_CNK_VIEW *view,RxRoomSubmit submit,void *context)
{
    BH_PWORK *a;const char *error=actor_skin_check(r,record,&a);if(error||!a)return error;
    return rx_face_pose_draw(r->enemies[record].face_pose,view_matrix,view,submit,context);
}

static int owned_enemy_bank(const RxEngineRoom *r,unsigned id)
{
    if(id>=sizeof(sys->emtp)/sizeof(*sys->emtp))return 0;
    MN_WORK *bank=sys->emtp[id];
    if(!bank)return 0;
    if(r->empty_motion[id]&&bank==r->empty_motion[id])return 1;
    for(size_t i=0;i<r->enemy_count;++i)
        if((r->rom.enep[i].flg&1)&&r->rom.enep[i].id==id&&r->enemies[i].motion.motions==bank)return 1;
    return 0;
}
const char *rx_engine_room_sub_control_check(RxEngineRoom *r,unsigned sub,BH_PWORK *actor)
{
    if(sub!=128&&sub!=144&&sub!=149)return "Original actor control mode is not ported yet";
    if(!r||rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count)return "Actor control requires its owned native room mapping";
    if(!actor){
        for(size_t i=0;i<r->enemy_count;++i)if(rom->enep[i].flg&1){
            if(rom->enep[i].wrk_no>=128||ene[rom->enep[i].wrk_no].id!=rom->enep[i].id)return "Invalid native actor control mapping";
            if(sub==128&&!owned_enemy_bank(r,rom->enep[i].id))return "Original actor handoff requires its owned native enemy bank";
        }
        return NULL;
    }
    size_t record;
    for(record=0;record<r->enemy_count;++record)
        if((rom->enep[record].flg&1)&&rom->enep[record].wrk_no<128&&actor==&ene[rom->enep[record].wrk_no])break;
    if(record==r->enemy_count||actor->id!=rom->enep[record].id)return "Original actor control has a foreign task target";
    if(sub==128&&!owned_enemy_bank(r,actor->id))return "Original actor handoff references a foreign native enemy bank";
    return NULL;
}


const char *rx_engine_room_event_sound_check(RxEngineRoom *r,unsigned selector,unsigned record,unsigned joint)
{
    if(!r)return "EasySESet requires its native room owner";
    BH_PWORK *actor;const ML_WORK *model;O_WORK *work;
    if(selector==1){
        if(rom->enep!=r->rom.enep||rom->ene_n!=(int)r->enemy_count||record>=r->enemy_count||!(rom->enep[record].flg&1)||rom->enep[record].wrk_no>=128)return "EasySESet references an absent native actor";
        actor=&ene[rom->enep[record].wrk_no];EngineEnemy *owned=r->enemies+record;if(!owned->part_count||actor->id!=rom->enep[record].id)return "Foreign EasySESet actor mapping";
        model=owned->face_pose?rx_face_pose_model(owned->face_pose):&owned->models[0].part.model;work=owned->script_work;
    }else if(selector==2||selector==3){
        int item=selector==3;O_WRK *base=item?r->sound_items:r->sound_objects;size_t count=item?r->item_count:r->prop_count;
        if((item?sys->itwp:sys->obwp)!=base||(item?rom->itm_n:rom->obj_n)!=(int)count||record>=count||(!item&&record<4)||!base)return "EasySESet references a foreign object/item pool";
        actor=(BH_PWORK *)(base+record);model=&(item?r->items:r->props)[record].part.model;work=(item?r->sound_item_work:r->sound_prop_work)[record];
    }else return "EasySESet source family is not ported yet";
    if(!work||actor->mlwP!=actor->mdl||actor->mlwP->objP!=model->objP||actor->mlwP->owP!=work||actor->mlwP->obj_num!=model->obj_num||joint>=model->obj_num)return "EasySESet references foreign native source matrices";
    for(unsigned i=12;i<15;++i)if(!isfinite(work[joint].mtx[i]))return "Nonfinite EasySESet joint position";
    return NULL;
}


const char *rx_engine_room_message_data(RxEngineRoom *r,const unsigned char **data,size_t *bytes,unsigned *count)
{
    if(!r||!r->messages||rom->mesp!=(unsigned int *)r->messages||rom->mes_n!=(int)r->message_count)
        return "Message requires its owned native room bank";
    *data=r->messages;*bytes=r->message_bytes;*count=r->message_count;return NULL;
}

const char *rx_engine_player_tick_check(RxEnginePlayer *p,RxEngineWeapon *weapon,BH_PWORK *a)
{
    const char *error=rx_engine_player_script_motion_check(p,weapon,a,a->mtn_no,3,1);if(error)return error;
    if(a->mlwP->obj_num<22||a->mnwP!=p->native_motion.motions||a->mtn_tp||(a->mtn_md&~0x28u)||a->frm_no<0||a->hokan_count<0||a->hokan_count>8||a->hokan_rate>65536||a->ax||a->az)
        return "Unported original continuous player motion/blend/root state";
    const MN_WORK *m=a->mnwP+a->mtn_no;unsigned frame=(unsigned)a->frm_no>>16;
    int64_t period=(int64_t)m->frm_num*65536,next=(int64_t)a->frm_no+(int)a->mtn_add;
    if(!m->md2P[0].p[0]||frame>=m->frm_num||(int)a->mtn_add>period||(int)a->mtn_add< -period||next>INT32_MAX||next<INT32_MIN)
        return "Original scripted player frame/step exceeds native range";
    const float root[]={a->px,a->py,a->pz};for(unsigned k=0;k<3;++k)if(fabsf(root[k])>1000000)return "Original scripted player position exceeds native range";
    unsigned neighbor=frame+1<m->frm_num?frame+1:0;
    for(unsigned i=0;i<m->obj_num;++i){if(a->mlwP->owP[i].flg&~2u)return "Unported scripted player matrix override/protection";
        for(unsigned k=0;k<3;++k)if(fabsf(a->mlwP->objP[i].pos[k])>1000000)return "Original scripted player node exceeds native range";
        if(m->md2P[i].p[0])for(unsigned n=0;n<2;++n)for(unsigned k=0;k<3;++k){float key=((float*)m->md2P[i].p[0])[(n?neighbor:frame)*3+k];if(!isfinite(key)||fabsf(key)>1000000)return "Invalid original scripted player translation key";}}
    return NULL;
}
int rx_engine_player_world_pose(const RxEnginePlayer *p){return p&&p->world_pose;}
const char *rx_engine_player_publish(RxEnginePlayer *p,BH_PWORK *a)
{
    const char *error=rx_engine_player_tick_check(p,NULL,a);if(error)return error;
    /* Original npInitCalcSkin removes actor translation, retaining world
       rotation. Skinned owners draw with view plus actor translation only. */
    const float origin[]={a->px,a->py,a->pz};
    for(unsigned i=0;i<p->source.node_count;++i){memcpy(p->script_matrices[i],a->mlwP->owP[i].mtx,sizeof(NJS_MATRIX));
        for(unsigned k=0;k<3;++k)p->script_matrices[i][12+k]-=origin[k];}
    p->world_pose=1;return player_skin(p,p->script_matrices);
}

const char *rx_engine_room_player_floor_check(RxEngineRoom *r)
{
    if(!r||rom->flrp!=r->rom.flrp||rom->flr_n!=r->rom.flr_n||sys->mflr_n||rom->walp!=r->rom.walp||rom->wal_n!=r->rom.wal_n||sys->mwal_n)
        return "Scripted player footsteps require owned static room floors";
    for(int i=0;i<rom->flr_n;++i){const ATR_WORK *f=rom->flrp+i;
        if(f->type==1&&f->prm0>=5)return "Unported original player floor sound";
        const float v[]={f->px,f->pz,f->w,f->d};for(unsigned k=0;k<4;++k)if(!isfinite(v[k])||fabsf(v[k])>1000000)return "Invalid native player floor geometry";}
    return NULL;
}

const char *rx_engine_room_player_walk_walls_check(RxEngineRoom *r)
{
    const char *error=rx_engine_room_player_floor_check(r);if(error)return error;
    if(!isfinite(rom->h)||fabsf(rom->h)>1000000)return "Invalid native walking room height";
    for(int i=0;i<rom->wal_n;++i){const ATR_WORK *wall=rom->walp+i;if(!(wall->flg&1))continue;
        const float values[]={wall->px,wall->py,wall->pz,wall->w,wall->h,wall->d};
        for(unsigned k=0;k<6;++k)if(!isfinite(values[k])||fabsf(values[k])>1000000)return "Invalid native walking wall geometry";
        if(wall->type>7||wall->type==6)return "Original walking wall family is not ported yet";
        if((wall->type==2||wall->type==3)&&(wall->attr&0x40))return "Original wall hit-reaction task is not ported yet";
        if((wall->type==4||wall->type==5)&&!wall->w&&!wall->d)return "Degenerate native walking wall line";
    }
    return NULL;
}

const char *rx_engine_room_player_spawn_check(RxEngineRoom *r)
{
    if(!r||rom->posp!=r->rom.posp||rom->pos_n!=r->rom.pos_n)return "Player placement requires its owned native spawn table";
    if((unsigned)sys->ply_id>=4)return "Invalid original spawn character";
    for(unsigned i=0;i<32;++i)if(!isfinite(rom->grand[i]))return "Nonfinite original spawn floor height";
    const float *position=NULL;
    if(sys->cb_flg&0x800000){
        if(sys->flr_no< -2||sys->flr_no>29||sys->ply_wno[(unsigned)sys->ply_id]<0||sys->ply_wno[(unsigned)sys->ply_id]>20)return "Invalid original saved player placement";
        position=&sys->ply_pos.x;
    }else if(rom->pos_n){
        if((unsigned)sys->pos_no>=(unsigned)rom->pos_n)return "Original room spawn selector exceeds its owned table";
        position=&rom->posp[(unsigned)sys->pos_no].px;
    }
    if(position)for(unsigned i=0;i<3;++i)if(!isfinite(position[i])||fabsf(position[i])>1000000)return "Invalid original player spawn position";
    return NULL;
}
