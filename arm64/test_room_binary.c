#include "room_binary.h"
#include "room_engine.h"
#include "face_control_runtime.h"
#include "room_scripts.h"
#include "event.h"
#include "Motion.h"
#include "room.h"
#include "dread.h"
#include "ps2/guest_layout.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ROM_WORK *rom; // Original cut.c reads the active room through this global.
static SYS_WORK room_system;
SYS_WORK *sys = &room_system;
BH_PWORK ene[128];
BH_PWORK *plp;
O_WRK eff[512];
int bhCheckCutArea(float x, float z, char floor);
extern unsigned char *bhScePtr, *G_Sp;
extern int bhIfelFlg;
void check_texture_heap(const unsigned char *rdt, size_t bytes);
void check_room_matrix(const unsigned char *rdt, size_t bytes, FILE *reference);
void check_shadow_runtime(const char *path);
void check_room_effects_file(const char *path);
void check_particle_runtime(const char *path,const char *common_path);

static void check(int ok, const char *reason)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", reason); exit(1); }
}

static uint32_t word(const uint8_t *p)
{
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= (uint32_t)p[i] << (8 * i);
    return result;
}

static void put(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t hash(uint32_t h, const void *ptr, size_t size)
{
    const uint8_t *p = ptr;
    for (size_t i = 0; i < size; ++i) h = (h ^ p[i]) * UINT32_C(16777619);
    return h;
}

static unsigned compare_lights(const RxRoomLight *lights, uint32_t count,
                               RxRoomSpan span, FILE *reference)
{
    check((lights != NULL) == (count != 0), "wrong native light array");
    unsigned discarded = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const RxRoomLight *light = &lights[i];
        check(!light->linked_work && !light->extension && !light->render_light,
              "disc addresses leaked into native light work");
        const uint8_t *raw = span.data + (size_t)i * RX_LIGHT_DISK_BYTES + 0xd4;
        for (size_t k = 0; k < 3; ++k) discarded += word(raw + k * 4) != 0;
        uint32_t expected;
        uint32_t h = hash(UINT32_C(2166136261), &light->values, sizeof(light->values));
        check(fscanf(reference, "%" SCNu32, &expected) == 1 && h == expected,
              "decoded light scalars differ from reference");
    }
    return discarded;
}

static void compare_header(const RxRoomHeader *header, const uint8_t *rdt, FILE *reference)
{
    uint32_t h = UINT32_C(2166136261), expected;
#define FIELD(name) h = hash(h, &header->name, sizeof(header->name))
    FIELD(dumps); FIELD(counts); FIELD(dummy); FIELD(flags);
    FIELD(background_color); FIELD(fog_color); FIELD(fog_near); FIELD(fog_far);
    FIELD(extent); FIELD(ground); FIELD(fog); FIELD(ambient_modes);
    FIELD(ambient_r); FIELD(ambient_g); FIELD(ambient_b);
#undef FIELD
    check(fscanf(reference, "%" SCNu32, &expected) == 1 && h == expected,
          "normalized ROM scalar fields differ from reference");
    for (size_t i = 0; i < RX_ROOM_TABLES; ++i) {
        RxRoomSpan span = header->tables[i];
        size_t length;
        check(fscanf(reference, "%" SCNu32 " %zu", &expected, &length) == 2 &&
              span.offset == expected && span.bytes == length &&
              span.data == (span.bytes ? rdt + expected : NULL), "wrong native ROM table reference");
    }
    for (size_t i = 0; i < header->counts[0]; ++i) {
        const RxRoomCut *cut = &header->cuts[i];
        uint8_t fields[] = {cut->flags, cut->type, (uint8_t)cut->floor, cut->area_count};
        h = hash(UINT32_C(2166136261), fields, sizeof(fields));
        h = hash(h, cut->center, sizeof(cut->center));
        h = hash(h, cut->extent, sizeof(cut->extent));
        h = hash(h, cut->cameras, sizeof(cut->cameras));
        h = hash(h, cut->extra, sizeof(cut->extra));
        h = hash(h, cut->areas, cut->area_count * sizeof(*cut->areas));
        check(fscanf(reference, "%" SCNu32, &expected) == 1 && h == expected,
              "decoded cut/camera/area fields differ from reference");
    }
    unsigned stale_lights = compare_lights(header->lights, header->counts[1],
                                            header->tables[RX_LIGHTS], reference);
    stale_lights += compare_lights(header->event_lights, header->counts[13],
                                   header->tables[RX_EVENT_LIGHTS], reference);
    unsigned stale_camera_pointers = 0;
    check((header->event_cameras != NULL) == (header->counts[11] != 0), "wrong native camera array");
    for (uint32_t i = 0; i < header->counts[11]; ++i) {
        const RxEventCamera *camera = &header->event_cameras[i];
        h = hash(UINT32_C(2166136261), &camera->flags, sizeof(camera->flags));
        h = hash(h, &camera->type, sizeof(camera->type));
        h = hash(h, &camera->next, sizeof(camera->next));
        h = hash(h, &camera->key_count, sizeof(camera->key_count));
        for (size_t k = 0; k < RX_CAMERA_KEYS; ++k) {
            const RxEventCameraKey *key = &camera->keys[k];
            check(!key->record, "disc recp address leaked into native camera");
            const uint8_t *raw = header->tables[RX_EVENT_CAMERAS].data +
                (size_t)i * RX_EVENT_CAMERA_DISK_BYTES + 8 + k * RX_CAMERA_KEY_DISK_BYTES + 0x7c;
            stale_camera_pointers += word(raw) != 0;
            h = hash(h, &key->values, sizeof(key->values));
        }
        check(fscanf(reference, "%" SCNu32, &expected) == 1 && h == expected,
              "decoded event-camera fields differ from reference");
    }
    printf("PASS %u room lights, %u event lights, %u event cameras; ignored %u light and %u camera disc addresses\n",
           header->counts[1], header->counts[13], header->counts[11], stale_lights, stale_camera_pointers);
    RxRoomSpan events = header->tables[RX_EVENTS];
    unsigned char *copy = malloc(events.bytes);
    check(copy && events.bytes, "missing scenario fixture");
    memcpy(copy, events.data, events.bytes);
    RxRoomScripts scripts = {0};
    const char *error = rx_room_scripts_open(copy, events.bytes, &scripts);
    check(!error, error);
    size_t slots;
    check(fscanf(reference, "%zu", &slots) == 1 && slots == scripts.slots && slots >= RX_SCRIPT_FIRST_EVENT,
          "scenario slot count differs from disc reference");
    ROM_WORK script_room = {.evtp = &scripts};
    rom = &script_room;
    for (size_t i = 0; i < slots; ++i) {
        check(fscanf(reference, "%" SCNu32, &expected) == 1, "missing scenario offset");
        unsigned char *address = expected ? copy + expected : NULL;
        check(rx_room_script(&scripts, i) == address, "scenario offset differs from disc reference");
        if (i >= RX_SCRIPT_FIRST_EVENT) {
            BH_SCEWORK task = {0};
            check(Event_init(&task, (unsigned int)(i - RX_SCRIPT_FIRST_EVENT)) == !!address &&
                  task.data == address && task.status == !!address, "original Event_init chose the wrong entry");
        }
    }
    BH_SCEWORK task = {.status = 3, .data = copy};
    check(!Event_init(&task, (unsigned int)(slots - RX_SCRIPT_FIRST_EVENT)) &&
          !Event_init(&task, UINT32_MAX) && task.status == 3 && task.data == copy,
          "original Event_init overwrote a task for an out-of-range ID");
    rom = NULL;
    // A branch starts at an odd address and retains the high half of its pointer.
    unsigned char *branch = copy + 1;
    branch[1] = 8;
    bhScePtr = branch;
    check((uintptr_t)branch > UINT32_MAX && bhIfelCk() && bhIfelFlg == 1 &&
          bhScePtr == branch + 2 && G_Sp == branch + 14 &&
          bhEndif() && bhIfelFlg == 0 && G_Sp == branch + 10,
          "original branch address lost its ARM64 pointer");
    bhScePtr = G_Sp = NULL;
    free(copy);
    printf("PASS %zu original scenario offsets (%zu numbered events)\n", slots, slots - RX_SCRIPT_FIRST_EVENT);
}

