#include "event_runtime.h"
#include "room_scripts.h"
#include "event.h"
#include "main.h"
#include "event_audio.h"
#include "event_effects.h"
#include "message_runtime.h"
#include "fade_runtime.h"
#include "face_control_runtime.h"
#include "player_control_runtime.h"
#include "room.h"
#ifdef RECVX_NATIVE_MOVIE
#include "event_movie.h"
#endif

#include <stdint.h>
#include <math.h>
#include <limits.h>
#include <string.h>

extern unsigned char *bhScePtr, *G_Sp;
extern int bhIfelFlg;
extern BH_SCEWORK bhEtask[16], *bhCetask;
extern unsigned int Event_T_timer;

typedef struct {
    unsigned (*handler)(void);
    unsigned bytes;
} Command;

/* Each populated entry calls the original handler. Unported commands are
   reported before a room entry can change game state. */
static const Command commands[256] = {
#ifdef RECVX_NATIVE_MOVIE
    [0x5e]={bhMovieStart,4},[0x5f]={bhMovieStop,2},[0x9b]={bhMovieCk,6},
#endif
    [0x00] = {bhEnd,2}, [0x01] = {bhIfelCk,2}, [0x02] = {bhElseCk,2},
    [0x03] = {bhEndif,2}, [0x04] = {bhCk,6}, [0x05] = {bhSet,6},
    [0x06] = {bhCmpB,4}, [0x07] = {bhCmpW,6}, [0x08] = {bhSv,4}, [0x09] = {bhSvW,4},
    [0x0a] = {bhWalAtariSet,4}, [0x0b] = {bhEtcAtariSet,4}, [0x0c] = {bhFlrAtariSet,4},
    [0x11] = {bhPlItemCheck,2},
    [0x0e] = {bhItmCk,6}, [0x23] = {bhItmSetCk,6},
    [0x26] = {bhArmsItemCheck,2}, [0x81] = {bhSubMapBusyCk,2},
    [0x86] = {bhEasySESet,16}, [0x16] = {bhBgmOff,2}, [0x18] = {bhSeOff,2},
    [0x19] = {bhVoiceOn,8}, [0x1a] = {bhVoiceOff,2},
    [0x1f] = {bhMessageSet,4}, [0x5c] = {bhMesDispEndSet,2},
    [0x36] = {bhFadeSet,6}, [0x91] = {bhEffModeSet,4},
    [0xca] = {bhEffClearEvt,2},
    [0x42] = {bhItmAspdSet,4},
    [0x9c] = {bhSetItmMotion,2},
    [0x27] = {bhArmsItemChange,2},
    [0x31] = {bhPlItemLost,2}, [0x25] = {bhEtcAtariSet2,10}, [0x6b] = {bhDelYakkyou,2},
    [0x63] = {bhRandamSet,2}, [0x8c] = {bhRandTest,6},
    [0x92] = {bhBGSeOff2,2}, [0x93] = {bhBgmOff2,2}, [0x94] = {bhBGSeOn2,4},
    [0xa6] = {bhBgmOnEx,4}, [0x95] = {bhBgmOn2,4}, [0xa7] = {bhBgmOn2Ex,4},
    [0x5b] = {bhInitCamSet,2}, [0x13] = {bhCamSet,4}, [0x12] = {bhCineSet,2}, [0x6a] = {bhEventSkipSet,2},
    [0x9e] = {bhPuruPuruFlagSet,2}, [0x9a] = {bhFaceRep,4},
    [0x8f] = {bhFacePauseSet,4}, [0x90] = {bhFaceReSet,2},
    [0x3e] = {bhMaskStart,4}, [0x3f] = {bhLipStart,4},
    [0x29] = {bhCamPauseSet,2}, [0x38] = {bhFrameCheck,6}, [0x3c] = {bhMaskSet,4}, [0x3d] = {bhLipSet,4},
    [0x14] = {bhEvtOn,4}, [0x20] = {bhSetDispObj,4}, [0x24] = {bhInitModelSet,4},
    [0x2b] = {bhMotionPauseSet,2}, [0x2d] = {bhInitMotionPause,2},
    [0x2e] = {bhMotionPauseSetPly,2}, [0x30] = {bhInitMotionPauseEx,4},
    [0x2f] = {bhInitSetKage,4},
    [0x32] = {bhObjLinkSet,12}, [0x34] = {bhObjLinkSetPly,12},
    [0x35] = {bhLightSet,4}, [0x3b] = {bhDefModelSet,6}, [0x53] = {bhObjLinkSetEneItem,12},
    [0x43] = {bhEffDispSet,4}, [0x44] = {bhEffAmbSet,6},
    [0x5d] = {bhPadCheck,6}, [0x65] = {bhLoadWork,4},
    [0x67] = {bhSubCtr,1}, [0x64] = {bhPlCtr,1}, [0x69] = {bhCommonCtr,1},
    [0x60] = {bhTFrameCheck,6}, [0x61] = {bhEventTimerClr,2}, [0xbc] = {bhEvtKill,2},
    [0xf3] = {bhEwhile2,1}, [0xf4] = {bhComNext,1},
    [0xf8] = {bhSleep,4}, [0xf9] = {bhSleeping,3},
    [0xfa] = {bhFor,4}, [0xfb] = {bhNext,2}, [0xfc] = {bhWhile,2},
    [0xfd] = {bhEwhile,1}, [0xfe] = {bhEvtNext,1}, [0xff] = {bhEvtEnd,2}
};

