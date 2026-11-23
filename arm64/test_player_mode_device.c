#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "player_control_runtime.h"
#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL player mode: %s\n",why);exit(1);}}
static unsigned char code[40];static RxRoomScripts scripts;
static const char *run(unsigned action,unsigned variant){
 memset(code,0,sizeof(code));code[0]=12;code[4]=18;code[8]=20;code[12]=0x14;
 const unsigned char commands[]={0xfe,0x64,131,action,variant,5,1,30,0,0,0};
 memcpy(code+20,commands,sizeof(commands));code[31]=0xff;
 check(!rx_room_scripts_open(code,33,&scripts),"mode fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~2u;
 const char *error=rx_events_start_room();if(error)return error;
 /* Resume a task that already owns the player, so LoadWork's independent
    mode reset does not hide the incoming action used by PlCtr.131. */
 bhEtask[0].work=plp;return rx_events_tick();
}
static void reject(const unsigned char *command,unsigned bytes){
 unsigned char raw[32]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};raw[4]=14+bytes;memcpy(raw+14,command,bytes);
 RxRoomScripts bad;check(!rx_room_scripts_open(raw,16+bytes,&bad),"invalid mode fixture");rom->evtp=&bad;
 BH_PWORK before=*plp;unsigned flag=sys->ev_flg[0];sys->ev_flg[0]=0;
 check(rx_events_start_room()!=NULL&&!sys->ev_flg[0]&&!memcmp(&before,plp,sizeof(before)),"invalid mode executed prefix or changed player");sys->ev_flg[0]=flag;
}
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);
 check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full opening remains gated at next work selection");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 BH_PWORK saved=*plp;EXP_WORK scalar=opening.player_work;unsigned cases=0;
 for(unsigned prior=0;prior<256;++prior)for(unsigned action=0;action<256;++action){
  *plp=saved;plp->mode0=prior^0xa5;plp->mode1=prior^0x5a;plp->mode2=prior;plp->mode3=action^0xff;
  unsigned variant=(prior+action)&255;BH_PWORK expected=*plp;expected.mode0=7;expected.mode1=variant;expected.mode2=action;expected.mode3=prior==action&&action!=1;
  error=run(action,variant);check(!error,error);check(sys->ev_flg[0]&2,"original four-byte mode command length");
  check(!memcmp(&expected,plp,sizeof(expected))&&!memcmp(&scalar,&opening.player_work,sizeof(scalar)),"original mode transition changed unrelated player/scalar work");++cases;
 }
 for(unsigned action=0;action<256;++action)for(unsigned variant=0;variant<256;++variant){
  *plp=saved;plp->mode2=action;plp->mode3=255;BH_PWORK expected=*plp;expected.mode0=7;expected.mode1=variant;expected.mode2=action;expected.mode3=action!=1;
  error=run(action,variant);check(!error,error);check(!memcmp(&expected,plp,sizeof(expected)),"same-action restart or variant differs");++cases;
 }
 printf("PASS %u whole-player transitions: all prior/action pairs and all action/variant pairs; repeat-action latch, forced action-1 restart and unrelated scalar preservation\n",cases);
 *plp=saved;
 const unsigned offsets[]={6540,6558};for(unsigned i=0;i<2;++i){const unsigned char *p=original->data+offsets[i];check(original->bytes>=offsets[i]+4&&p[0]==0x64&&p[1]==131&&p[2]==i+1&&!p[3],"actual opening action bytes");error=run(p[2],p[3]);check(!error,error);check(plp->mode0==7&&plp->mode1==0&&plp->mode2==i+1&&!plp->mode3,"actual opening action transition");}
 puts("PASS actual opening action-1/action-2 script requests");
 *plp=saved;const unsigned char command[]={0x64,131,1,0};for(unsigned bytes=1;bytes<4;++bytes)reject(command,bytes);
 unsigned char *work=plp->exp0;plp->exp0=(void *)(uintptr_t)1;reject(command,4);plp->exp0=work;
 unsigned char wait[36]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0,0x65,0,0,0,0xfe};memcpy(wait+25,command,4);wait[29]=0xff;
 RxRoomScripts delayed;check(!rx_room_scripts_open(wait,31,&delayed),"delayed mode fixture");rom->evtp=&delayed;
 const uintptr_t targets[]={0,1,(uintptr_t)&ene[0]};for(unsigned i=0;i<3;++i){check(!rx_events_start_room(),"start delayed mode task");BH_PWORK before=*plp;bhEtask[0].work=(void *)targets[i];check(rx_events_tick()!=NULL&&!memcmp(&before,plp,sizeof(before)),"foreign task reached mode handler");}
 rx_events_reset();rom->evtp=original;*plp=saved;rx_opening_end(&opening);check(rx_player_control_check()!=NULL,"retired player scalar context accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);
 puts("PASS truncated/foreign-scalar preflight, null/foreign/actor task targets and retired player binding");return 0;
}