static size_t compare_model_package(const RxEngineModelPart *part, const uint8_t *shadow, FILE *ref)
{
    size_t offset, skin_bytes;
    unsigned nodes, has_tex, textures;
    uint32_t skin_hash;
    check(fscanf(ref, "%zu %u %u %u %zu %" SCNu32, &offset, &nodes, &has_tex,
                 &textures, &skin_bytes, &skin_hash) == 6, "missing actor model reference");
    check(part && part->model.datP == shadow + offset && part->model.obj_num == nodes &&
          (part->model.objP != NULL) == (nodes != 0) &&
          (part->model.texP != NULL) == (has_tex != 0) &&
          (!has_tex || part->model.texP->nbTexture == textures), "actor MDL work differs");
    check(part->skin_bytes == skin_bytes && (part->skin != NULL) == (skin_bytes != 0) &&
          hash(UINT32_C(2166136261), part->skin, skin_bytes) == skin_hash, "actor skin payload differs");
    if (!skin_bytes) return 0;
    uint32_t flags_hash;
    unsigned groups;
    check(fscanf(ref, "%" SCNu32 " %u", &flags_hash, &groups) == 2, "missing converted skin reference");
    uint32_t flags = UINT32_C(2166136261);
    for (unsigned i = 0; i < nodes; ++i)
        flags = hash(flags, &part->model.objP[i].evalflags, sizeof(uint32_t));
    check(flags == flags_hash, "converted skin owner flags differ");
    size_t converted = 0;
    for (unsigned i = 0; i < groups; ++i) {
        unsigned owner, count;
        uint32_t expected;
        check(fscanf(ref, "%u %u %" SCNu32, &owner, &count, &expected) == 3 && owner < nodes,
              "missing converted skin vertex reference");
        const NJS_CNK_MODEL *model = part->model.objP[owner].model;
        check(model && model->vlist &&
              hash(UINT32_C(2166136261), model->vlist, 64 + (size_t)count * 32) == expected,
              "converted skin vertices differ from original algorithm");
        converted += count;
    }
    return converted;
}

