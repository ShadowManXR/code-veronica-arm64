#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "event.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL native frame: %s\n",why);exit(1);}}
static unsigned char bank[38]={12,0,0,0,18,0,0,0,20,0,0,0,20,0,0,0,0,0,0,0};
static RxRoomScripts scripts;
static void begin(const unsigned char command[6]){
 const unsigned char body[]={0xfc,8,0x38,0,0,0,0,0,0xfe,0xf3,5,1,30,0,0,0,0xff,0};
 memcpy(bank+20,body,sizeof(body));memcpy(bank+22,command,6);check(!rx_room_scripts_open(bank,sizeof(bank),&scripts),"native wait fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~2u;
 const char *error=rx_events_start_room();check(!error,error);
}
static void tick(void){const char *error=rx_events_tick();check(!error,error);}
static int done(void){return (sys->ev_flg[0]&2)&&!bhEtask[0].status;}
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"complete opening must remain gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"frame or pause command still gated");
 const RxRoomScripts *original=rom->evtp;unsigned flags[sizeof(sys->ev_flg)/sizeof(*sys->ev_flg)];memcpy(flags,sys->ev_flg,sizeof(flags));
 check(original->bytes>1810&&original->data[1804]==0x38,"actual original FrameCheck bytes");unsigned char actual[6];memcpy(actual,original->data+1804,6);
 BH_PWORK *actor=&ene[rom->enep[actual[2]].wrk_no];BH_PWORK saved=*actor;unsigned boundary=((unsigned)actual[4]|(unsigned)actual[5]<<8)*65536;
 actor->frm_no=(int)boundary-1;begin(actual);check(!done()&&bhEtask[0].status&&bhEtask[0].loop==0,"actual wait did not yield");
 check((uintptr_t)bhEtask[0].lcondition[0]>UINT32_MAX,"wait continuation truncated");
 tick();check(!done()&&actor->frm_no==(int)boundary-1,"predicate advanced actor motion");actor->frm_no=boundary;tick();check(done(),"actual wait did not complete at exact frame");*actor=saved;
 /* Original native body evaluation supplies the frame clock; the predicate
    must neither advance it nor release a wait before the exact half frame. */
 unsigned char player_wait[]={0x38,0,255,128,1,0};
 error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_WALK,0,1);check(!error,error);check(plp->frm_no==65536,"original walk clock step");begin(player_wait);check(!done(),"half-frame wait released early");
 error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_WALK,65536,0);check(!error,error);tick();check(!done()&&plp->frm_no==65536,"paused original pose advanced wait");
 error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_WALK,98303,0);check(!error,error);tick();check(!done(),"fractional frame released early");
 error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_WALK,98304,0);check(!error,error);tick();check(done(),"half-frame equality did not release wait");
 /* Original reverse/wrap steps keep the same signed comparison semantics. */
 error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_WALK,0,-1);check(!error,error);check(plp->frm_no==(int)(rx_engine_player_frames(opening.player,RX_PLAYER_WALK)-1)*65536,"original reverse wrap");begin(player_wait);tick();check(done(),"wrapped frame did not use current original frame value");
 rx_events_reset();rom->evtp=(void *)original;memcpy(sys->ev_flg,flags,sizeof(flags));
 puts("PASS actual opening FrameCheck wait/yield/completion with high pointers; original native player motion, pause, fractional half frame, reverse wrap; progression restored");
 rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
