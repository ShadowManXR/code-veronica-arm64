#include "player_control_runtime.h"
#include "main.h"
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include "pl_evt.h"
#include "MdlPut.h"
#include "room.h"
#include "pwksub.h"
#include "event_audio.h"
#include "sdfunc.h"
#include "ps2/vu0.h"
static unsigned *script_flags;static int script_preview;static const char *script_error;
static BH_PWORK *actor;
static EXP_WORK *work;
static RxEnginePlayer *model_owner;static RxEngineWeapon *hand_owner;static MN_WORK *bank;
_Static_assert(sizeof(EXP_WORK)==124&&offsetof(EXP_WORK,spx)==72&&offsetof(EXP_WORK,spz)==80,"Original scalar player layout changed");
const char *rx_player_control_bind(BH_PWORK *player,EXP_WORK *owned,size_t bytes)
{
    if(!player||player!=plp||!owned||bytes!=sizeof(*owned)||(uintptr_t)owned%_Alignof(EXP_WORK))return "Invalid native player scalar work";
    if((actor&&(actor!=player||work!=owned))||(player->exp0&&player->exp0!=(unsigned char *)owned))return "Player script would replace occupied scalar work";
    actor=player;work=owned;player->exp0=(unsigned char *)owned;return NULL;
}
const char *rx_player_control_check(void)
{
    if(!actor||actor!=plp||!work||actor->exp0!=(unsigned char *)work)return "Player script requires its owned native scalar work";
    return NULL;
}
void rx_player_control_reset(void)
{
    if(actor&&actor->exp0==(unsigned char *)work)actor->exp0=NULL;
    if(bank&&sys->plmthp==(unsigned char *)bank)sys->plmthp=NULL;
    if(model_owner&&sys->plzmtp==rx_engine_player_translation_bank(model_owner))sys->plzmtp=NULL;
    script_flags=NULL;actor=NULL;work=NULL;model_owner=NULL;hand_owner=NULL;bank=NULL;
}

const char *rx_player_control_bind_models(RxEnginePlayer *player,RxEngineWeapon *weapon)
{
    const char *error=rx_player_control_check();if(error)return error;
    MN_WORK *native=rx_engine_player_motion_bank(player);
    unsigned char *translations=rx_engine_player_translation_bank(player);
    if(!native||actor->mnwPb!=native||(sys->plmthp&&sys->plmthp!=(unsigned char *)native))return "Player script would replace an occupied native motion bank";
    if(!translations||(sys->plzmtp&&sys->plzmtp!=translations))return "Player script would replace an occupied native translation bank";
    model_owner=player;hand_owner=weapon;bank=native;sys->plmthp=(unsigned char *)bank;sys->plzmtp=translations;return NULL;
}
const char *rx_player_control_motion_check(const unsigned char *p,int runtime)
{
    const char *error=rx_player_control_check();if(error)return error;
    if(p[3]!=0)return "Original common motion bank/actor mode is not ported yet";
    if(p[7]!=6)return "Original common motion interpolation is not ported yet";
    if(!bank||sys->plmthp!=(unsigned char *)bank)return "Original common motion requires its native player bank";
    return rx_engine_player_script_motion_check(model_owner,hand_owner,actor,p[4],p[6],runtime);
}

const char *rx_player_control_model_check(void)
{
    const char *error=rx_player_control_check();if(error)return error;
    if(!bank||sys->plmthp!=(unsigned char *)bank)return "Original model recalculation requires its native player context";
    return rx_engine_player_script_motion_check(model_owner,hand_owner,actor,actor->mtn_no,3,0);
}

const char *rx_player_control_stand_check(void)
{
    const char *error=rx_player_control_check();if(error)return error;
    if(work->wpntp<0||work->wpntp>=2)return "Original standing motion has an invalid weapon group";
    if(!bank||sys->plmthp!=(unsigned char *)bank)return "Original standing motion requires its native player bank";
    unsigned level=actor->hp>=120?((actor->stflg&0x280000)!=0):actor->hp>=30?1:2;
    return rx_engine_player_script_motion_check(model_owner,hand_owner,actor,PlMtnAct[work->wpntp][level][0],3,0);
}

const char *rx_player_control_handoff_check(void)
{
    const char *error=rx_player_control_check();if(error)return error;
    if(!bank||sys->plmthp!=(unsigned char *)bank||actor->mnwPb!=bank)
        return "Original player handoff requires its native motion bank";
    return NULL;
}