static void check_engine_handoff(uint8_t *data, size_t bytes, uint8_t *scratch)
{
    static ROM_WORK active_rom;
    static O_WRK props[32], items[32];
    rom = &active_rom;
    sys->obwp = props;
    sys->itwp = items;
    props[0].id = 777; // The engine reserves prop slots 0 through 3.
    sys->rdtp = data;
    sys->rdtsz = (unsigned)bytes;
    const char *error = bhStageNativeRoom();
    check(!error, error);
    bhBindNativeRoomMotions();
    check(props[0].id == 777 && sys->rmthp && sys->ewk_n == rom->ene_n,
          "native room handoff lost reserved prop or room motion work");
    size_t model_directory = word(data + 20);
    const uint8_t *shadow = (const uint8_t *)rom->mdl.datP - word(data + model_directory);
    check((uintptr_t)shadow > UINT32_MAX && shadow != data, "original room retained an arena pointer");
    size_t texture_directory = word(data + 32);
    size_t textures = word(data + texture_directory);
    check(sys->txr_n > 0 && (size_t)sys->txr_n <= textures && sys->txlp[0] == rom->mdl.texP,
          "room texture queue missing root");
    for (int i = 0; i < sys->txr_n; ++i) {
        size_t offset = word(data + texture_directory + (i + 1) * 4);
        check(sys->txdp[i] == shadow + offset && sys->txlp[i] && sys->txloff[i] == 0,
              "model texture queue differs from disc order");
    }
    for (int i = 0; i < rom->ene_n; ++i) {
        check(ene[i].id == rom->enep[i].id && ene[i].px == rom->enep[i].px && ene[i].mdl_n &&
              ene[i].mnwP && ene[i].mnwP == ene[i].mnwPb, "original enemy did not bind native work");
        if (i && ene[i].id == ene[0].id) {
            check(ene[i].mnwP == ene[0].mnwP, "duplicate enemies did not reuse the motion bank");
            if (ene[i].mdlver == ene[0].mdlver)
                check(ene[i].mdl[0].texP == ene[0].mdl[0].texP &&
                      !(ene[i].mdl[0].flg & 0x200), "duplicate enemies did not reuse textures");
            else
                check(ene[i].mdl[0].texP != ene[0].mdl[0].texP,
                      "different enemy model versions unexpectedly shared textures");
        }
    }
    for (int i = 4; i < rom->obj_n; ++i)
        check(props[i].id == rom->objp[i].id && props[i].px == rom->objp[i].px &&
              props[i].mlwP == props[i].mdl, "original prop creation differs");
    for (int i = 0; i < rom->itm_n; ++i)
        check(items[i].id == rom->itmp[i].id && items[i].pz == rom->itmp[i].pz &&
              items[i].mlwP == items[i].mdl, "original item creation differs");
    size_t motion_directory = word(data + 24);
    size_t mouth = word(data + motion_directory + (rom->ene_n + 1) * 4);
    size_t lip = word(data + motion_directory + (rom->ene_n + 2) * 4);
    check(sys->mspp == (mouth ? shadow + mouth : NULL) && sys->lspp == (lip ? shadow + lip : NULL),
          "original facial data pointers differ");
    unsigned char arena[2 * sizeof(O_WORK) + 16];
    ML_WORK model = {.obj_num = 2};
    unsigned char *end = bhKeepObjWork(&model, arena + 1);
    check((uintptr_t)model.owP > UINT32_MAX && (uintptr_t)model.owP % 16 == 0 &&
          end == (unsigned char*)(model.owP + 2), "original object-work allocation truncated address");

    memcpy(scratch, data, bytes);
    put(scratch + texture_directory + 4, (uint32_t)texture_directory);
    sys->rdtp = scratch;
    MN_WORK *active = sys->rmthp;
    check(bhStageNativeRoom() && sys->rmthp == active && rom->mdl.objP && ene[0].mnwP,
          "bad texture directory replaced the active engine room");
    memcpy(scratch, data, bytes);
    put(scratch + word(data + texture_directory + 4) + 4, UINT32_MAX);
    error = bhStageNativeRoom();
    check(error && !strcmp(error, "room texture packet exceeds its block") && sys->rmthp == active,
          "out-of-range texture packet replaced the active engine room");
    memcpy(scratch, data, bytes);
    put(scratch + word(data + texture_directory + 4) + 32 + 0x80, UINT32_MAX);
    error = bhStageNativeRoom();
    check(error && !strcmp(error, "room texture packet exceeds its block") && sys->rmthp == active,
          "out-of-range TIM2 picture replaced the active engine room");
    memcpy(scratch, data, bytes);
    bhInitRoom();
    check(!rom->mdl.objP && !sys->rmthp && !sys->txr_n && !sys->txlp[0] && !ene[0].mnwP &&
          !items[0].mlwP && !props[4].mlwP && props[0].id == 777,
          "room teardown retained native work or erased reserved props");
    rom = NULL;
    puts("PASS original actor creation, room texture queue, shared motion banks, and teardown");
}

