#include "event_runtime.h"
#include "room_scripts.h"
#include "types.h"
#include "main.h"
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL FrameCheck: %s\n",why);exit(1);}}
static unsigned long cases;
static void predicate(void (*run)(const unsigned char*,size_t),unsigned type,unsigned index,unsigned half,unsigned frame,int current)
{
    unsigned char code[]={0x38,type,index,half,frame,frame>>8,5,1,30,0,0,0,0,0};
    int64_t boundary=(int64_t)frame*65536+(half?32768:0);if(boundary>=INT64_C(2147483648))boundary-=INT64_C(4294967296);
    run(code,sizeof(code));check(!!(sys->ev_flg[0]&2)==((int64_t)current<boundary),"signed 16.16 frame boundary/return differs");++cases;
}
static void reject(const unsigned char *command,size_t bytes)
{
    unsigned char code[32]={8,0,0,0};code[4]=8+6+bytes;
    const unsigned char flag[]={5,1,30,0,0,0};memcpy(code+8,flag,6);memcpy(code+14,command,bytes);
    RxRoomScripts script;check(!rx_room_scripts_open(code,16+bytes,&script),"rejection fixture");void *old=rom->evtp;rom->evtp=&script;
    unsigned value=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"invalid FrameCheck executed its prefix");sys->ev_flg[0]=value;rx_events_reset();rom->evtp=old;
}
void rx_check_frame_check(void (*run)(const unsigned char*,size_t))
{
    BH_PWORK saved=*plp;ETTY_WORK *old_enemies=rom->enep;int old_count=rom->ene_n;O_WRK *old_objects=sys->obwp;
    static ETTY_WORK mapping[128];static O_WRK objects[32];static BH_PWORK enemies[128];memcpy(enemies,ene,sizeof(enemies));
    rom->enep=mapping;rom->ene_n=128;sys->obwp=objects;
    /* Full encoded threshold range, including signed wrap, exact equality and
       both adjacent fixed-point values. No float conversion in the oracle. */
    for(unsigned frame=0;frame<65536;++frame)for(unsigned half=0;half<2;++half){
        int64_t boundary=(int64_t)frame*65536+(half?32768:0);if(boundary>=INT64_C(2147483648))boundary-=INT64_C(4294967296);
        for(int delta=-1;delta<=1;++delta){int64_t current=boundary+delta;if(current<INT_MIN||current>INT_MAX)continue;plp->frm_no=(int)current;predicate(run,0,255,half,frame,plp->frm_no);}
    }
    for(unsigned i=0;i<128;++i){mapping[i].wrk_no=127-i;ene[127-i].frm_no=i*65536+32767;predicate(run,1,i,1,i,ene[127-i].frm_no);ene[127-i].frm_no++;predicate(run,1,i,1,i,ene[127-i].frm_no);}
    for(unsigned i=0;i<32;++i){objects[i].frm_no=(int)i*65536;predicate(run,2,i,0,i,objects[i].frm_no);predicate(run,2,i,1,i,objects[i].frm_no);}
    for(unsigned half=0;half<256;++half){plp->frm_no=32767;predicate(run,0,half,half,0,plp->frm_no);}
    for(unsigned i=3;i<256;++i)reject((unsigned char[]){0x38,i,0,0,0,0},6);
    for(unsigned i=32;i<256;++i)reject((unsigned char[]){0x38,2,i,0,0,0},6);
    mapping[0].wrk_no=128;reject((unsigned char[]){0x38,1,0,0,0,0},6);mapping[0].wrk_no=127;
    rom->ene_n=0;reject((unsigned char[]){0x38,1,0,0,0,0},6);rom->ene_n=128;
    rom->enep=NULL;reject((unsigned char[]){0x38,1,0,0,0,0},6);rom->enep=mapping;
    sys->obwp=NULL;reject((unsigned char[]){0x38,2,0,0,0,0},6);sys->obwp=objects;
    BH_PWORK *player=plp;plp=NULL;reject((unsigned char[]){0x38,0,0,0,0,0},6);plp=player;
    for(unsigned length=1;length<6;++length)reject((unsigned char[]){0x38,0,0,0,0,0},length);
    for(unsigned i=0;i<128;++i){BH_PWORK actual=ene[i];actual.frm_no=enemies[i].frm_no;check(!memcmp(&actual,enemies+i,sizeof(actual)),"frame predicate mutated unrelated enemy work");}
    BH_PWORK actual_player=*plp;actual_player.frm_no=saved.frm_no;check(!memcmp(&actual_player,&saved,sizeof(saved)),"frame predicate mutated unrelated player work");
    for(unsigned i=0;i<32;++i){O_WRK actual=objects[i],zero={0};actual.frm_no=0;check(!memcmp(&actual,&zero,sizeof(actual)),"frame predicate mutated unrelated object work");}
    CAM_WORK old_camera=cam;
    const unsigned modes[]={0,1,3,255};
    for(unsigned m=0;m<4;++m)for(unsigned value=0;value<256;++value){
        memset(&cam,0xa5,sizeof(cam));cam.mode0=(unsigned char)modes[m];CAM_WORK wanted=cam;
        if(value==0)wanted.mode0=3;else if(value==1)wanted.mode0=1;
        run((unsigned char[]){0x29,value,5,1,30,0,0,0,0,0},10);
        check(!memcmp(&cam,&wanted,sizeof(cam))&&(sys->ev_flg[0]&2),"original camera pause/resume/default selector changed unrelated camera work");
    }
    cam=old_camera;reject((unsigned char[]){0x29},1);
    puts("PASS 1024 original CamPauseSet states: pause/resume modes, every selector including original no-ops, full camera preservation and truncated rejection");
    *plp=saved;memcpy(ene,enemies,sizeof(enemies));rom->enep=old_enemies;rom->ene_n=old_count;sys->obwp=old_objects;
    printf("PASS %lu original FrameCheck predicates: all signed 16.16 thresholds/half-frames, exact and adjacent boundaries, 128 remapped actors/32 objects, selector semantics and atomic malformed-input rejection\n",cases);
}

