#include "opening.h"

#include "boot_disc.h"
#include "enemy01_work.h"
#include "inventory_status.h"
#include "item_names.h"
#include "event_audio.h"
#include "event_movie.h"
#include "message_runtime.h"
#include "game_runtime.h"
#include "fade_runtime.h"
#include "message.h"
#include "player_control_runtime.h"
#include "camera.h"
#include "cut.h"
#include "expand.h"
#include "hitchk.h"
#include "light.h"
#include "main.h"
#include "player.h"
#include "ps2_dummy.h"
#include "ps2_NaMath.h"
#include "ps2_NaView.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_pxlconv.h"
#include "ps2_sg_gd.h"
#include "ps2_sg_maloc.h"
#include "ps2_texture.h"
#include "pwksub.h"
#include "room.h"
#include "system.h"
#include "weapon.h"
#include "dread.h"
#include "MdlPut.h"
#include "effect.h"
#include "effect_runtime.h"
#include "ps2/cdvd_image.h"

#include <stdlib.h>
#include <string.h>
#include <android/log.h>

enum { ARENA_SIZE = 12845056, TEXTURE_HEAP_SIZE = 10 * 1024 * 1024 };
static NJS_MATRIX matrices[128];

const char *rx_opening_check_events(RxEventFault *fault)
{
    const char *error=rx_events_validate_room(fault);if(error)return error;
    *fault=(RxEventFault){.opcode=RX_OPENING_LIVE_RUNTIME,
        .reason="Original remaining model/controller paths and gameplay handoff are not ported yet"};
    return fault->reason;
}

// The flat host uses the original 4:3 view.
float pc_hor_scale(void) { return 1.0f; }

static const char *keep_actor_model(ML_WORK *model)
{
    if (!model || !model->objP || !model->obj_num) return NULL;
    uintptr_t start=((uintptr_t)sys->memp+15)&~(uintptr_t)15;
    uintptr_t end=(uintptr_t)sys->endp;
    if (start>end || model->obj_num>(end-start)/sizeof(O_WORK))
        return "Original actor joint work exceeds the native game arena";
    sys->memp=bhKeepObjWork(model,sys->memp);
    bhCalcTree(NULL,model);
    return NULL;
}

static const char *load_opening_room(void)
{
    GDFS file = gdFsOpen("RM_0000.RDX", NULL);
    Sint32 bytes;
    if (!file || !gdFsGetFileSize(file, &bytes) || bytes <= 0 || bytes > ARENA_SIZE) {
        if (file) gdFsClose(file);
        return "Could not open the original starting room";
    }
    unsigned sectors = ((unsigned)bytes + CDVD_SECTOR_SIZE - 1) / CDVD_SECTOR_SIZE;
    unsigned char *packed = malloc((size_t)sectors * CDVD_SECTOR_SIZE);
    int loaded = packed && gdFsRead(file, sectors, packed) == 0;
    gdFsClose(file);
    if (!loaded) {
        free(packed);
        return "Could not read the original starting room";
    }
    size_t available = (size_t)(sys->endp - sys->memp);
    int expanded = ExpandBounded(packed, (size_t)bytes, sys->memp, available);
    free(packed);
    if (expanded <= 0) return "Could not expand the original starting room";

    sys->rdtp = sys->memp;
    sys->rdtsz = (unsigned)expanded;
    sys->memp += expanded;
    sys->ef_pbnk = 32;
    bhSetRDT();
    if ((sys->error & 1) || !rom->mdl.objP || !rom->mdl.texP || !rom->cut_n ||
        (unsigned)rom->amb_chr >= 4)
        return "The original starting room failed to initialize";
    for (unsigned i = 0; i < (unsigned)sys->txr_n; ++i) {
        NJS_TEXLIST *list = sys->txlp[i];
        int count = bhSetMemPvpTexture(list, sys->txdp[i], sys->txloff[i]);
        if (count < 0 || (unsigned)count != list->nbTexture)
            return "Could not register an original starting-room texture";
    }
    bhBindNativeRoomMotions();
    /* The model-work allocation portion of bhFinishRoom. Actor constructors
       have published native model records; retain their local joint work in
       the original arena before scripts attach objects or create shadows. */
    const char *error=NULL;
    for (unsigned i=0;!error && i<128;++i)
        if (ene[i].flg&1)
            for (int j=0;!error && j<ene[i].mdl_n;++j)
                error=keep_actor_model(&ene[i].mdl[j]);
    for (int i=4;!error && i<rom->obj_n;++i) error=keep_actor_model(sys->obwp[i].mlwP);
    for (int i=0;!error && i<rom->itm_n;++i) error=keep_actor_model(sys->itwp[i].mlwP);
    if(!error)error=bhRetainNativeRoomActorWork();
    if (error) return error;
    return NULL;
}