void rx_check_face_pose(RxEngineRoom *room,unsigned index);
void rx_check_face_aux(RxEngineRoom *room,unsigned index,const unsigned char *rdt,size_t bytes,const unsigned char *backing);
void rx_check_face_mesh(RxEngineRoom *room,unsigned index,const unsigned char *rdt,size_t bytes);
static void compare_actor_packages(const uint8_t *data, size_t bytes, FILE *ref)
{
    RxEngineRoom *room = NULL;
    const char *error = rx_engine_room_load(data, bytes, &room);
    check(!error, error);
    ROM_WORK *work = rx_engine_room_rom(room);
    const uint8_t *shadow = (const uint8_t *)work->mdl.datP - word(data + word(data + 20));
    check((uintptr_t)shadow > UINT32_MAX && shadow != data, "actor assets need independent high memory");
    size_t model_parts = 0, populated = 0, empty = 0, sampled = 0, skinned = 0;
    for (int i = 0; i < work->ene_n; ++i) {
        size_t control_bytes;int32_t *controls=rx_engine_enemy_face_control(room,(unsigned)i,&control_bytes);
        check((controls!=NULL)==(work->enep[i].id>90)&&control_bytes==(controls?RX_FACE_CONTROL_BYTES:0),"facial scalar control allocation differs from original actor family");
        if(controls) {
            check((uintptr_t)controls>UINT32_MAX,"facial control storage needs native high addresses");
            for(unsigned word=0;word<RX_FACE_CONTROL_WORDS;++word)check(!controls[word],"facial control staging must preserve original cleared allocation");
        }
        size_t parts, mask_bytes, actual_mask;
        uint32_t mask_hash;
        check(fscanf(ref, "%zu %zu %" SCNu32, &parts, &mask_bytes, &mask_hash) == 3,
              "missing enemy package reference");
        for (size_t j = 0; j < parts; ++j)
            skinned += compare_model_package(rx_engine_enemy_model(room, i, j), shadow, ref);
        check(!rx_engine_enemy_model(room, i, parts), "extra enemy model part");
        model_parts += parts;
        const uint8_t *mask = rx_engine_enemy_mask(room, i, &actual_mask);
        check((mask != NULL) == (mask_bytes != 0) && actual_mask == mask_bytes &&
              hash(UINT32_C(2166136261), mask, actual_mask) == mask_hash, "enemy mask payload differs");
        /* The actual geometry remains detached until jaw/eye/tongue context
           migration is complete; exercise its native muscle stage without publication. */
        if(word(data+word(data+24)+12)==1598488){rx_check_face_mesh(room,(unsigned)i,data,bytes);rx_check_face_aux(room,(unsigned)i,data,bytes,shadow);rx_check_face_pose(room,(unsigned)i);}
        int slots;
        check(fscanf(ref, "%d", &slots) == 1 && slots >= -1 && slots <= RX_ROOM_MOTION_SLOTS,
              "missing enemy motion bank reference");
        MN_WORK *bank = rx_engine_enemy_motions(room, i);
        check((bank != NULL) == (slots >= 0), "enemy motion bank presence differs");
        for (int j = 0; bank && j < RX_ROOM_MOTION_SLOTS; ++j) {
            const MN_WORK *motion = &bank[j];
            size_t offset = 0;
            unsigned nodes = 0, frames = 0;
            uint32_t expected_hash = 0;
            if (j < slots)
                check(fscanf(ref, "%zu %u %u %" SCNu32, &offset, &nodes, &frames, &expected_hash) == 4,
                      "missing enemy motion slot reference");
            if (!offset) {
                check(!motion->datP && !motion->md2P && !motion->frm_num, "enemy motion gap shifted");
                empty += j < slots;
                continue;
            }
            ++populated;
            check(motion->datP == shadow + offset && motion->obj_num == nodes &&
                  motion->frm_num == frames && frames && motion->md2P, "enemy motion work differs");
            uint32_t h = UINT32_C(2166136261);
            int sampled_clip = 0;
            for (size_t k = 0; k < nodes; ++k) {
                const float *pos = motion->md2P[k].p[0];
                const unsigned short *angle = motion->md2P[k].p[1];
                if (pos) h = hash(h, pos + (frames - 1) * 3, 12);
                if (angle) h = hash(h, angle + (frames - 1) * 3, 6);
                if (!sampled_clip && pos && angle && k < 64 && frames <= 32768) {
                    BH_PWORK actor = {.mnwP = bank, .mtn_no = j, .frm_no = (int)((frames - 1) * 65536)};
                    float translation[3];
                    int rotation[3];
                    bhGetObjMotion(&actor, (int)k, translation, rotation);
                    for (size_t axis = 0; axis < 3; ++axis) {
                        size_t key = (frames - 1) * 3 + axis;
                        if (translation[axis] != pos[key] || rotation[axis] != angle[key])
                            fprintf(stderr, "enemy %d clip %d frame %u joint %zu axis %zu: %a/%d expected %a/%d\n",
                                    i, j, frames - 1, k, axis, translation[axis], rotation[axis],
                                    pos[key], (int)angle[key]);
                        check(translation[axis] == pos[key] && rotation[axis] == angle[key],
                              "original enemy motion reader chose the wrong key");
                    }
                    sampled_clip = 1;
                    ++sampled;
                }
            }
            check(h == expected_hash, "last enemy motion keys differ");
        }
    }
    check(!rx_engine_enemy_model(room, work->ene_n, 0) &&
          !rx_engine_enemy_motions(room, work->ene_n), "out-of-range enemy index accepted");
    for (int i = 0; i < work->obj_n && i < RX_FIRST_ROOM_PROP; ++i)
        check(!rx_engine_prop_model(room, i), "reserved prop slot consumed a room MDL");
    for (int i = RX_FIRST_ROOM_PROP; i < work->obj_n; ++i)
        skinned += compare_model_package(rx_engine_prop_model(room, i), shadow, ref);
    for (int i = 0; i < work->itm_n; ++i)
        skinned += compare_model_package(rx_engine_item_model(room, i), shadow, ref);
    check(!rx_engine_prop_model(room, work->obj_n) &&
          !rx_engine_item_model(room, work->itm_n), "out-of-range prop or item index accepted");
    char trailing;
    check(fscanf(ref, " %c", &trailing) == EOF && !ferror(ref), "unexpected actor reference rows");
    check(!populated || sampled, "fixture did not exercise the original motion reader");
    printf("PASS %zu enemy model parts, %d props, %d items and %zu/%zu populated enemy motion slots; "
           "%zu clips sampled by original bhGetObjMotion; %zu skin vertices compared\n",
           model_parts, work->obj_n > RX_FIRST_ROOM_PROP ? work->obj_n - RX_FIRST_ROOM_PROP : 0,
           work->itm_n, populated, populated + empty, sampled, skinned);
    rx_engine_room_destroy(room);
}

