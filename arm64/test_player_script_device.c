#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "player_control_runtime.h"
#include "android_egl.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <GLES3/gl3.h>
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL native player script: %s\n",why);exit(1);}}
static unsigned char data[64];static RxRoomScripts scripts;
static void execute(const unsigned char command[8]){
 memset(data,0,sizeof(data));data[0]=8;data[4]=28;
 const unsigned char prefix[]={0x65,0,0,0,0x64,12,0,0,0,255};memcpy(data+8,prefix,10);memcpy(data+18,command,8);
 check(!rx_room_scripts_open(data,30,&scripts),"actual player command fixture");rom->evtp=&scripts;const char *error=rx_events_start_room();check(!error,error);
}
static unsigned char picture[640*480*4],reference[sizeof(picture)],hidden[sizeof(picture)];
static void capture(RxOpening *opening,unsigned char *out){
 for(unsigned i=0;i<512;++i)if((eff[i].flg&1)&&eff[i].id==9&&eff[i].mode0==1)eff[i].ct0=0;
 glViewport(0,0,640,480);const char *error=rx_opening_present(opening);check(!error,error);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,out);check(!glGetError(),"player position GPU readback");
}
int main(int argc,char **argv){
 check(argc==2,"supply original ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};RxEgl egl={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"complete opening must stay gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next original dependency");
 check(!rx_player_control_check()&&(uintptr_t)plp->exp0>UINT32_MAX,"owned native player scalar binding");
 const RxRoomScripts *original=rom->evtp;unsigned flags[sizeof(sys->ev_flg)/sizeof(*sys->ev_flg)];memcpy(flags,sys->ev_flg,sizeof(flags));
 BH_PWORK saved=*plp;EXP_WORK scalar=opening.player_work;unsigned offsets[]={2182,6518,7180};
 for(unsigned i=0;i<3;++i){check(original->bytes>offsets[i]+8&&original->data[offsets[i]]==0x64&&original->data[offsets[i]+1]==7,"actual original player position bytes");
  const unsigned char *p=original->data+offsets[i];execute(p);float x=(p[2]|p[3]<<8)/100.0f,y=(p[4]|p[5]<<8)/100.0f,z=(p[6]|p[7]<<8)/100.0f;
  check(plp->px==x&&plp->py==y&&plp->pz==z&&plp->pxb==x&&plp->pyb==y&&plp->pzb==z&&opening.player_work.spx==x&&opening.player_work.spz==z,"actual disc position/scalar backups");
 }
 /* The same original bytes must feed the real GLES path identically to direct
    native position state, without executing any campaign continuation. */
 check(rx_egl_open(&egl,NULL,640,480),"player script EGL");sys->sp_flg&=~0x80u;
 execute(original->data+2182);error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_IDLE,0,0);check(!error,error);capture(&opening,hidden);capture(&opening,picture);
 float x=plp->px,y=plp->py,z=plp->pz;*plp=saved;opening.player_work=scalar;plp->mode0=7;plp->mode2=0;plp->stflg=(plp->stflg|0x10000)&~0x80u;plp->flg|=0x10000;plp->px=plp->pxb=x;plp->py=plp->pyb=y;plp->pz=plp->pzb=z;opening.player_work.spx=x;opening.player_work.spz=z;
 error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_IDLE,0,0);check(!error,error);capture(&opening,reference);
 check(!memcmp(picture,reference,sizeof(picture)),"scripted player GPU frame differs from independent native world position");
 plp->stflg|=0x1000000;capture(&opening,hidden);size_t changed=0;for(size_t i=0;i<sizeof(picture);i+=4)changed+=memcmp(reference+i,hidden+i,4)!=0;check(changed>100,"scripted player was not visible in GPU comparison");
 printf("PASS actual original player position bytes (three sites), typed scalar backups and byte-identical GLES reference; %zu visible player pixels\n",changed);
 *plp=saved;opening.player_work=scalar;
 /* A valid task whose target is retired/reassigned must fail before the
    position handler can touch either target or player scalar storage. */
 unsigned char waiting[41]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0,0x65,0,0,0,0xfe};memcpy(waiting+25,original->data+2182,8);waiting[33]=5;waiting[34]=1;waiting[35]=30;waiting[39]=0xff;
 RxRoomScripts bad;check(!rx_room_scripts_open(waiting,sizeof(waiting),&bad),"wrong target fixture");rom->evtp=&bad;check(!rx_events_start_room(),"start delayed player fixture");
 BH_PWORK before=*plp;EXP_WORK old=opening.player_work;bhEtask[0].work=(BH_PWORK *)(uintptr_t)1;unsigned char mode=bhEtask[0].mode0;
 check(rx_events_tick()!=NULL&&!memcmp(&before,plp,sizeof(before))&&!memcmp(&old,&opening.player_work,sizeof(old))&&bhEtask[0].mode0==mode,"foreign task target reached player handler");
 rx_events_reset();rom->evtp=(void *)original;memcpy(sys->ev_flg,flags,sizeof(flags));
 rx_opening_close_graphics(&opening);rx_egl_close(&egl);rx_opening_end(&opening);
 check(!plp->exp0&&rx_player_control_check()!=NULL,"native player ownership survived teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);
 puts("PASS foreign task-target rejection, complete scenario gate/progression restoration and native player binding teardown");return 0;
}