static const char *load_opening_player(RxOpening *opening)
{
    // The opening starts with Claire in her default costume (dread.c).
    unsigned size;
    unsigned char *data = rx_boot_read_member(6, 10, &size);
    if (!data) return "Could not read Claire's original SYSTEM.AFS package";
    const unsigned short *motions = PlMtnAct[0][0];
    const RxPlayerClipSource clips[RX_PLAYER_CLIPS] = {
        [RX_PLAYER_IDLE] = {motions[0], -1},
        [RX_PLAYER_TURN] = {motions[1], -1},
        [RX_PLAYER_WALK] = {motions[2], PlyTrsZ[0][0]},
        [RX_PLAYER_RUN] = {motions[3], PlyTrsZ[3][0]},
        [RX_PLAYER_REVERSE] = {motions[4], PlyTrsZ[1][0]}
    };
    const char *error = rx_engine_player_load(data, size, clips, &opening->player);
    syFree(data);
    if (error) return error;

    unsigned char *pictures;
    NJS_TEXLIST *textures = rx_engine_player_textures(opening->player, &pictures);
    int count = bhSetMemPvpTexture(textures, pictures, 0);
    if (count < 0 || (unsigned)count != textures->nbTexture)
        return "Could not register Claire's original textures";
    opening->room_texture_count = rom->mdl.texP->nbTexture;
    opening->texture_count = opening->room_texture_count + textures->nbTexture;
    return NULL;
}

static const char *load_opening_weapon(RxOpening *opening)
{
    unsigned size;
    // Original bhSysCallGame selects SYSTEM.AFS at 20 + character*30 + weapon.
    unsigned member = 20 + (unsigned char)sys->ply_id * 30 + (unsigned)plp->wpnr_no;
    unsigned char *data = rx_boot_read_member(6, member, &size);
    if (!data) return "Could not read the original equipped-hand package";
    const char *error = rx_engine_weapon_load(data, size, plp->mlwP->obj_num, &opening->weapon);
    syFree(data);
    if (error) return error;
    for (unsigned side = 0; side < 2; ++side) {
        unsigned char *pictures;
        NJS_TEXLIST *textures = rx_engine_weapon_textures(opening->weapon, side, &pictures);
        if (!textures || !textures->nbTexture) continue;
        int count = bhSetMemPvpTexture(textures, pictures, 0);
        if (count < 0 || (unsigned)count != textures->nbTexture)
            return "Could not register the original equipped-hand textures";
        opening->texture_count += textures->nbTexture;
    }
    for (unsigned side = 0; side < 2; ++side)
        bhSetWeapon(&sys->obwp[side], plp->wpnr_no, side);
    rx_engine_weapon_bind(opening->weapon, plp, sys->obwp);
    error=rx_player_control_bind_models(opening->player,opening->weapon);
    if(error)return error;
    opening->weapon_number=(unsigned)plp->wpnr_no;
    opening->textures = calloc(opening->texture_count, sizeof(*opening->textures));
    if (!opening->textures) return "Could not allocate original actor texture handles";
    return NULL;
}