static void compare_engine_room(const RxRoomBinary *decoded, const uint8_t *data,
                                size_t bytes, uint8_t *scratch)
{
    RxEngineRoom *engine_room = NULL;
    const char *error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(!error, error);
    check(!memcmp(data, scratch, bytes), "engine load mutated caller buffer");
    memset(scratch, 0, bytes); // No engine views may borrow the caller's bytes.
    rom = rx_engine_room_rom(engine_room);
    const RxRoomHeader *header = &decoded->header;
    check(rom->cut_n == (int)header->counts[0] && rom->lgt_n == (int)header->counts[1] &&
          rom->evc_n == (int)header->counts[11] && rom->evl_n == (int)header->counts[13] &&
          rom->fog_col == header->fog_color && rom->grand[31] == 0 &&
          !memcmp(rom->fog, header->fog, sizeof(rom->fog)), "engine ROM scalars differ");
    for (int i = 0; i < rom->cut_n; ++i) {
        CUT_WORK *cut = &rom->cutp[i];
        const RxRoomCut *ref = &header->cuts[i];
        check(cut->ctab_n == ref->area_count &&
              !memcmp(cut->cam, ref->cameras, sizeof(cut->cam)) &&
              (!cut->ctab_n || !memcmp(cut->cuttp, ref->areas, cut->ctab_n * sizeof(*cut->cuttp))),
              "engine camera cut differs");
    }
    check(bhCheckCutArea(17, 20, 0) == 0 && bhCheckCutArea(17, 50, 0) == 1 &&
          bhCheckCutArea(46, 50, 0) == 2 && bhCheckCutArea(17, 50, 1) == -1 &&
          bhCheckCutArea(-1, 20, 0) == -1, "original camera area selection differs");
    rom->cutp[1].flg = 0;
    check(bhCheckCutArea(17, 50, 0) == -1 && header->cuts[1].flags == 1,
          "original camera lookup cannot observe native mutation");
    rom->cutp[1].flg = 1;
    for (int i = 0; i < rom->evc_n; ++i) {
        const EVC_WORK *cam = &rom->evcp[i];
        check(cam->keyf_n == header->event_cameras[i].key_count &&
              cam->nxt_no == header->event_cameras[i].next, "engine event camera header differs");
        for (int j = 0; j < RX_CAMERA_KEYS; ++j) {
            const RxEventCameraKeyValues *values = &header->event_cameras[i].keys[j].values;
            check(!cam->keyf[j].recp && !memcmp(&cam->keyf[j], values, sizeof(*values)),
                  "engine event camera key differs");
        }
    }
    for (int i = 0; i < rom->evl_n; ++i) {
        const RxLightValues *values = &header->event_lights[i].values;
        check(!rom->evlp[i].lkwkp && !rom->evlp[i].exp && !rom->evlp[i].light &&
              !memcmp(&rom->evlp[i], values, sizeof(*values)), "engine event light differs");
    }
    const RxRoomScripts *scripts = rom->evtp;
    check(scripts && !rom->dmp00 && scripts->bytes == header->tables[RX_EVENTS].bytes &&
          scripts->bytes && !memcmp(scripts->data, header->tables[RX_EVENTS].data, scripts->bytes),
          "engine scenario span differs");
    BH_SCEWORK task = {0};
    unsigned char *entry = rx_room_script(scripts, RX_SCRIPT_FIRST_EVENT);
    check(Event_init(&task, 0) && task.status == 1 && task.data == entry &&
          (uintptr_t)task.data > UINT32_MAX && task.loop == -1,
          "original event initializer cannot use the room's native script pointer");
    check(Event_exec(0, 0) && !Event_exec(0, UINT32_MAX), "original event dispatch accepted an invalid ID");

    const RxModel *model = &decoded->root;
    const NJS_CNK_OBJECT *root = rom->mdl.objP;
    check(rom->mdl.flg == model->flags && rom->mdl.obj_num == model->node_count &&
          rom->mdl.datP && (uintptr_t)root > UINT32_MAX &&
          rom->mdl.texP && rom->mdl.texP->nbTexture == model->texture_count, "engine MDL header differs");
    for (size_t i = 0; i < model->node_count; ++i) {
        const RxNode *node = &model->nodes[i];
        const NJS_CNK_OBJECT *obj = &root[i];
        check(obj->evalflags == node->flags &&
              !memcmp(obj->pos, node->position, sizeof(obj->pos)) &&
              !memcmp(obj->ang, node->angle, sizeof(obj->ang)) &&
              !memcmp(obj->scl, node->scale, sizeof(obj->scl)) &&
              obj->child == (node->child ? &root[node->child - model->nodes] : NULL) &&
              obj->sibling == (node->sibling ? &root[node->sibling - model->nodes] : NULL),
              "engine MDL hierarchy or transform differs");
        check((obj->model != NULL) == (node->model != NULL), "engine MDL chunk missing");
        if (obj->model) {
            check(obj->model->r == node->model->radius &&
                  !memcmp(&obj->model->center, node->model->center, sizeof(obj->model->center)),
                  "engine chunk bounds differ");
            if (node->model->vertices)
                check((uintptr_t)obj->model->vlist > UINT32_MAX &&
                      !memcmp(obj->model->vlist, node->model->vertices, sizeof(Sint32)),
                      "engine chunk vertex stream differs");
            if (node->model->polygons)
                check(!memcmp(obj->model->plist, node->model->polygons, sizeof(Sint16)),
                      "engine chunk polygon stream differs");
        }
    }
    for (size_t i = 0; i < model->texture_count; ++i) {
        const NJS_TEXNAME *tex = &rom->mdl.texP->textures[i];
        check(!strcmp(tex->filename, model->textures[i].filename) &&
              tex->attr == model->textures[i].attributes && tex->texaddr == model->textures[i].texture,
              "engine texture reference differs");
    }
    MN_WORK *bank = rx_engine_room_motions(engine_room);
    for (size_t i = 0; i < RX_ROOM_MOTION_SLOTS; ++i) {
        const RxMotion *source = i < decoded->motion_slots ? &decoded->motions[i] : NULL;
        const MN_WORK *motion = &bank[i];
        if (!source || !source->source) {
            check(!motion->datP && !motion->md2P && !motion->frm_num, "engine empty motion slot differs");
            continue;
        }
        check(motion->obj_num == source->node_count && motion->frm_num == source->frame_count &&
              motion->flg == source->flags && (uintptr_t)motion->datP > UINT32_MAX &&
              (motion->atrP != NULL) == (source->attributes != NULL), "engine MTN header differs");
        for (uint32_t j = 0; j < motion->obj_num; ++j) {
            const RxTracks *track = &source->tracks[j];
            uint32_t last = motion->frm_num - 1;
            const float *pos = motion->md2P[j].p[0];
            const short *angle = motion->md2P[j].p[1];
            check((pos != NULL) == (track->position != NULL) &&
                  (angle != NULL) == (track->angle != NULL), "engine motion track presence differs");
            if (pos) check(!memcmp(&pos[last * 3], track->position + last * 12, 12),
                           "engine motion position keys differ");
            if (angle) check(!memcmp(&angle[last * 3], track->angle + last * 6, 6),
                             "engine motion angle keys differ");
        }
    }
    RxEngineRoom *active = engine_room;
    check(rx_engine_room_load(scratch, bytes, &engine_room) && engine_room == active &&
          rx_engine_room_rom(engine_room) == rom && bhCheckCutArea(17, 50, 0) == 1,
          "invalid engine room poisoned the active room");
    memcpy(scratch, data, bytes);
    size_t event_offset = header->tables[RX_EVENTS].offset;
    put(scratch + event_offset + RX_SCRIPT_FIRST_EVENT * 4, (uint32_t)scripts->bytes);
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && !strcmp(error, "room script offset outside bytecode") &&
          engine_room == active && rom->evtp == scripts,
          "out-of-range script offset replaced the active room");
    memcpy(scratch, data, bytes);
    put(scratch + event_offset + RX_SCRIPT_FIRST_EVENT * 4, RX_SCRIPT_FRAME * 4);
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && engine_room == active, "script offset into its own table accepted");
    memcpy(scratch, data, bytes);
    uint32_t position_table = header->tables[RX_POSITIONS].offset;
    check(header->tables[RX_POSITIONS].bytes > 0, "opening fixture needs a position table");
    put(scratch + header->sections[0] + RX_POSITIONS * 4, position_table + 1);
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && !strcmp(error, "unaligned ROM posp table") && engine_room == active &&
          rx_engine_room_rom(engine_room) == rom, "unaligned engine table replaced the active room");
    memcpy(scratch, data, bytes);
    size_t enemy_package = word(data + header->sections[1] + 4);
    put(scratch + enemy_package, UINT32_MAX - 1);
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && !strcmp(error, "asset block exceeds room bank") && engine_room == active &&
          rx_engine_enemy_model(engine_room, 0, 0), "malformed enemy replaced the active room");
    memcpy(scratch, data, bytes);
    size_t skin = decoded->enemies[0].parts[0].skin.offset;
    check(word(data + skin) == 0 && (word(data + skin + 4) & 2), "fixture lacks the expected skin owner");
    put(scratch + skin + 8, 128);
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && !strcmp(error, "skin group index outside engine table") && engine_room == active,
          "invalid skin group replaced the active room");
    memcpy(scratch, data, bytes);
    put(scratch + skin + 12, UINT32_MAX);
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && !strcmp(error, "skin weights exceed payload") && engine_room == active,
          "truncated skin weights replaced the active room");
    memcpy(scratch, data, bytes);
    const RxModel *skinned = &decoded->enemies[0].parts[0].model;
    size_t owner_stream = (size_t)(skinned->nodes[1].model->vertices - data);
    scratch[owner_stream + 6] = 0xff;
    scratch[owner_stream + 7] = 0xff;
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && !strcmp(error, "skin vertex count exceeds its stream or weight table") &&
          engine_room == active, "out-of-range vertex count replaced the active room");
    memcpy(scratch, data, bytes);
    // Wrap the first prop's MDL in a short SKIN record using its disc padding.
    size_t model_table = header->sections[1];
    size_t prop_start = word(data + model_table + 3 * 4);
    size_t prop_end = word(data + model_table + 4 * 4);
    check(header->counts[2] == 2 && prop_end - prop_start > 16 &&
          !memcmp(data + prop_end - 12, (uint8_t[12]){0}, 12), "prop fixture lacks wrapper space");
    memmove(scratch + prop_start + 12, scratch + prop_start, prop_end - prop_start - 12);
    memcpy(scratch + prop_start + 4, "SKIN", 4);
    put(scratch + prop_start + 8, 0);
    put(scratch + prop_start, (uint32_t)(prop_end - prop_start));
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(error && !strcmp(error, "prop skin outside model package") && engine_room == active,
          "out-of-range prop skin replaced the active room");
    put(scratch + prop_start, 4);
    task.data = scratch;
    put(scratch + event_offset + RX_SCRIPT_FIRST_EVENT * 4, 0);
    error = rx_engine_room_load(scratch, bytes, &engine_room);
    check(!error, error);
    rom = rx_engine_room_rom(engine_room);
    RxEngineModelPart *prop = rx_engine_prop_model(engine_room, RX_FIRST_ROOM_PROP);
    check(prop && prop->skin_bytes == 4 && prop->skin && *prop->skin == 0 &&
          prop->model.obj_num == decoded->props[RX_FIRST_ROOM_PROP].model.node_count,
          "wrapped prop did not retain its skin and model");
    check(bhCheckCutArea(46, 50, 0) == 2, "engine room replacement broke camera lookup");
    check(!Event_init(&task, 0) && task.data == scratch && Event_init(&task, 1),
          "sparse event slot shifted the next event or overwrote the task");
    rom = NULL;
    rx_engine_room_destroy(engine_room);
    puts("PASS native original ROM/ML/MN work, original camera lookups, and engine-room ownership");
}