extern void (*bhCPM0_event_tbl[10])(void);
extern NJS_POINT3 PlayerPos;
const char *rx_player_control_bind_script_flags(unsigned *owned)
{
    const char *error=rx_player_control_check();if(error)return error;
    if(!owned||(uintptr_t)owned%_Alignof(unsigned)||actor->exp1!=(unsigned char*)owned||(script_flags&&script_flags!=owned))return "Foreign original player script flags";
    script_flags=owned;return NULL;
}
void rx_player_script_footstep(int floor,int type,int flag)
{
    unsigned char command[]={0x86,1,0,64,64,128,128,0,floor,0,0,0,type,0,0,0};
    if(floor<0||floor>=5||type<0||type>1||flag!=1){script_error="Unsupported original player footstep";return;}
    const char *error=rx_event_se_check(command);if(!error)error=rx_event_step_position_check(&PlayerPos);
    if(error){script_error=error;return;}
    if(!script_preview)CallPlayerFootStepSe(floor,type,flag);
}
static int normal_mode_supported(void)
{
    if(actor->mode1)return 0;
    if(actor->mode2==13)return actor->mode3<=2;
    if(actor->mode2==6)return actor->mode3<=1;
    return actor->mode2<=5&&actor->mode3<=1&&!(actor->flg&0x10000);
}
static const char *idle_gesture_clips_check(void)
{
    unsigned slot=PlMtnAct[work->wpntp][work->dmlvl][6];
    const char *error=rx_engine_player_script_motion_check(model_owner,hand_owner,actor,slot,3,0);if(error)return error;
    return rx_engine_player_script_motion_check(model_owner,hand_owner,actor,slot+1,3,0);
}
static const char *normal_walk_check(void)
{
    unsigned pad=sys->pad_on&15;
    if((actor->flg&0x10000)||(pad!=1&&pad!=5&&pad!=9))return NULL;
    const char *error=bhCheckNativePlayerWalkWalls();if(error)return error;
    if(!isfinite(actor->ar)||actor->ar<=0||actor->ar>64||!isfinite(actor->ah)||actor->ah<=0||actor->ah>64)
        return "Invalid native walking collision bounds";
    unsigned level=(unsigned)work->dmlvl,slot=PlMtnAct[work->wpntp][level][2];
    error=rx_engine_player_script_motion_check(model_owner,hand_owner,actor,slot,3,0);if(error)return error;
    unsigned next_level=actor->hp>=120?((actor->stflg&0x280000)!=0):actor->hp>=30?1:2;
    error=rx_engine_player_script_motion_check(model_owner,hand_owner,actor,PlMtnAct[work->wpntp][next_level][2],3,0);if(error)return error;
    if(PlyTrsZ[0][level]!=(int)level)return "Foreign original walking translation selector";
    unsigned state=(sys->pad_old&1)?actor->mode3:0;
    if(state>1)return "Original walking entry requires its supported source state";
    int frame=actor->frm_no;
    if(!state)frame=(actor->flg&0x800000)?bhGetFrameNum(bank[actor->mtn_no].frm_num,bank[slot].frm_num,actor->frm_no):5*65536;
    if(frame<0||(unsigned)frame/65536>=bank[slot].frm_num)return "Original walking entry exceeds its owned motion clip";
    return rx_engine_player_walk_translation_check(model_owner,level,(unsigned)frame/65536);
}
static const char *normal_run_check(void)
{
    const char *error=bhCheckNativePlayerWalkWalls();if(error)return error;
    if(!isfinite(actor->ar)||actor->ar<=0||actor->ar>64||!isfinite(actor->ah)||actor->ah<=0||actor->ah>64)
        return "Invalid native running collision bounds";
    unsigned level=(unsigned)work->dmlvl,slot=PlMtnAct[work->wpntp][level][3];
    error=rx_engine_player_script_motion_check(model_owner,hand_owner,actor,slot,3,0);if(error)return error;
    unsigned next_level=actor->hp>=120?((actor->stflg&0x280000)!=0):actor->hp>=30?1:2;
    error=rx_engine_player_script_motion_check(model_owner,hand_owner,actor,PlMtnAct[work->wpntp][next_level][3],3,0);if(error)return error;
    if(PlyTrsZ[3][level]!=(int)(3+level))return "Foreign original running translation selector";
    int frame=actor->frm_no;
    if(!actor->mode3)frame=bhGetFrameNum(bank[actor->mtn_no].frm_num,bank[slot].frm_num,actor->frm_no);
    if(frame<0||(unsigned)frame/65536>=bank[slot].frm_num)return "Original running entry exceeds its owned motion clip";
    return rx_engine_player_locomotion_translation_check(model_owner,3+level,(unsigned)frame/65536);
}
static const char *script_state_check(void)
{
    const char *error=rx_player_control_check();if(error)return error;
    if(!model_owner||!bank||actor->mnwP!=bank||actor->mnwPb!=bank||sys->plmthp!=(unsigned char*)bank||
       !script_flags||actor->exp1!=(unsigned char*)script_flags)return "Scripted player tick requires its owned native context";
    if(sys->ply_id!=0||actor->wpnr_no||actor->wpnl_no||actor->hp<=0)return "Native scripted player currently requires living unarmed Claire";
    if(work->wpntp<0||work->wpntp>=2||work->dmlvl<0||work->dmlvl>=3||!isfinite(work->rtspd)||fabsf(work->rtspd)>16)
        return "Invalid original scripted player weapon/health/turn state";
    if(actor->mode0==1){
        unsigned pad=sys->pad_on&15;
        int gesture_locked=actor->mode2==13&&(actor->flg&0x10000);
        int run_locked=actor->mode2==6&&(actor->flg&0x10000);
        if(!normal_mode_supported()||
           (!gesture_locked&&!run_locked&&pad!=0&&pad!=4&&pad!=8&&pad!=1&&pad!=5&&pad!=9)||(sys->pad_on&0x10)||(sys->pad_ps&0x200)||
           (sys->gm_flg&0x1000040)||actor->ct3<0||actor->ct3==INT_MAX||
           (actor->flg2&8)||(actor->stflg&0x10080)||!isfinite(actor->gpx)||!isfinite(actor->gpz))
            return "Original normal player action/interaction mode is not ported yet";
        if(actor->mode2==13){error=idle_gesture_clips_check();if(error)return error;}
    }else if(actor->mode0==7){
        unsigned m=actor->mode2;
        if((m!=0&&m!=1&&m!=2&&m!=5&&m!=8)||!bhCPM0_event_tbl[m])return "Original scripted player controller is not ported yet";
        void (*expected)(void)=m==0?pl_smove00:m==1?pl_smove01:m==2?pl_smove02:m==5?pl_smove05:pl_smove08;
        if(bhCPM0_event_tbl[m]!=expected)return "Foreign original scripted player callback";
        if((m==1||m==2)&&(actor->mode1>2||actor->mode3>1))return "Unsupported original scripted locomotion state";
        if(m==0&&actor->mode3>2)return "Unsupported original scripted seated state";
        if(m==5&&(actor->mode1>=32||actor->mode3>4||actor->ct0<0||actor->ct0>2))return "Invalid original basic scripted motion";
        if(m==8&&(actor->mode3>4||actor->ct0<0||actor->ct0>2))return "Invalid original explicit scripted motion";
    }else if(actor->mode0!=8||actor->mode3>1)return "Native continuous player tick only admits original scripted states";
    if(actor->stflg&0x381030||actor->flg&0x24001080u)return "Original scripted player water/stair/link/knockback mode is not ported yet";
    const float values[]={work->bpx,work->bpy,work->bpz,work->nlxb,work->nlyb,work->nlzb,work->spx,work->spz,work->spxn,work->spzn,work->arn,work->arp};
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i)if(!isfinite(values[i])||fabsf(values[i])>1000000)return "Invalid original scripted player scalar position";
    error=rx_engine_player_tick_check(model_owner,hand_owner,actor);if(error)return error;
    error=bhCheckNativePlayerFloor();if(error)return error;
    if(actor->mode0!=1)return NULL;
    if(actor->mode2==6&&(actor->flg&0x10000))return normal_run_check();
    return normal_walk_check();
}
const char *rx_player_control_script_check(void)
{
    if(!plp)return "Missing native scripted player";
    if(!(sys->sp_flg&1)||(plp->stflg&0x1000000))return NULL;
    const char *error=script_state_check();if(error)return error;
    BH_PWORK before=*actor;EXP_WORK scalar=*work;unsigned flags=*script_flags;VuState vu=g_vu0;
    unsigned system_flags=sys->st_flg,pad_on=sys->pad_on,pad_ps=sys->pad_ps,pad_old=sys->pad_old;
    script_preview=1;script_error=NULL;
    if(actor->mode0==1)bhCPM0_action();else if(actor->mode0==7)bhCPM0_event();else bhCPM0_nothing();
    error=script_error;
    if(!error&&actor->mode0==1&&!normal_mode_supported())error="Original normal player continuation is not ported yet";
    if(!error)error=rx_engine_player_tick_check(model_owner,hand_owner,actor);
    *actor=before;*work=scalar;*script_flags=flags;sys->st_flg=system_flags;
    sys->pad_on=pad_on;sys->pad_ps=pad_ps;sys->pad_old=pad_old;
    g_vu0=vu;script_preview=0;script_error=NULL;
    return error;
}
const char *rx_player_control_script_tick(void)
{
    const char *error=rx_player_control_script_check();if(error)return error;
    if(!(sys->sp_flg&1)||(plp->stflg&0x1000000))return NULL;
    bhBackupScriptPlayer();
    if(actor->mode0==1)bhCPM0_action();else if(actor->mode0==7)bhCPM0_event();else bhCPM0_nothing();
    if(script_error)return script_error;
    bhAdvanceScriptPlayerMotion();bhFixScriptPlayerPosition();bhUpdateScriptPlayerBody();
    bhCollideScriptPlayer();bhSaveScriptPlayerPosition();bhCalcModel(actor);bhPublishScriptPlayerBounds();
    return rx_engine_player_publish(model_owner,actor);
}

const char *rx_player_control_place_room(void)
{
    const char *error=rx_player_control_check();if(!error)error=bhCheckNativePlayerSpawn();if(error)return error;
    bhPlaceRoomPlayer();return NULL;
}
