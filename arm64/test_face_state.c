#include "face_control_runtime.h"
#include "face_sampling.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "main.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern BH_SCEWORK bhEtask[16];extern unsigned Event_T_timer;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL original facial state: %s\n",why);exit(1);}}
static void command(const unsigned char *code,size_t bytes,int reject)
{
    unsigned char data[32]={8,0,0,0};data[4]=(unsigned char)(8+bytes);memcpy(data+8,code,bytes);
    RxRoomScripts scripts;check(!rx_room_scripts_open(data,10+bytes,&scripts),"command fixture");
    void *old=rom->evtp;rom->evtp=&scripts;
    int32_t before[2][RX_FACE_CONTROL_WORDS];for(unsigned i=0;i<2;++i)memcpy(before[i],ene[rom->enep[i].wrk_no].exp0,sizeof(before[i]));
    Event_T_timer=123;bhEtask[15].status=1;const char *error=rx_events_start_room();
    if(reject){check(error&&Event_T_timer==123&&bhEtask[15].status==1,"bad facial command changed scheduler");
        for(unsigned i=0;i<2;++i)check(!memcmp(before[i],ene[rom->enep[i].wrk_no].exp0,sizeof(before[i])),"bad facial command changed scalar state");}
    else check(!error,error);
    rx_events_reset();rom->evtp=old;
}
static void op(unsigned opcode,unsigned index,unsigned action)
{unsigned char c[4]={(unsigned char)opcode,(unsigned char)index,(unsigned char)action,0};
    if(opcode==0x3e||opcode==0x3f){c[1]=0;c[2]=(unsigned char)index;c[3]=(unsigned char)action;}
    command(c,opcode==0x90?2:4,0);}