static void equipment_clips(unsigned weapon,unsigned level,RxPlayerClipSource clips[RX_PLAYER_CLIPS])
{
    const unsigned short *motions=PlMtnAct[weapon>=10][level];
    clips[RX_PLAYER_IDLE]=(RxPlayerClipSource){motions[0],-1};
    clips[RX_PLAYER_TURN]=(RxPlayerClipSource){motions[1],-1};
    clips[RX_PLAYER_WALK]=(RxPlayerClipSource){motions[2],PlyTrsZ[0][level]};
    clips[RX_PLAYER_RUN]=(RxPlayerClipSource){motions[3],PlyTrsZ[3][level]};
    clips[RX_PLAYER_REVERSE]=(RxPlayerClipSource){motions[4],PlyTrsZ[1][level]};
}
static void release_weapon_textures(RxEngineWeapon *weapon)
{
    for(unsigned side=0;side<2;++side) {
        NJS_TEXLIST *list=rx_engine_weapon_textures(weapon,side,NULL);
        if(!list)continue;
        for(unsigned i=0;i<list->nbTexture;++i) {
            uintptr_t pointer=list->textures[i].texaddr,start=(uintptr_t)tbuf;
            /* A failed registration can leave raw global IDs among native pointers. */
            if(pointer>=start && pointer-start<256*sizeof(*tbuf) && (pointer-start)%sizeof(*tbuf)==0) {
                NJS_TEXLIST one={&list->textures[i],1};njReleaseTexture(&one);
            }
        }
    }
    bhGarbageTexture(NULL,0);
}
static const char *register_weapon_textures(RxEngineWeapon *weapon)
{
    for(unsigned side=0;side<2;++side) {
        unsigned char *pictures;NJS_TEXLIST *list=rx_engine_weapon_textures(weapon,side,&pictures);
        if(!list||!list->nbTexture)continue;
        int count=bhSetMemPvpTexture(list,pictures,0);
        if(count<0||(unsigned)count!=list->nbTexture)return "Could not register replacement weapon textures";
    }
    return NULL;
}
const char *rx_opening_sync_weapon(RxOpening *opening)
{
    if(!opening||!opening->weapon||!opening->player||!plp||!sys->obwp||!plp->mlwP||!plp->mnwP||
       plp->exp0!=(unsigned char *)&opening->player_work)
        return "Missing native equipment binding";
    unsigned number=(unsigned)plp->wpnr_no;
    if(number>20 || sys->ply_id!=0 || sys->costume!=0 || (unsigned)opening->player_work.dmlvl>2)
        return "Unported original equipment package or player variant";
    if(number==opening->weapon_number)return NULL;
    RxPlayerClipSource clips[RX_PLAYER_CLIPS];equipment_clips(number,opening->player_work.dmlvl,clips);
    const char *error=rx_engine_player_clips(opening->player,clips,0);
    if(error)return error;
    unsigned bytes;unsigned char *data=rx_boot_read_member(6,20+number,&bytes);
    if(!data)return "Could not read replacement original weapon package";
    RxEngineWeapon *next=NULL;error=rx_engine_weapon_load(data,bytes,plp->mlwP->obj_num,&next);syFree(data);
    if(error)return error;
    unsigned count=opening->room_texture_count+rx_engine_player_textures(opening->player,NULL)->nbTexture;
    for(unsigned side=0;side<2;++side) {
        NJS_TEXLIST *list=rx_engine_weapon_textures(next,side,NULL);if(list)count+=list->nbTexture;
    }
    GLuint *handles=calloc(count,sizeof(*handles));
    if(!handles){rx_engine_weapon_destroy(next);return "Could not allocate replacement weapon texture cache";}
    /* Match the original release/re-register order: packages reuse global IDs.
       Keep the old owned package until registration and native publication succeed. */
    rx_opening_close_graphics(opening);release_weapon_textures(opening->weapon);
    error=register_weapon_textures(next);
    if(!error)error=rx_player_control_bind_models(opening->player,next);
    if(error) {
        release_weapon_textures(next);
        const char *restore=register_weapon_textures(opening->weapon);
        rx_engine_weapon_destroy(next);free(handles);
        return restore?"Could not restore previous equipment texture registration":error;
    }
    RxEngineWeapon *previous=opening->weapon;
    memset(sys->obwp,0,2*sizeof(*sys->obwp));
    for(unsigned side=0;side<2;++side)bhSetWeapon(&sys->obwp[side],(int)number,(int)side);
    rx_engine_weapon_bind(next,plp,sys->obwp);
    rx_engine_player_clips(opening->player,clips,1);
    opening->weapon=next;opening->weapon_number=number;
    free(opening->textures);opening->textures=handles;opening->texture_count=count;
    rx_engine_weapon_destroy(previous);
    /* mn_mode0=3 remains the original pending monitor request. Its sound-bank
       transfer/acknowledgement is not implemented by this resource adapter. */
    return NULL;
}