/* Full lengths include both the outer opcode and original subcommand. */
static const unsigned char common_bytes[256]={
    [0]=2,[2]=3,[3]=3,[5]=6,[6]=6,[7]=8,[9]=6,[11]=6,[12]=6,[13]=6,
    [23]=4,[24]=8,[25]=8,[34]=10,[36]=2,[41]=4,[47]=2
};
static const unsigned char sub_bytes[256]={[0]=2,[128]=2,[139]=2,[144]=2,[149]=4};
static const unsigned char player_bytes[256]={[0]=2,[7]=8,[12]=6,[13]=6,[52]=2,[128]=2,[131]=4};
static unsigned command_bytes(const unsigned char *p)
{
    return *p==0x67?sub_bytes[p[1]]:*p==0x69?common_bytes[p[1]]:*p==0x64?player_bytes[p[1]]:commands[*p].bytes;
}
static int effect_common(unsigned sub)
{
    switch(sub){case 0:case 2:case 3:case 5:case 6:case 7:case 11:case 12:case 13:case 23:case 41:return 1;default:return 0;}
}

_Static_assert(offsetof(O_WRK,px)==offsetof(BH_PWORK,px) &&
               offsetof(O_WRK,ax)==offsetof(BH_PWORK,ax) &&
               offsetof(O_WRK,mdl)==offsetof(BH_PWORK,mdl) &&
               offsetof(O_WRK,mlwP)==offsetof(BH_PWORK,mlwP),"Original common actor prefix changed");
#define ITEM_PREFIX(field) _Static_assert(offsetof(O_WRK,field)==offsetof(BH_PWORK,field),"Original item motion prefix changed: " #field)
ITEM_PREFIX(mode3); ITEM_PREFIX(mnwP); ITEM_PREFIX(mtn_no); ITEM_PREFIX(mtn_add);
ITEM_PREFIX(mtn_md); ITEM_PREFIX(mtn_tp); ITEM_PREFIX(frm_no); ITEM_PREFIX(frm_mode);
ITEM_PREFIX(mtn_attr); ITEM_PREFIX(hokan_count); ITEM_PREFIX(hokan_rate); ITEM_PREFIX(mtx);
ITEM_PREFIX(flg); ITEM_PREFIX(flr_no); ITEM_PREFIX(pxb); ITEM_PREFIX(pyb); ITEM_PREFIX(pzb);
ITEM_PREFIX(axb); ITEM_PREFIX(ayb); ITEM_PREFIX(azb);
#undef ITEM_PREFIX

static RxEventFault current_fault;
static unsigned instructions_left;
enum { FRAME_INSTRUCTION_LIMIT = 100000 };

