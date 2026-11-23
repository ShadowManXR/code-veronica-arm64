#include "player_control_runtime.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "main.h"
#include "event.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL player script: %s\n",why);exit(1);}}
static void reject(const unsigned char *command,size_t bytes){
 unsigned char data[64]={8,0,0,0};data[4]=14+bytes;const unsigned char mark[]={5,1,30,0,0,0};memcpy(data+8,mark,6);memcpy(data+14,command,bytes);
 RxRoomScripts scripts;check(!rx_room_scripts_open(data,16+bytes,&scripts),"player rejection fixture");void *old=rom->evtp;rom->evtp=&scripts;unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;
 check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"invalid player command executed prefix");sys->ev_flg[0]=flags;rx_events_reset();rom->evtp=old;
}
void rx_check_player_controls(void (*run)(const unsigned char*,size_t)){
 BH_PWORK saved=*plp;float ground[32];memcpy(ground,rom->grand,sizeof(ground));
 struct Guard {uint64_t first;EXP_WORK work;uint64_t last;} *guard=malloc(sizeof(*guard));check(guard&&(uintptr_t)guard>UINT32_MAX,"high native scalar allocation");
 memset(guard,0xa5,sizeof(*guard));plp->exp0=NULL;check(!rx_player_control_bind(plp,&guard->work,sizeof(guard->work)),"native scalar bind");
 check(rx_player_control_bind(plp,&guard->work,sizeof(guard->work)-1)!=NULL,"short scalar work accepted");
 EXP_WORK foreign;check(rx_player_control_bind(plp,&foreign,sizeof(foreign))!=NULL,"occupied scalar work replaced");
 memset(rom->grand,0,sizeof(rom->grand));rom->grand[0]=-50;rom->grand[1]=-10;rom->grand[3]=10;rom->grand[4]=20;rom->grand[30]=50;
 unsigned long count=0;
 for(unsigned sign=0;sign<8;++sign)for(unsigned value=0;value<65536;++value){
  *plp=saved;plp->exp0=(unsigned char *)&guard->work;memset(&guard->work,0xa5,sizeof(guard->work));
  unsigned short x=value,y=value,z=65535-value;
  unsigned char code[]={0x65,0,0,0,0x64,12,sign&1,sign&2,sign&4,255,0x64,7,x,x>>8,y,y>>8,z,z>>8,0,0};
  BH_PWORK expected=*plp;EXP_WORK scalar=guard->work;
  expected.mode0=7;expected.mode2=0;expected.mtn_no=(expected.mtn_no&~255u)|42;expected.stflg=(expected.stflg|0x10000)&~0x80u;expected.flg|=0x10000;
  expected.px=expected.pxb=(float)x/100.0f;if(sign&1)expected.px=expected.pxb=-expected.px;
  expected.py=expected.pyb=(float)y/100.0f;if(sign&2)expected.py=expected.pyb=-expected.py;
  expected.pz=expected.pzb=(float)z/100.0f;if(sign&4)expected.pz=expected.pzb=-expected.pz;
  float h=expected.py;expected.flr_no=h>=50?28:h>=20?2:h>=10?1:h>=0?0:h>=-10?-1:h>=-50?-2:0;
  scalar.spx=expected.px;scalar.spz=expected.pz;
  run(code,sizeof(code));check(!memcmp(plp,&expected,sizeof(expected)),"original position/backup/floor or unrelated player fields differ");check(!memcmp(&scalar,&guard->work,sizeof(scalar)),"original scalar offset or canary differs");
  check(bhEtask[0].mode0==1&&bhEtask[0].work==plp&&bhEtask[0].bpx==(sign&1)&&bhEtask[0].bpy==(sign&2)&&bhEtask[0].bpz==(sign&4),"original player task mode/signs differ");++count;
 }
 for(unsigned byte=0;byte<256;++byte){unsigned char code[]={0x64,12,byte,byte,byte,byte,0x64,13,byte,byte,byte,byte,0x64,0,0,0};BH_PWORK before=*plp;EXP_WORK old=guard->work;run(code,sizeof(code));
  check(bhEtask[0].mode0==1&&(unsigned char)bhEtask[0].bpx==byte&&(unsigned char)bhEtask[0].bpy==byte&&(unsigned char)bhEtask[0].bpz==byte&&(unsigned char)bhEtask[0].bax==byte&&(unsigned char)bhEtask[0].bay==byte&&(unsigned char)bhEtask[0].baz==byte,"full-byte task signs/padding/nop differ");
  check(!memcmp(&before,plp,sizeof(before))&&!memcmp(&old,&guard->work,sizeof(old)),"task sign/nop changed actor work");
 }
 check(guard->first==UINT64_C(0xa5a5a5a5a5a5a5a5)&&guard->last==guard->first,"player scalar allocation canaries");
 for(unsigned sub=0;sub<256;++sub)if(sub!=0&&sub!=7&&sub!=12&&sub!=13&&sub!=52&&sub!=128&&sub!=131)reject((unsigned char[]){0x64,sub,0,0,0,0,0,0},8);
 reject((unsigned char[]){0x64,52},2);reject((unsigned char[]){0x64,128},2); /* no model bank in this scalar-only fixture */
 for(unsigned size=1;size<8;++size)reject((unsigned char[]){0x64,7,0,0,0,0,0,0},size);
 for(unsigned size=1;size<6;++size)reject((unsigned char[]){0x64,12,0,0,0,0},size);
 for(unsigned size=1;size<4;++size)reject((unsigned char[]){0x64,131,1,0},size);
 unsigned char *owned=plp->exp0;plp->exp0=(unsigned char *)(uintptr_t)1;reject((unsigned char[]){0x64,7,0,0,0,0,0,0},8);plp->exp0=owned;
 rom->grand[4]=NAN;reject((unsigned char[]){0x64,7,0,0,0,0,0,0},8);rom->grand[4]=20;
 rx_player_control_reset();check(!plp->exp0&&rx_player_control_check()!=NULL,"player binding teardown retained native reference");reject((unsigned char[]){0x64,7,0,0,0,0,0,0},8);
 reject((unsigned char[]){0x64,131,1,0},4);
 *plp=saved;memcpy(rom->grand,ground,sizeof(ground));free(guard);
 printf("PASS %lu original player positions: every unsigned coordinate, eight signs, scalar backups/floors/full actor canaries; all task-sign bytes/nop and bounded unsupported/truncated/foreign/retired work\n",count);
}