const char *rx_opening_start(RxOpening *opening, unsigned difficulty)
{
    if (difficulty > 2 || opening->arena) return "Invalid New Game request";
    opening->arena = aligned_alloc(64, ARENA_SIZE);
    opening->texture_heap = aligned_alloc(64, TEXTURE_HEAP_SIZE);
    opening->pixel_work = aligned_alloc(64, 1024 * 1024);
    if (!opening->arena || !opening->texture_heap || !opening->pixel_work) {
        rx_opening_end(opening);
        return "Could not allocate the original game work buffers";
    }

    const char *arena_error=rx_enemy_arena_bind(opening->arena,ARENA_SIZE);
    if(arena_error){rx_opening_end(opening);return arena_error;}
    rx_inventory_status_reset();
    rx_item_names_clear();

    keepmem = freemem = opening->arena;
    sys->endp = freemem + ARENA_SIZE;
    sys->tk_flg = 0x300020; // Original bhSysCallTitle hands off the opening task.
    sys->gm_mode = difficulty;
    sys->pos_no = 0;
    sys->ef_ct = sys->ef_ctb = sys->ef_extn = 0;
    sys->ef_tlist = (NJS_TEXLIST){sys->ef_tex, 0};
    sys->error = 0;
    _Make_SinTable();
    njInitMatrix(matrices, 128, 0);
    njUnitMatrix(&crmat);
    njScale(&crmat, 1, -1, -1);
    Ps2_tex_buff = opening->texture_heap;
    Ps2_PXLCONV = opening->pixel_work;
    njInitTexture(tbuf, 256);
    bhFirstGameStart();
    bhFontScaleSet(1.0f,1.0f,1.0f);
    unsigned names_bytes=0;
    unsigned char *names=rx_boot_read_file("SYSMES.ALD",&names_bytes);
    const char *names_error=names?rx_item_names_load(names,names_bytes):"Could not read original item-name bank";
    if(!names_error)names_error=rx_system_messages_load(names,names_bytes);
    if(names)syFree(names);
    if(names_error){rx_opening_end(opening);return names_error;}
    rx_message_clear();
    rx_fade_clear();
    rx_events_reset();
    rx_effects_reset();
    if (!sys->obwp || !sys->itwp) {
        rx_opening_end(opening);
        return "Original New Game initialization ran out of workspace";
    }

    unsigned effects_bytes=0;
    unsigned char *effects=rx_boot_read_member(6,1,&effects_bytes);
    const char *error=effects ? rx_engine_effects_load(effects,effects_bytes,&opening->effects) :
        "Could not read the original common effect models";
    if (effects) syFree(effects);
    static const unsigned effect_ids[]={0,1,7,13};
    for (unsigned i=0;!error && i<4;++i)
        sys->efm[effect_ids[i]]=*rx_engine_effect_model(opening->effects,effect_ids[i]);
    if (!error) error=rx_effects_load_common(opening->effects);
    if (!error) error = load_opening_room();
    if (!error) error = load_opening_player(opening);
    if (error) {
        rx_opening_end(opening);
        return error;
    }
    memset(plp, 0, sizeof(*plp));
    error=rx_player_control_bind(plp,&opening->player_work,sizeof(opening->player_work));
    if(error){rx_opening_end(opening);return error;}
    plp->exp1 = (unsigned char *)&opening->pose_flags;
    error=rx_player_control_bind_script_flags(&opening->pose_flags);
    if(error){rx_opening_end(opening);return error;}
    rx_engine_player_bind(opening->player, plp);
    error = rx_engine_player_pose(opening->player, plp, RX_PLAYER_IDLE, 0, 0);
    if (error) {
        rx_opening_end(opening);
        return error;
    }
    plp->mode0 = 1; // Standard player action state used by the active camera.
    plp->flg = 0x119; // Original bhSetPlayer initial active/model flags.
    plp->mdflg = 0x20;
    plp->hp = sys->ply_hp[(unsigned char)sys->ply_id];
    plp->stflg = sys->ply_stflg[(unsigned char)sys->ply_id];
    plp->wpnr_no = plp->wpnl_no = sys->ply_wno[(unsigned char)sys->ply_id];
    plp->ar = PlyInfo[0][0];
    plp->ah = PlyInfo[0][1];
    error=rx_player_control_place_room();
    if(error){rx_opening_end(opening);return error;}
    bhCalcModel(plp);
    error=rx_engine_player_publish(opening->player,plp);
    if(error){rx_opening_end(opening);return error;}
    error = load_opening_weapon(opening);
    if (error) {
        rx_opening_end(opening);
        return error;
    }
    sys->pt_flg=~0u; // Original system initialization enables actor shadows.
    if (bhSetShadow(NULL,(unsigned char *)plp,1,4.5f,4.0f,3.5f)<0) {
        rx_opening_end(opening);
        return "Original player shadow work is exhausted";
    }
    bhInitCamera();
    NJS_SCREEN screen={640.0f,640.0f,480.0f,320.0f,240.0f};
    njSetScreen(&screen);
    njClipZ(-2.0f,-20000.0f);
    bhInitLight();
    int cut = bhCheckCutArea(plp->px, plp->pz, plp->flr_no);
    cam.ncut = cut >= 0 && cut < rom->cut_n ? cut : 0;
    bhSetCut(cam.ncut, 1);
    cam.ocflg = rom->cutp[cam.ncut].cam[cam.camver].flg;
    pd_port = 0;
    sys->sp_flg |= 1 | 8 | 0x20 | 0x40 | 0x80; // Motion, effects, pad, player and light tasks.
    rx_opening_clear_controls();
    error=rx_room_effects_prepare(&opening->room_effects);
    if (error) { rx_opening_end(opening); return error; }
    RxRoomEffectFault effect_fault;
    error=rx_room_effects_check_live(opening->room_effects,&effect_fault);
    if (!error) {
        error=rx_room_effects_publish(opening->room_effects);
        if (error) { rx_opening_end(opening); return error; }
        __android_log_print(ANDROID_LOG_INFO,"RECVX-ARM64","Published %zu original room effects",
                            rx_room_effects_count(opening->room_effects));
    } else {
        __android_log_print(ANDROID_LOG_INFO,"RECVX-ARM64",
                           "Prepared %zu original room effects; record %zu ID %u awaits update/draw: %s",
                           rx_room_effects_count(opening->room_effects),effect_fault.record,effect_fault.id,error);
    }
    error=rx_event_audio_init(1);
    if(!error)error=rx_event_se_mount((unsigned)sys->stg_no*1000+(unsigned char)sys->rom_no*10+(unsigned char)sys->rcase);
    if(!error)error=rx_event_bgm_mount();
    if(!error)error=rx_event_voice_mount();
    if(error){rx_opening_end(opening);return error;}
    error=rx_event_movie_bind(opening);
    if(error){rx_opening_end(opening);return error;}
    RxEventFault fault;
    opening->events_ready = !rx_opening_check_events(&fault);
    opening->commands_ready=fault.opcode==RX_OPENING_LIVE_RUNTIME||opening->events_ready;
    if (opening->events_ready) {
        error = rx_events_start_room();
        if (error) {
            rx_opening_end(opening);
            return error;
        }
    } else if(opening->commands_ready){
        __android_log_print(ANDROID_LOG_INFO,"RECVX-ARM64","Original room scenario command preflight complete; live runtime awaits: %s",fault.reason);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "RECVX-ARM64",
                           "Original room scenario awaits opcode %02x at byte %zu: %s",
                           fault.opcode, fault.offset, fault.reason);
    }
    return NULL;
}