static void select_clip(unsigned opcode,unsigned index,unsigned clip,unsigned group)
{unsigned char c[]={opcode,group,index,clip};command(c,4,0);}
static unsigned ticks;
static void tick(BH_PWORK *a)
{
    ML_WORK *before=a->mlwP;const char *error=rx_face_control_tick(a);check(!error,error);
    check(a->mlwP==before,"source controller left actor bound to staged model");
    const int32_t *w=(const int32_t *)a->exp0;check(!w[265]&&!w[266]&&!w[267]&&!w[268],"native controller wrote packed pointer words");ticks++;
}
static void expect(BH_PWORK *a,const RxFaceClip *clip,unsigned frame)
{
    PARAM_WORK p;check(!rx_face_sample(clip,a->id,(float)frame,&p),"independent sampled clip");
    check(!memcmp(&((MASK_WORK *)a->exp1)->param,&p,160),"original controller sampled the wrong expression frame");
    const MASK_WORK *fm=(MASK_WORK *)a->exp1;const ML_WORK *m=rx_face_control_model(a);
    check(m->objP[7].ang[0]==fm->toothsrc->ang[0]&&m->objP[11].ang[0]==fm->tangorg->ang[0]&&
          m->objP[9].ang[0]==-fm->eyesrc[0]->ang[0]&&m->objP[9].ang[1]==-fm->eyesrc[0]->ang[1]&&
          m->objP[13].ang[0]==-fm->eyesrc[1]->ang[0]&&m->objP[13].ang[1]==-fm->eyesrc[1]->ang[1],"original flat facial-angle copies differ");
}
void rx_check_face_state(size_t mouth_bytes,size_t lip_bytes)
{
    unsigned old_gm=sys->gm_flg;int old_now=sys->loop_now,old_count=sys->loop_ct;
    unsigned old_flags[2]={ene[rom->enep[0].wrk_no].flg2,ene[rom->enep[1].wrk_no].flg2};
    unsigned progress[32];_Static_assert(sizeof(progress)==sizeof(sys->ev_flg),"progress snapshot size");memcpy(progress,sys->ev_flg,sizeof(progress));
    sys->gm_flg&=~0x200u;sys->loop_ct=1;sys->loop_now=0;
    const RxRoomScripts *room=rom->evtp;check(room->bytes>1750&&room->data[1746]==0x3c,"retail MaskSet fixture");
    command(room->data+1746,4,0);BH_PWORK *retail=&ene[rom->enep[1].wrk_no];tick(retail);
    check(((MASK_WORK *)retail->exp1)->fnum==1,"actual opening MaskSet did not activate native clip zero");
    for(unsigned index=0;index<2;++index){sys->gm_flg&=~0x200u;BH_PWORK *a=&ene[rom->enep[index].wrk_no];
        check(!rx_face_control_ready(a)&&(uintptr_t)a->exp1>UINT32_MAX&&(uintptr_t)a->exp2>UINT32_MAX,"native facial context identity/high addresses");
        int32_t *w=(int32_t *)a->exp0;MASK_WORK *fm=(MASK_WORK *)a->exp1;LIP_WORK *lip=(LIP_WORK *)a->exp2;
        const PARAM_WORK *base=rx_face_control_base(a);unsigned base_count=(unsigned)w[8];
        check(fm->head!=(HEAD_WORK *)a->mskp&&((HEAD_WORK *)a->mskp)->nFace==base_count,"native controller header must be separately owned");
        for(unsigned clip=0;clip<20;++clip){select_clip(0x3c,index,clip,clip*11);op(0x3f,index,1);op(0x9a,index,1);op(0x8f,index,1);op(0x3e,index,0);
            RxFaceClip view;check(!rx_face_clip(sys->mspp,mouth_bytes,clip,&view),"all original expression selections");tick(a);expect(a,&view,0);
            check(fm->face==view.keys&&fm->fnum==(int)view.count&&fm->head->nFace==view.count&&w[8]==(int)base_count,"native expression activation/count isolation");}
        select_clip(0x3c,index,2,0);op(0x9a,index,0);op(0x3e,index,0);RxFaceClip active;
        check(!rx_face_clip(sys->mspp,mouth_bytes,2,&active),"loop expression view");
        for(unsigned i=0;i<35;++i)tick(a);check(w[6]==35&&(w[0]&3)==3,"original expression frame progression");expect(a,&active,34);
        op(0x8f,index,0);for(unsigned i=0;i<4;++i)tick(a);check(w[6]==35&&(w[0]&16),"original face pause did not hold frame");expect(a,&active,35);
        op(0x8f,index,1);tick(a);check(w[6]==36,"original face resume");
        for(unsigned i=0;i<34;++i)tick(a);check(w[6]==0&&(w[0]&1),"original repeat did not wrap");expect(a,&active,69);
        op(0x9a,index,1);for(unsigned i=0;i<70;++i)tick(a);check(w[6]==69&&!(w[0]&1),"original one-shot endpoint differs");
        op(0x90,index,0);tick(a);check(fm->face==base&&fm->fnum==(int)base_count&&w[6]==0&&w[8]==(int)base_count,"original base reset");tick(a);
        RxFaceClip neutral={base,base_count,0};expect(a,&neutral,0);
        for(unsigned sequence=0;sequence<19;++sequence){unsigned group=sequence%(base_count/6);RxLipClip view;LIP_WORK reference;
            check(!rx_lip_clip(sys->lspp,lip_bytes,sequence,&view)&&!rx_lip_begin(&view,&reference),"original lip sequence view");
            select_clip(0x3d,index,sequence,group);op(0x3f,index,0);
            for(unsigned i=0;reference.flag&&i<10000;++i){PARAM_WORK expected;check(!rx_face_sample(&neutral,a->id,0,&expected)&&!rx_lip_sample(&view,base+group*6,&reference,&expected),"independent lip sample");
                tick(a);check(!memcmp(&fm->param,&expected,160)&&lip->ptr==reference.ptr&&lip->cnt==reference.cnt&&lip->time==reference.time&&lip->cur==reference.cur&&lip->next==reference.next&&lip->flag==reference.flag,"original lip timing/phoneme blend differs");}
            check(!reference.flag&&!(w[0]&4),"original lip completion did not clear start flag");}
        select_clip(0x3d,index,2,0);op(0x3f,index,0);tick(a);unsigned char *playing=lip->top;
        select_clip(0x3d,index,1,0);tick(a);check(lip->top==playing&&w[3]==1,"selecting a new lip sequence interrupted the active one");
        op(0x3f,index,1);tick(a);op(0x3f,index,0);tick(a);check(lip->top==rx_face_control_lip_data(a),"stopped/restarted lip sequence did not use new selection");
        op(0x3f,index,1);op(0x90,index,0);tick(a);tick(a);
        size_t geometry=68+(size_t)fm->head->nAttr*32;unsigned char *snapshot=malloc(geometry);check(snapshot!=NULL,"throttle geometry snapshot");
        memcpy(snapshot,fm->dst->vlist,geometry);sys->loop_ct=2;sys->loop_now=0;select_clip(0x3c,index,4,0);tick(a);
        check(!memcmp(snapshot,fm->dst->vlist,geometry),"original last-loop geometry gate ignored");sys->loop_now=1;tick(a);
        check(memcmp(snapshot,fm->dst->vlist,geometry)!=0,"last-loop facial deformation did not run");tick(a);check(a->flg2&0x80,"original unchanged-face cache did not freeze");
        sys->gm_flg|=0x200;tick(a);check(!(a->flg2&0x80),"original game flag did not override freeze");sys->loop_ct=1;sys->loop_now=0;
        op(0x90,index,0);tick(a);tick(a);
        int32_t saved_words[RX_FACE_CONTROL_WORDS];memcpy(saved_words,w,sizeof(saved_words));MASK_WORK saved_work=*fm;HEAD_WORK saved_head=*fm->head;LIP_WORK saved_lip=*lip;
        NJS_CNK_OBJECT nodes[31];const ML_WORK *flat=rx_face_control_model(a);memcpy(nodes,flat->objP,sizeof(nodes));unsigned flags=a->flg2;
        memcpy(snapshot,fm->dst->vlist,geometry);float old_muscle=base[0].muscle[0];((PARAM_WORK *)base)[0].muscle[0]=NAN;
        check(rx_face_control_tick(a)&&!memcmp(saved_words,w,sizeof(saved_words))&&!memcmp(&saved_work,fm,sizeof(saved_work))&&!memcmp(&saved_head,fm->head,sizeof(saved_head))&&
              !memcmp(&saved_lip,lip,sizeof(saved_lip))&&!memcmp(snapshot,fm->dst->vlist,geometry)&&!memcmp(nodes,flat->objP,sizeof(nodes))&&a->flg2==flags,"failed geometry committed partial controller state");
        ((PARAM_WORK *)base)[0].muscle[0]=old_muscle;
        for(unsigned ignored=0;ignored<256;++ignored)select_clip(0x3c,index,0,ignored);
        for(unsigned bad=20;bad<256;++bad){unsigned char c[]={0x3c,0,index,bad};command(c,4,1);}
        for(unsigned bad=19;bad<256;++bad){unsigned char c[]={0x3d,0,index,bad};command(c,4,1);}
        for(unsigned group=base_count/6;group<256;++group){unsigned char c[]={0x3d,group,index,0};command(c,4,1);}
        unsigned char *exp=a->exp1;a->exp1=(unsigned char *)(uintptr_t)1;command((unsigned char[]){0x3c,0,index,0},4,1);a->exp1=exp;
        exp=a->exp2;a->exp2=(unsigned char *)(uintptr_t)1;command((unsigned char[]){0x3d,0,index,0},4,1);a->exp2=exp;
        unsigned char *bank=sys->mspp;sys->mspp=NULL;command((unsigned char[]){0x3c,0,index,0},4,1);sys->mspp=bank;
        NJS_CNK_OBJECT *eye=fm->eyesrc[0];fm->eyesrc[0]=(NJS_CNK_OBJECT *)(uintptr_t)1;check(rx_face_control_tick(a)!=NULL,"foreign eye pointer accepted");fm->eyesrc[0]=eye;
        w[265]=1;check(rx_face_control_tick(a)!=NULL,"packed pointer contamination accepted");w[265]=0;
        for(unsigned opcode=0x3c;opcode<=0x3d;++opcode)for(unsigned length=1;length<4;++length)command((unsigned char[]){opcode,0,index,0},length,1);
        op(0x90,index,0);tick(a);tick(a);free(snapshot);a->flg2=old_flags[index];
    }
    check(!memcmp(progress,sys->ev_flg,sizeof(progress)),"native facial controller forced campaign progression flags");
    sys->gm_flg=old_gm;sys->loop_ct=old_count;sys->loop_now=old_now;
    printf("PASS original native facial control: both room actors, 20 clips/19 lip streams/all groups, %u ticks; source pause/repeat/one-shot/reset/freeze/loop gates, pending selection, typed high pointers, immutable header, rollback, real MaskSet bytes and malformed/foreign rejection\n",ticks);
}