int main(int argc, char **argv)
{
    if (argc==4&&!strcmp(argv[1],"--particles-only")) {
        check_particle_runtime(argv[2],argv[3]);return 0;
    }
    if (argc==3&&!strcmp(argv[1],"--room-effects-only")) {
        check_room_effects_file(argv[2]);
        return 0;
    }
    if (argc==3 && !strcmp(argv[1],"--effects-only")) {
        check_shadow_runtime(argv[2]);
        return 0;
    }
    if (argc != 4) return 2;
    int routes_only = !strcmp(argv[1], "--header-only");
    int lights_only = !strcmp(argv[1], "--lights-only");
    int header_only = routes_only || lights_only;
    FILE *file = fopen(argv[header_only ? 2 : 1], "rb");
    FILE *reference = header_only ? NULL : fopen(argv[2], "rb");
    FILE *rom_reference = fopen(argv[3], "rb");
    check(file && (header_only || reference) && rom_reference, "expanded RDT or reference missing");
    check(!fseek(file, 0, SEEK_END), "RDT seek failed");
    long end = ftell(file);
    check(end >= (long)sizeof(Ps2Rdt32) && end <= 64 * 1024 * 1024, "invalid RDT size");
    check(!fseek(file, 0, SEEK_SET), "RDT rewind failed");
    size_t bytes = (size_t)end;
    uint8_t *data = malloc(bytes), *other = malloc(bytes);
    check(data && other && fread(data, 1, bytes, file) == bytes, "RDT read failed");
    fclose(file);
    memcpy(other, data, bytes);
    if (header_only) {
        RxRoomHeader header = {0};
        const char *error = rx_decode_room_header(data, bytes, &header);
        check(!error, error);
        compare_header(&header, data, rom_reference);
        compare_actor_packages(data, bytes, rom_reference);
        check_engine_handoff(data, bytes, other);
        check(!memcmp(data, other, bytes), "room header decode changed source bytes");
        if (routes_only) {
            int multi_area = 0;
            for (size_t i = 0; i < header.counts[0]; ++i)
                if (header.cuts[i].area_count > 1) multi_area = 1;
            check(header.tables[RX_ROUTE_LINKS].bytes > 0 && multi_area,
                  "fixture lacks routes or multi-area cuts");
            printf("PASS RM_0140 ROM header, route links (%zu bytes) and %u camera cuts\n",
                   header.tables[RX_ROUTE_LINKS].bytes, header.counts[0]);
        } else {
            check(header.counts[1] > 0 && word(header.tables[RX_LIGHTS].data + 0xdc) != 0,
                  "RM_0130 fixture lacks a stale light address");
        }
        rx_room_header_destroy(&header);
        fclose(rom_reference);
        free(data);
        free(other);
        return 0;
    }

    RxRoomBinary work = {0};
    const char *error = rx_decode_room_binary(data, bytes, &work);
    check(!error, error);
    check(!memcmp(data, other, bytes), "room read changed source bytes");
    compare_header(&work.header, data, rom_reference);
    compare_actor_packages(data, bytes, rom_reference);
    check_engine_handoff(data, bytes, other);
    check_texture_heap(data, bytes);
    fclose(rom_reference);
    check(work.header.counts[13] && work.header.counts[11], "opening fixture lacks cinematic records");
    work.header.event_lights[0].values.flags ^= 2;
    work.header.event_cameras[0].keys[0].values.hidden_lights[0] ^= 1;
    check(!memcmp(data, other, bytes), "mutable light/camera work changed disc bytes");
    work.header.event_lights[0].values.flags ^= 2;
    work.header.event_cameras[0].keys[0].values.hidden_lights[0] ^= 1;
    check(work.header.cuts && work.header.cuts[0].area_count >= 1, "fixture has no camera areas");
    work.header.cuts[0].areas[0].attributes ^= 1;
    check(!memcmp(data, other, bytes), "native cut mutation changed disc bytes");
    work.header.cuts[0].areas[0].attributes ^= 1;
    printf("PASS original ROM scalar fields, %d table spans and %u native cuts match ISO reference\n",
           RX_ROOM_TABLES, work.header.counts[0]);
    check(work.root.node_count == 67 && work.root.texture_count == 32, "wrong RM_0000 root model");
    size_t slots;
    check(fscanf(reference, "%zu", &slots) == 1 && slots == work.motion_slots, "motion slot count differs");
    size_t populated = 0;
    for (size_t i = 0; i < slots; ++i) {
        uint32_t nodes, frames;
        check(fscanf(reference, "%" SCNu32 " %" SCNu32, &nodes, &frames) == 2 &&
              nodes == work.motions[i].node_count && frames == work.motions[i].frame_count,
              "room bank dimensions differ");
        if (!frames) continue;
        ++populated;
        for (uint32_t node = 0; node < nodes; ++node) {
            RxSample sample;
            error = rx_motion_sample(&work.motions[i], node, frames - 1, &sample);
            check(!error, error);
        }
    }
    check_room_matrix(data, bytes, reference);
    char trailing;
    check(fscanf(reference, " %c", &trailing) == EOF && !ferror(reference), "unexpected reference rows");
    fclose(reference);
    printf("PASS expanded RM_0000 root and original motion bank: %zu/%zu slots populated\n", populated, slots);
    compare_engine_room(&work, data, bytes, other);

    size_t table = word(data + 24), rom = word(data + 16);
    size_t room_slot = table + 4 * word(data + rom + offsetof(Ps2RoomHeader32, counts) + 8);
    size_t bank = word(data + room_slot), exit = bank;
    check(bank > room_slot && bank < bytes, "motion fixture is not suitable");
    for (size_t i = 0; i < slots; ++i) exit += 4 + word(data + exit);
    check(exit + 4 <= bytes && word(data + exit) == UINT32_MAX, "missing fixture marker");
    RxNode *root_ref = work.root.nodes;
    RxMotion *motions_ref = work.motions;
    RxRoomCut *cuts_ref = work.header.cuts;

    put(other + exit, 16); // Cannot parse the unterminated/trailing bytes as a valid MTN.
    check(rx_decode_room_binary(other, bytes, &work) && work.root.nodes == root_ref &&
          work.motions == motions_ref && work.header.cuts == cuts_ref,
          "bad final MTN changed active room work");
    memcpy(other, data, bytes);
    put(other + bank, UINT32_MAX - 1);
    check(rx_decode_room_binary(other, bytes, &work) && work.motions == motions_ref,
          "motion payload outside its section accepted");
    memcpy(other, data, bytes);
    put(other + word(data + 20), (uint32_t)table);
    check(rx_decode_room_binary(other, bytes, &work) && work.root.nodes == root_ref,
          "root MDL pointer into motion section accepted");
    memcpy(other, data, bytes);
    size_t first_cut = work.header.tables[RX_CUTS].offset;
    put(other + first_cut + offsetof(Ps2RoomCut32, areas), (uint32_t)first_cut);
    check(rx_decode_room_binary(other, bytes, &work) && work.header.cuts == cuts_ref,
          "cut area list pointing into the cut records was accepted");
    memcpy(other, data, bytes);
    put(other + first_cut + offsetof(Ps2RoomCut32, areas), work.header.tables[RX_LIGHTS].offset);
    check(rx_decode_room_binary(other, bytes, &work) && work.header.cuts == cuts_ref,
          "cut area list pointing into the light records was accepted");
    memcpy(other, data, bytes);
    put(other + rom + offsetof(Ps2RoomHeader32, pointers) + RX_POSITIONS * 4, (uint32_t)table);
    check(rx_decode_room_binary(other, bytes, &work) && work.header.cuts == cuts_ref,
          "position table pointing into the motion section was accepted");
    memcpy(other, data, bytes);
    size_t camera = work.header.tables[RX_EVENT_CAMERAS].offset;
    RxEventCamera *camera_ref = work.header.event_cameras;
    RxRoomLight *light_ref = work.header.event_lights;
    put(other + camera + 4, (17u << 16) | (word(data + camera + 4) & 0xffffu));
    check(rx_decode_room_binary(other, bytes, &work) && work.header.event_cameras == camera_ref &&
          work.header.event_lights == light_ref, "invalid camera count replaced active work");
    memcpy(other, data, bytes);
    put(other + camera + 4, (word(data + camera + 4) & 0xffff0000u) | (work.header.counts[11] + 1));
    check(rx_decode_room_binary(other, bytes, &work) && work.header.event_cameras == camera_ref,
          "out-of-range next camera accepted");

    memcpy(other, data, bytes);
    size_t first_length = word(data + bank);
    check(slots > 1 && first_length && bank + 4 + first_length < exit, "missing first MTN");
    put(other + bank, 0);
    memmove(other + bank + 4, other + bank + 4 + first_length,
            exit + 4 - (bank + 4 + first_length));
    uint32_t second_frames = work.motions[1].frame_count;
    error = rx_decode_room_binary(other, bytes, &work);
    check(!error, error);
    check(work.motion_slots == slots && !work.motions[0].tracks &&
          work.motions[1].frame_count == second_frames, "empty motion slot shifted indices");

    rx_room_binary_destroy(&work);
    memcpy(other, data, bytes);
    put(other + room_slot, 0);
    error = rx_decode_room_binary(other, bytes, &work);
    check(!error, error);
    check(work.root.node_count == 67 && !work.motions && !work.motion_slots, "absent optional motion bank rejected");
    rx_room_binary_destroy(&work);
    rx_room_binary_destroy(&work);
    puts("PASS bounded sections, late decode rollback, empty slots and repeated room-work release");
    free(data);
    free(other);
    return 0;
}