void rx_opening_read_runtime_controls(void){bhSysCallPad();}

void rx_opening_read_controls(void)
{
    bhSysCallPad();
    if ((sys->sp_flg & 0x40) && !(sys->cb_flg & 4) && !(plp->flg & 0x10000))
        bhControlPlayerPad();
}

static RxPlayerClip opening_clip(void)
{
    // Locomotion states selected by the original bhControlPlayerPad.
    switch (plp->mode2) {
    case 1: case 2: return RX_PLAYER_TURN;
    case 3: case 4: case 5:
        return sys->pad_on & 0x400 ? RX_PLAYER_RUN : RX_PLAYER_WALK;
    case 6: return RX_PLAYER_RUN;
    case 9: case 10: case 11: return RX_PLAYER_REVERSE;
    default: return RX_PLAYER_IDLE;
    }
}

static void update_opening_cut(void)
{
    float x = plp->px, z = plp->pz;
    char floor = plp->flr_no;
    int cut = bhCheckCutArea(x, z, floor);
    if (cut < 0 || cut == cam.ncut || cut >= rom->cut_n) return;

    // The original bhCheckCut keeps the current shot within one unit of its area.
    if (bhCheckCutArea(x - 1, z, floor) == cam.ncut ||
        bhCheckCutArea(x + 1, z, floor) == cam.ncut ||
        bhCheckCutArea(x, z - 1, floor) == cam.ncut ||
        bhCheckCutArea(x, z + 1, floor) == cam.ncut) return;
    cam.ocut = cam.ncut;
    cam.ocflg = rom->cutp[cam.ocut].cam[cam.camver].flg;
    cam.ncut = cut;
    bhSetCut(cut, 0);
}