static unsigned half(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static int in_script(const RxRoomScripts *scripts, const void *pointer, size_t bytes)
{
    if (!scripts || !scripts->data) return 0;
    uintptr_t start = (uintptr_t)scripts->data, address = (uintptr_t)pointer;
    return address >= start && address - start >= scripts->slots * 4 &&
           address - start <= scripts->bytes && bytes <= scripts->bytes - (address - start);
}

static unsigned flag_words(unsigned type)
{
    switch (type) {
    case 1: return sizeof(sys->ev_flg) / sizeof(*sys->ev_flg);
    case 2: return sizeof(sys->ky_flg) / sizeof(*sys->ky_flg);
    case 3: return sizeof(sys->ed_flg) / sizeof(*sys->ed_flg);
    case 7: return sizeof(sys->it_flg) / sizeof(*sys->it_flg);
    case 8: return sizeof(sys->mp_flg) / sizeof(*sys->mp_flg);
    case 9: return sizeof(sys->ic_flg) / sizeof(*sys->ic_flg);
    case 4: case 5: case 6: case 10: case 11: case 12:
    case 13: case 14: case 15: case 16: return 1;
    default: return 0;
    }
}

static int enemy_record(unsigned index)
{
    return rom->ene_n>0 && rom->ene_n<=128 && index < (unsigned)rom->ene_n && rom->enep &&
           rom->enep[index].wrk_no < sizeof(ene) / sizeof(*ene);
}

/* New Game allocates 32 original object and item work records. They include
   reserved objects (weapons) which are not ordinary RDT object entries. */
static BH_PWORK *actor_record(unsigned type, unsigned index)
{
    switch (type) {
    case 0: return plp;
    case 1: return enemy_record(index) ? &ene[rom->enep[index].wrk_no] : NULL;
    case 2: return sys->obwp && index < 32 ? (BH_PWORK *)&sys->obwp[index] : NULL;
    case 3: return sys->itwp && index < 32 ? (BH_PWORK *)&sys->itwp[index] : NULL;
    default: return NULL;
    }
}

static int actor_joint(const BH_PWORK *actor, unsigned joint)
{
    return actor && actor->mlwP && actor->mlwP->objP &&
           joint < actor->mlwP->obj_num;
}

static int companion_valid(const BH_PWORK *actor)
{
    if (!actor) return 0;
    if (actor->id != 1) return 1;
    /* Original multipart enemy handlers also write the second BH_PWORK.
       Require a companion in the native enemy pool before calling them. */
    uintptr_t address = (uintptr_t)actor->exp1, start = (uintptr_t)ene;
    return address >= start && address - start < sizeof(ene) &&
           (address - start) % sizeof(*ene) == 0;
}

static const char *validate_operands(const RxRoomScripts *scripts, const unsigned char *p)
{
    switch (*p) {
    case 0x42:return bhCheckNativeItemSpeed(p[1]);
    case 0xca:return rx_event_effect_clear_check();
#ifdef RECVX_NATIVE_MOVIE
    case 0x5e:return rx_event_movie_check(p[1]);
    case 0x5f:case 0x9b:return rx_event_movie_status_check();
#endif
    case 0x91:
        return rx_event_effect_mode_check(p[1],p[2]);
    case 0x36:
        return rx_fade_check();
    case 0x1f:
        return p[1]?NULL:rx_message_check(p[2]);
    case 0x38:
        /* Original selector 0 ignores the record byte; selector 1 follows the
           RDT work remap, selector 2 uses the 32 original object work slots.
           This predicate only reads frm_no: no model/clip dereference occurs. */
        if(p[1]>2||!actor_record(p[1],p[2]))return "FrameCheck references an absent original player, actor or object";
        break;
    case 0x86: {
        const char *error=rx_event_se_check(p);if(error)return error;
        if(p[9]==0){
            error=rx_player_control_model_check();if(error)return error;
            if(p[11]>=plp->mlwP->obj_num)return "EasySESet player joint exceeds native model";
            for(unsigned i=12;i<15;++i)if(!isfinite(plp->mlwP->owP[p[11]].mtx[i]))return "Nonfinite EasySESet player joint";
        }else{error=bhCheckNativeEventSound(p[9],p[10],p[11]);if(error)return error;}
        if(p[1]==1){BH_PWORK *source=actor_record(p[9],p[10]);return rx_event_step_position_check(&source->mlwP->owP[p[11]].mtx[12]);}
        return NULL;
    }
    case 0x19: {
        const char *error=rx_event_voice_check((unsigned)p[2]|(unsigned)p[3]<<8,p[4]);if(error)return error;
        if(p[1]==0)return rx_player_control_model_check();
        if(p[1]>2)return "VoiceOn source family is not ported yet";
        return bhCheckNativeEventSound(p[1],p[6],0);
    }
    case 0x1a:
        return rx_event_voice_stop_check(p[1]);
    case 0x67:
        if(p[1]!=0&&p[1]!=139)return bhCheckNativeSubControl(p[1],NULL);
        break;
    case 0x64:
        if(p[1]==131)return rx_player_control_check();
        if(p[1]==52)return rx_player_control_stand_check();
        if(p[1]==128)return rx_player_control_handoff_check();
        if(p[1]==7){
            const char *error=rx_player_control_check();if(error)return error;
            for(unsigned i=0;i<31;++i)if(!isfinite(rom->grand[i]))return "Player script floor table is nonfinite";
        }
        break;
    case 0x16:
        return rx_event_bgm_stop_check(p[1]);
    case 0xa6:
        return rx_event_bgm_check(p[1],p[3]);
    case 0x5b: {
        const char *error=rx_player_control_model_check();if(error)return error;
        return bhCheckNativeCameraInit();
    }
    case 0x13: {
        if(p[1]==1){
            const char *error=rx_player_control_model_check();if(error)return error;
            return bhCheckNativeCameraInit();
        }
        if(p[1]!=0)return "Original CamSet selector mode is not ported yet";
        if((unsigned)sys->stg_no>=10 || !plp)return "Camera requires a valid original stage and player";
        if(!rom->evcp || rom->evc_n<=0 || p[2]>=(unsigned)rom->evc_n)
            return "Event camera index exceeds native room bank";
        /* These authored clip-volume overrides also require the native renderer's
           separate display-window/culling-volume path, which is still gated. */
        unsigned stage=(unsigned)sys->stg_no, room=(unsigned char)sys->rom_no, camera_no=p[2];
        if((stage==3&&room==15&&sys->rcase==0&&camera_no==3)||
           (stage==5&&room==0&&sys->rcase==1&&camera_no==0)||
           (stage==6&&room==10&&sys->rcase==0&&(camera_no==24||camera_no==30))||
           (stage==9&&sys->rcase==0&&((room==28&&camera_no==7)||(room==29&&camera_no==0)||(room==35&&camera_no==45))))
            return "Original event camera custom clip volume is not ported yet";
        EVC_WORK *camera=&rom->evcp[p[2]];
        if(camera->keyf_n<1 || camera->keyf_n>(int)(sizeof(camera->keyf)/sizeof(*camera->keyf)) || p[3]>=(unsigned)camera->keyf_n)
            return "Event camera key exceeds original key bank";
        CAM_KEYF_WORK *key=&camera->keyf[p[3]];
        if(!rom->mdl.objP || rom->mdl.obj_num>sizeof(key->hidobj)*8 || rom->evl_n<0 ||
           (unsigned)rom->evl_n>sizeof(key->hidlgt)*8 || (rom->evl_n&&!rom->evlp))
            return "Event camera hide lists exceed native objects or lights";
        if(!isfinite(key->px)||!isfinite(key->py)||!isfinite(key->pz)||
           !isfinite(key->fog_nr)||!isfinite(key->fog_fr)||key->pers<=0)
            return "Invalid original event camera projection";
        /* Actor and point locks require their original world matrices and native
           publication validation; keep them gated until that path is verified. */
        if(key->lkflg)return "Original event camera target lock is not ported yet";
        break;
    }
    case 0x3c: case 0x3d: case 0x9a: case 0x8f: case 0x90: case 0x3e: case 0x3f: {
        unsigned index=(*p==0x3c||*p==0x3d||*p==0x3e||*p==0x3f)?p[2]:p[1];
        if(!enemy_record(index))return "Facial command references an absent room actor";
        if(!(rom->enep[index].flg&1)||rom->enep[index].id!=ene[rom->enep[index].wrk_no].id)
            return "Facial command references an unpublished room actor mapping";
        if(*p==0x3c||*p==0x3d)return rx_face_control_request(&ene[rom->enep[index].wrk_no],*p==0x3c,p[3],p[1]);
        return rx_face_control_check(&ene[rom->enep[index].wrk_no]);
    }
    case 0x92: case 0x94:
        if(p[1]>=2)return "Deferred background sound references an absent original slot";
        break;
    case 0x63:
        if(!p[1])return "Original random selector requires a nonzero modulus";
        break;
    case 0x27:
        if(!plp || (unsigned)sys->ply_id>=sizeof(sys->ply_wno)/sizeof(*sys->ply_wno))
            return "Scenario weapon change exceeds native player equipment storage";
        if(p[1]>20)return "Unported original weapon package selector";
        break;
    case 0x9c: {
        O_WRK *item=sys->itwp;
        if(!item || p[1]>=32)return "Scenario item motion exceeds native item work";
        item+=p[1];
        if(item->mode3!=1 || !(sys->sp_flg&4))break;
        if(!sys->rmthp || item->mnwP!=sys->rmthp || item->mtn_no>=512)
            return "Item motion requires its original native room motion bank";
        const MN_WORK *motion=&item->mnwP[item->mtn_no];
        uintptr_t model=(uintptr_t)item->mlwP,first=(uintptr_t)item->mdl;
        if(model<first || model-first>=sizeof(item->mdl) || (model-first)%sizeof(*item->mdl) ||
           !item->mlwP->objP || !item->mlwP->owP || !item->mlwP->obj_num || item->mlwP->obj_num>64 ||
           item->mtx!=(void *)item->mtxbuf)
            return "Item motion requires bounded native model and matrix work";
        /* Fast native tracks are validated by the room loader. Other original
           formats, interpolation, remapping and linked bases remain gated. */
        if(item->mtn_md || item->hokan_count || (motion->flg&0x30) || (item->flg&0x80))
            return "Unported item motion mode, interpolation, format or linked base";
        if(!motion->datP || !motion->md2P || !motion->obj_num || motion->obj_num>item->mlwP->obj_num ||
           !motion->frm_num || motion->frm_num>32767 || item->frm_no<0 ||
           (unsigned)item->frm_no/65536>=motion->frm_num || !motion->md2P[0].p[0])
            return "Item motion frame or native track binding is invalid";
        unsigned eligible=0;
        for(unsigned i=0;i<item->mlwP->obj_num;++i)eligible+=!(item->mlwP->owP[i].flg&4);
        if(motion->obj_num>eligible)return "Item motion exceeds its original node mapping";
        int step=(int)item->mtn_add;int64_t next=(int64_t)item->frm_no+step;
        int64_t period=(int64_t)motion->frm_num*65536;
        if(step< -period || step>period || next<INT_MIN || next>INT_MAX)
            return "Item motion step exceeds a single original frame wrap";
        break;
    }
    case 0x18:
        return rx_event_audio_check(p[1]);
    case 0x26:
        if (!plp) return "Scenario equipment check requires native player work";
        break;
    case 0x0e: case 0x23: {
        if (!rom->etcp || rom->etc_n<=0 || p[4]>=(unsigned)rom->etc_n)
            return "Scenario item interaction exceeds room collision records";
        if (!sys->itwp || (*p==0x23?p[1]:rom->etcp[p[4]].prm0)>=32)
            return "Scenario item interaction exceeds native item work";
        if (*p==0x0e && (half(p+2)&0x3ff)>=sizeof(sys->it_flg)/sizeof(*sys->it_flg)*32)
            return "Scenario item interaction exceeds original pickup flags";
        break;
    }
    case 0x11: case 0x31:
        if ((unsigned)sys->ply_id>=sizeof(sys->itm)/sizeof(*sys->itm)/16)
            return "Scenario player inventory bank exceeds original item storage";
        break;
    case 0x35: {
        const LGT_WORK *lights=p[3]?rom->evlp:rom->lgtp;
        int count=p[3]?rom->evl_n:rom->lgt_n;
        if (!lights || count<=0 || p[2]>=(unsigned)count)
            return "Scenario light selector exceeds its original room/event light bank";
        break;
    }
    case 0x69:
        if(p[1]==7)for(unsigned i=0;i<31;++i)if(!isfinite(rom->grand[i]))return "Common script floor table is nonfinite";
        if(p[1]==24||p[1]==34)return bhCheckNativeRoomMotion(p,NULL);
        if(p[1]==25)return rx_player_control_motion_check(p,0);
        if(p[1]==47){
            const char *error=rx_player_control_model_check();if(error)return error;
            return bhCheckNativeActorModel(NULL);
        }
        if (p[1]==23 && p[2]>3) return "Unported original common display flag selector";
        break;
    case 0x43: {
        unsigned index=p[1];
        if (!rom->effp || rom->eff_n<=0 || index>=(unsigned)rom->eff_n ||
            index>=sizeof(sys->efid)/sizeof(*sys->efid))
            return "Scenario display references an absent room effect record";
        int slot=sys->efid[index];
        if (slot<0 || slot>=512 || eff[slot].id!=rom->effp[index].id)
            return "Scenario display requires the original room effect table to be published";
        break;
    }
    case 0x44:
        if (p[4]>=4) return "Scenario ambient light selector exceeds original room banks";
        break;
    case 0x01:
        if (!in_script(scripts, p + 2, p[1])) return "Scenario conditional target exceeds bytecode";
        break;
    case 0x02:
        if (p[1] < 2 || !in_script(scripts, p, p[1]))
            return "Scenario Else target exceeds bytecode";
        break;
    case 0x04: case 0x05:
        if ((half(p+2) & 1023) >= flag_words(p[1]) * 32)
            return "Scenario flag index exceeds its original flag bank";
        if (p[5] > (*p == 4 ? 1 : 5)) return "Invalid original scenario flag operation";
        break;
    case 0x06:
        switch (p[1]) {
        case 0: case 1: case 2: case 8: case 15: case 17:
        case 21: case 23: case 24: case 25: break;
        default: return "Unported scenario byte comparison selector";
        }
        if (p[2] > 5) return "Invalid original scenario comparison";
        break;
    case 0x07:
        if (p[2] != 5 && p[2] != 7 && p[2] != 9 && p[2] != 10)
            return "Unported scenario word comparison selector";
        if (p[3] > 5 || (p[2] == 10 && !enemy_record(p[1])))
            return "Invalid original scenario word comparison";
        break;
    case 0x08:
        switch (p[1]) {
        case 0: case 1: case 2: case 8: case 15: case 16: case 18:
        case 19: case 20: case 21: case 22: case 25: break;
        default: return "Unported scenario byte store selector";
        }
        if ((p[1] == 18 || p[1] == 22) && (unsigned char)sys->ply_id >= 4)
            return "Scenario player selector exceeds original player state";
        break;
    case 0x09:
        if (p[1] != 5 && p[1] != 9 && (p[1] < 11 || p[1] > 15))
            return "Unported scenario word store selector";
        if (p[1] >= 11 && !enemy_record(p[1] - 11))
            return "Scenario word store references an absent enemy";
        break;
    case 0x14:
        if (!rx_room_script(scripts, RX_SCRIPT_FIRST_EVENT + p[3]))
            return "Scenario start references an absent event";
        break;
    case 0x0a: case 0x0b: case 0x0c: case 0x25: {
        ATR_WORK *table = *p == 0x0a ? rom->walp : *p == 0x0c ? rom->flrp : rom->etcp;
        int count = *p == 0x0a ? rom->wal_n : *p == 0x0c ? rom->flr_n : rom->etc_n;
        if (!table || count<=0 || p[1] >= (unsigned)count) return "Scenario collision record exceeds room table";
        break;
    }
    case 0x20:
        if (!actor_record(p[2], p[1])) return "Scenario display command references an absent actor";
        break;
    case 0x24:
        if (!actor_record(p[1], p[2])) return "Scenario visibility command references an absent actor";
        break;
    case 0x2b: case 0x2e:
        if (p[1] > 1) return "Invalid original motion pause selector";
        break;
    case 0x2f: {
        BH_PWORK *actor=actor_record(1,p[1]);
        if (!actor_joint(actor,1) || !actor->mlwP->owP || p[2]>4 ||
            !sys->efm[1].objP || sys->efm[1].obj_num!=2)
            return "Original shadow setup requires native actor joints and modifier models";
        break;
    }
    case 0x2d: case 0x30: {
        BH_PWORK *actor = actor_record(1, p[1]);
        if (!companion_valid(actor)) return "Scenario motion pause references an absent actor or companion";
        if (*p == 0x30 && (!sys->rmthp || p[2] >= 10 || !sys->rmthp[p[2]].datP))
            return "Scenario motion pause references an absent room motion";
        break;
    }
    case 0x32: case 0x34: case 0x53: {
        BH_PWORK *parent = *p == 0x34 ? plp : actor_record(1, p[1]);
        BH_PWORK *child = actor_record(*p == 0x53 ? 3 : 2, p[2]);
        if (!child || !actor_joint(parent, p[3]))
            return "Scenario attachment references an absent actor or joint";
        break;
    }
    case 0x3b:
        if (p[1] > 2 || !actor_joint(actor_record(p[1], p[2]), p[3]))
            return "Scenario model visibility references an absent model node";
        break;
    case 0x65:
        if(p[1]==4){O_WRK *work;return rx_event_effect_work(p[2],&work);}
        if (!actor_record(p[1], p[2]) || p[3] >= 16)
            return "Scenario work selection references an unported actor or model part";
        break;
    case 0x5d:
        if (p[1] >= 32 || p[2] > 6) return "Invalid original scenario pad condition";
        break;
    case 0xbc:
        if (p[1] >= 16) return "Scenario kill references an absent task";
        break;
    case 0xfc:
        if (p[1] < 3 || !in_script(scripts, p, p[1]) || !in_script(scripts, p+2, 1))
            return "Scenario While target exceeds bytecode";
        break;
    }
    return NULL;
}

static const char *validate_command(const RxRoomScripts *scripts, const unsigned char *p)
{
    if (!in_script(scripts, p, 1)) return "Scenario instruction pointer exceeds bytecode";
    const Command *command = &commands[*p];
    if (!command->handler) return "Original scenario opcode is not ported yet";
    if (*p==0x67) {
        if(!in_script(scripts,p,2))return "Truncated original actor subcommand";
        if(!sub_bytes[p[1]])return "Original actor subcommand is not ported yet";
    }
    if (*p==0x64) {
        if(!in_script(scripts,p,2))return "Truncated original player subcommand";
        if(!player_bytes[p[1]])return "Original player subcommand is not ported yet";
    }
    if (*p==0x69) {
        if (!in_script(scripts,p,2)) return "Truncated original common subcommand";
        if (!common_bytes[p[1]]) return "Original common subcommand is not ported yet";
    }
    if (!in_script(scripts, p, command_bytes(p))) return "Truncated original scenario command";
    return validate_operands(scripts, p);
}

static RxEventFault fault_at(const RxRoomScripts *scripts, const unsigned char *p, const char *reason)
{
    RxEventFault fault = {.reason = reason};
    if (in_script(scripts, p, 1)) {
        fault.offset = (size_t)((uintptr_t)p - (uintptr_t)scripts->data);
        fault.opcode = *p;
    }
    return fault;
}

const char *rx_events_validate_room(RxEventFault *fault)
{
    const RxRoomScripts *scripts = rom->evtp;
    *fault = (RxEventFault){0};
    if (!scripts || !scripts->data) return NULL;
    for (size_t slot = 0; slot < scripts->slots; ++slot) {
        const unsigned char *p = rx_room_script(scripts, slot);
        if (!p) continue;
        size_t end = scripts->bytes;
        size_t offset = (size_t)(p - scripts->data);
        int effect_target=0;
        for (size_t other = 0; other < scripts->slots; ++other) {
            const unsigned char *next = rx_room_script(scripts, other);
            if (next && next > p && (size_t)(next - scripts->data) < end)
                end = (size_t)(next - scripts->data);
        }
        while (offset < end) {
            const char *error = validate_command(scripts, p);
            if(!error&&*p==0x65)effect_target=p[1]==4;
            if(!error&&effect_target&&*p==0x69&&!effect_common(p[1]))
                error="Original common effect model/motion operation is not ported yet";
            if (!error && command_bytes(p) > end - offset)
                error = "Scenario command crosses its entry boundary";
            if (error) {
                *fault = fault_at(scripts, p, error);
                return error;
            }
            offset += command_bytes(p);
            p = scripts->data + offset;
        }
    }
    return NULL;
}

void rx_events_reset(void)
{
    sys->sp_flg &= ~0x10u;
    memset(bhEtask, 0, sizeof(bhEtask));
    for (unsigned i = 0; i < 16; ++i) bhEtask[i].loop = -1;
    bhCetask = bhEtask;
    bhScePtr = G_Sp = NULL;
    bhIfelFlg = 0;
    Event_T_timer = 0;
    current_fault = (RxEventFault){0};
    instructions_left = 0;
}

static int known_task(const BH_SCEWORK *task)
{
    uintptr_t address=(uintptr_t)task,start=(uintptr_t)bhEtask;
    return address>=start && address-start<sizeof(bhEtask) && (address-start)%sizeof(*bhEtask)==0;
}

static unsigned fail_dispatch(const char *reason)
{
    if (!current_fault.reason)
        current_fault = fault_at(rom->evtp, bhScePtr, reason);
    if (known_task(bhCetask)) bhCetask->status = 0;
    bhIfelFlg = 0;
    return 0;
}

static int known_common_work(const BH_PWORK *work)
{
    if (work && work==plp) return 1;
    uintptr_t address=(uintptr_t)work;
    const struct { const void *base; size_t count,stride; } pools[]={
        {ene,128,sizeof(*ene)},{sys->obwp,32,sizeof(O_WRK)},{sys->itwp,32,sizeof(O_WRK)}};
    for (unsigned i=0;i<sizeof(pools)/sizeof(*pools);++i) {
        uintptr_t start=(uintptr_t)pools[i].base;
        if (start && address>=start && address-start<pools[i].count*pools[i].stride &&
            (address-start)%pools[i].stride==0) return 1;
    }
    return 0;
}

static const char *validate_common_work(unsigned sub)
{
    if (!known_task(bhCetask))
        return "Original common command has no native task work";
    BH_PWORK *work=bhCetask->work;
    uintptr_t address=(uintptr_t)work,base=(uintptr_t)eff;
    int effect=address>=base&&address-base<sizeof(eff);
    if(effect){
        const char *error=rx_event_effect_work_check(work);if(error)return error;
        if(!effect_common(sub))return "Original common effect model/motion operation is not ported yet";
        if(sub==7)for(unsigned i=0;i<31;++i)if(!isfinite(rom->grand[i]))return "Effect script floor table is nonfinite";
    }
    if (sub==0 || sub==5 || sub==6 || sub==12 || sub==13) return NULL;
    if (!effect&&!known_common_work(work)) return "Original common command references absent native actor work";
    if (sub==9 && (!work->mdl[0].objP || work->mdl[0].obj_num>4096 ||
                  bhCetask->model_cno>=work->mdl[0].obj_num))
        return "Original common rotation references an absent model node";
    if (sub==36 && (!work->mlwP || !work->mlwP->objP || !work->mlwP->obj_num || work->mlwP->obj_num>4096))
        return "Original common root reset references an absent native model";
    if (sub==2) {
        const float values[]={work->px,work->py,work->pz,bhCetask->addpx,bhCetask->addpy,bhCetask->addpz};
        for (unsigned i=0;i<sizeof(values)/sizeof(*values);++i)
            if (!isfinite(values[i])) return "Nonfinite original common translation state";
        if(effect&&(!isfinite(work->px+(bhCetask->bpx?-bhCetask->addpx:bhCetask->addpx))||
                    !isfinite(work->py+(bhCetask->bpy?-bhCetask->addpy:bhCetask->addpy))||
                    !isfinite(work->pz+(bhCetask->bpz?-bhCetask->addpz:bhCetask->addpz))))return "Original effect translation would overflow";
    }
    return NULL;
}

unsigned rx_event_dispatch(void)
{
    if (current_fault.reason) return 0;
    const RxRoomScripts *scripts = rom->evtp;
    const char *error = validate_command(scripts, bhScePtr);
    if (error) return fail_dispatch(error);
    if (!instructions_left) return fail_dispatch("Scenario exceeded the frame instruction budget");
    --instructions_left;
    unsigned opcode = *bhScePtr;
    if((opcode==0x19||opcode==0x1a)&&!known_task(bhCetask))return fail_dispatch("Voice command requires original native task work");
    if(opcode==0x67){
        if(!known_task(bhCetask))return fail_dispatch("Actor script requires original native task work");
        if(bhScePtr[1]!=0&&bhScePtr[1]!=139){
            if(!bhCetask->work)return fail_dispatch("Original actor control has no task target");
            error=bhCheckNativeSubControl(bhScePtr[1],bhCetask->work);if(error)return fail_dispatch(error);
        }
    }
    if(opcode==0x64){
        if(!known_task(bhCetask))return fail_dispatch("Player script requires original native task work");
        if((bhScePtr[1]==7||bhScePtr[1]==52||bhScePtr[1]==128||bhScePtr[1]==131)&&bhCetask->work!=plp)return fail_dispatch("Player script requires its published player task target");
    }
    if (opcode==0x69) {
        error=validate_common_work(bhScePtr[1]);
        if (error) return fail_dispatch(error);
        if(bhScePtr[1]==24||bhScePtr[1]==34){error=bhCheckNativeRoomMotion(bhScePtr,bhCetask->work);if(error)return fail_dispatch(error);}
        if(bhScePtr[1]==47&&bhCetask->work!=plp){
            error=bhCheckNativeActorModel(bhCetask->work);if(error)return fail_dispatch(error);
        }
        if(bhScePtr[1]==25){
            if(bhCetask->work!=plp)return fail_dispatch("Original player motion command has a foreign task target");
            error=rx_player_control_motion_check(bhScePtr,1);if(error)return fail_dispatch(error);
        }
    }
    if (opcode==0x2f) {
        BH_PWORK *actor=actor_record(1,bhScePtr[1]);
        if (!(actor->flg&0x800)) {
            unsigned i=0;
            while (i<512 && (eff[i].flg&3)) ++i;
            if (i==512) return fail_dispatch("Original shadow effect work is exhausted");
        }
    }
    if (opcode == 0x2b) {
        BH_PWORK *work = bhCetask ? bhCetask->work : NULL;
        int known = work == plp;
        for (unsigned i = 0; !known && i < 128; ++i) known = work == &ene[i];
        if (!known || !companion_valid(work))
            return fail_dispatch("Scenario motion pause has no valid player/enemy work");
    }
    int loop = bhCetask ? bhCetask->loop : -1;
    if ((opcode == 0x02 || opcode == 0x03) && bhIfelFlg <= 0)
        return fail_dispatch("Scenario conditional stack underflow");
    if (opcode == 0xf8 || opcode == 0xfa) {
        if (!bhCetask || loop < -1 || loop + 1 >= (int)(sizeof(bhCetask->cnt)/sizeof(*bhCetask->cnt)))
            return fail_dispatch("Scenario counter stack exceeds original work");
    } else if (opcode == 0xfc) {
        if (!bhCetask || loop < -1 || loop + 1 >= (int)(sizeof(bhCetask->lstack)/sizeof(*bhCetask->lstack)))
            return fail_dispatch("Scenario loop stack exceeds original work");
    } else if (opcode == 0xf9 || opcode == 0xfb) {
        if (!bhCetask || loop < 0 || loop >= (int)(sizeof(bhCetask->cnt)/sizeof(*bhCetask->cnt)))
            return fail_dispatch("Scenario counter stack underflow");
        if (opcode == 0xfb && !in_script(scripts, bhCetask->lstack[loop], 1))
            return fail_dispatch("Scenario For continuation exceeds bytecode");
    } else if (opcode == 0xf3 || opcode == 0xfd) {
        if (!bhCetask || loop < 0 || loop >= 6 ||
            !in_script(scripts, bhCetask->lcondition[loop], 1) ||
            !in_script(scripts, bhCetask->lstack[loop], 1))
            return fail_dispatch("Scenario While continuation exceeds bytecode");
    }
    unsigned result=commands[opcode].handler();
    if(opcode==0x86){error=rx_event_se_tick();if(error)return fail_dispatch(error);}
    if(opcode==0x18) {
        error=rx_event_audio_deliver();
        if(error)return fail_dispatch(error);
    }
    return result;
}

const char *rx_events_start_room(void)
{
    RxEventFault fault;
    const char *error = rx_events_validate_room(&fault);
    if (error) {
        current_fault = fault;
        return error;
    }
    rx_events_reset();
    sys->sp_flg |= 0x10;
    instructions_left = FRAME_INSTRUCTION_LIMIT;
    bhScenarioCheck(rx_room_script(rom->evtp, RX_SCRIPT_ENTRY));
    if (current_fault.reason) return current_fault.reason;
    return rx_events_run_tasks();
}

const char *rx_events_run_tasks(void)
{
    if (current_fault.reason) return current_fault.reason;
    instructions_left = FRAME_INSTRUCTION_LIMIT;
    bhEventScheduler2();
    return current_fault.reason;
}

const char *rx_events_tick(void)
{
    if (current_fault.reason) return current_fault.reason;
    instructions_left = FRAME_INSTRUCTION_LIMIT;
    bhControlEvent();
    return current_fault.reason;
}

const RxEventFault *rx_events_fault(void) { return &current_fault; }