const char *rx_opening_move_player(RxOpening *opening)
{
    const char *audio_error=rx_event_bgm_tick();
    if(audio_error)return audio_error;
    const char *fade_error=rx_fade_tick();
    if(fade_error)return fade_error;
    audio_error=rx_event_voice_monitor();
    if(audio_error)return audio_error;
    if(opening->events_ready){sys->loop_now=0;sys->loop_ct=1;const char *error=bhStepNativeRoomActors();if(error)return error;
        error=rx_player_control_script_tick();if(error)return error;
        error=rx_game_flags_tick();if(error)return error;}
    const char *message_error=rx_message_tick((sys->ts_flg&0x200)?0:1);
    if(message_error)return message_error;
    if (opening->events_ready && (sys->sp_flg&0x10)) {
        const char *error = rx_events_tick();
        if (error) return error;
    }
    if(opening->events_ready){const char *error=rx_event_movie_tick();if(error)return error;}
    const char *equipment_error=rx_opening_sync_weapon(opening);
    if(equipment_error)return equipment_error;
    if(opening->events_ready)return NULL;
    if (!(sys->sp_flg & 0x40) || !(sys->sp_flg & 1) ||
        (plp->stflg & 0x1000000) || (sys->cb_flg & 4)) return NULL;

    if ((plp->flg & 0x10000) && !(plp->flg & 0x4000000)) {
        // bhCPM2_act_run keeps the action state until forward or Run is released.
        if (!(sys->pad_on & 1)) {
            plp->flg &= ~0x10000u;
            plp->mode2 = 0;
            plp->mode3 = 0;
        } else if (!(sys->pad_on & 0x400)) {
            plp->flg &= ~0x10000u;
            plp->flg |= 0x800000;
            plp->mode2 = 3;
            plp->mode3 = 0;
        }
    }
    int quick_started = plp->mode2 >= 9 && plp->mode2 <= 11 && (sys->pad_ps & 0x400);
    if (quick_started) {
        if (opening->pose_clip != RX_PLAYER_REVERSE) {
            // bhCPM2_act_bak enters the reverse clip before checking Run.
            plp->mtn_no = PlMtnAct[opening->player_work.wpntp][opening->player_work.dmlvl][4];
            plp->frm_no = 25 * 65536;
        }
        bhStartPlayerQuickTurn();
    }
    int turning = (plp->flg & 0x4000000) != 0;

    RxPlayerClip clip = opening_clip();
    unsigned frames = rx_engine_player_frames(opening->player, clip);
    int fixed_frame = plp->frm_no;
    if(fixed_frame<0 || (unsigned)fixed_frame>=frames*65536)fixed_frame=0;
    int direction = clip == RX_PLAYER_TURN && plp->mode2 == 1 ? -1 : 1;
    if (clip != opening->pose_clip && !quick_started) {
        if (clip == RX_PLAYER_RUN || (clip == RX_PLAYER_WALK && opening->pose_clip == RX_PLAYER_RUN)) {
            unsigned old_frames = rx_engine_player_frames(opening->player, opening->pose_clip);
            fixed_frame = bhGetFrameNum(old_frames, frames, fixed_frame);
        } else {
            // bhCPM2_act_wlk and bhCPM2_act_bak enter their authored cycles here.
            fixed_frame = (clip == RX_PLAYER_WALK ? 5 : clip == RX_PLAYER_REVERSE ? 25 : 0) * 65536;
        }
        fixed_frame %= (int)(frames * 65536);
        static const unsigned blend_frames[RX_PLAYER_CLIPS] = {5, 8, 8, 4, 8};
        plp->hokan_count = blend_frames[clip];
        plp->hokan_rate = clip == RX_PLAYER_REVERSE ? 49152 : 42598;
    }
    unsigned frame = (unsigned)fixed_frame / 65536;
    opening->pose_clip = clip;
    opening->pose_frame = frame;

    if (clip != RX_PLAYER_IDLE && !turning) {
        float angle = clip == RX_PLAYER_TURN ? 7.2f : clip == RX_PLAYER_RUN ? 4.5f : 4.0f;
        int yaw = (int)(182.04445f * angle);
        if (sys->pad_on & 8) plp->ay = (plp->ay - yaw) & 0xffff;
        if (sys->pad_on & 4) plp->ay = (plp->ay + yaw) & 0xffff;
    }

    if (clip == RX_PLAYER_WALK || clip == RX_PLAYER_RUN || clip == RX_PLAYER_REVERSE) {
        NJS_POINT3 previous = {plp->px, plp->py, plp->pz};
        if (turning) {
            // The initiating reverse-state frame turns without translation.
            if (!quick_started) bhStepPlayerQuickTurn();
        } else {
            plp->spd = rx_engine_player_speed(opening->player, clip, frame);
            if (clip == RX_PLAYER_REVERSE) plp->spd = -plp->spd;
            bhAddSpeed(plp, clip == RX_PLAYER_REVERSE ? 32768 : 0);
        }
        NJS_POINT3 position = {plp->px, plp->py, plp->pz};
        plp->stflg &= ~1u;
        bhCheckWallEx(plp, &position, &previous, plp->ar, plp->ah);
        plp->px = plp->gpx = opening->player_work.plx = position.x;
        plp->py = plp->gpy = position.y;
        plp->pz = plp->gpz = opening->player_work.plz = position.z;
        update_opening_cut();
    } else {
        plp->spd = 0;
    }
    if (turning && bhAdvancePlayerTurn()) bhFinishPlayerTurn();
    const char *error = rx_engine_player_pose(opening->player, plp, clip, fixed_frame, direction);
    if (error) return error;
    bhControlActiveCamera();
    return NULL;
}

void rx_opening_clear_controls(void)
{
    sys->pad_on = sys->pad_oncpy = sys->pad_old = sys->pad_ps = sys->pad_rs = 0;
    sys->pad_ax = sys->pad_ay = sys->pad_al = sys->pad_ar = sys->pad_dx = sys->pad_dy = 0;
}

void rx_opening_end(RxOpening *opening)
{
    rx_inventory_status_reset();
    rx_item_names_clear();
    rx_event_movie_end(opening);
    rx_fade_clear();
    rx_message_clear();
    rx_event_audio_end();
    rx_events_reset();
    rx_player_control_reset();
    rx_effects_reset();
    rx_room_effects_destroy(opening->room_effects);
    opening->room_effects=NULL;
    if (sys->rdtp && opening->arena) bhInitRoom();
    sys->rdtp = sys->memp = sys->endp = NULL;
    sys->rdtsz = 0;
    pd_port = -1;
    sys->p1per = NULL;
    rx_opening_clear_controls();
    if (sys->obwp && opening->weapon) memset(sys->obwp, 0, 2 * sizeof(*sys->obwp));
    sys->obwp = sys->itwp = NULL;
    keepmem = freemem = NULL;
    Ps2_tex_buff = Ps2_PXLCONV = NULL;
    memset(plp, 0, sizeof(*plp));
    rx_engine_weapon_destroy(opening->weapon);
    rx_engine_player_destroy(opening->player);
    for (unsigned i=0;i<4;++i) {
        static const unsigned ids[]={0,1,7,13};
        memset(&sys->efm[ids[i]],0,sizeof(sys->efm[0]));
    }
    rx_engine_effects_destroy(opening->effects);
    free(opening->textures);
    free(opening->actor_textures);
    free(opening->pixel_work);
    free(opening->texture_heap);
    rx_enemy_arena_release(opening->arena);
    free(opening->arena);
    *opening = (RxOpening){0};
}
