#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "event_movie.h"
#include "movie_port.h"
#include "padman.h"
#include "sdfunc.h"
#include <unistd.h>
#include "player_control_runtime.h"
#include "room.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern BH_SCEWORK bhEtask[16];
static void state(void){printf("Message st%08x idx%d ct%d timer%d\n",sys->st_flg,sys->mes_idx,sys->mes_ct,sys->mes_tim);for(unsigned i=0;i<16;++i)if(bhEtask[i].status)printf("Task%u status%u offset%lld count%d\n",i,bhEtask[i].status,(long long)((uintptr_t)bhEtask[i].data-(uintptr_t)((RxRoomScripts*)rom->evtp)->data),bhEtask[i].cnt[0]);printf("System sp%08x cb%08x ts%08x gm%08x\n",sys->sp_flg,sys->cb_flg,sys->ts_flg,sys->gm_flg);printf("Player: mode %u/%u/%u/%u flags %08x/%08x motion %d frame %d bank %p\n",plp->mode0,plp->mode1,plp->mode2,plp->mode3,plp->flg,plp->stflg,plp->mtn_no,plp->frm_no,(void*)plp->mnwP);for(int i=0;i<rom->ene_n;++i){BH_PWORK *a=ene+rom->enep[i].wrk_no;printf("Actor%d id%u: mode %u/%u/%u/%u callback %d flags %08x/%08x motion %d frame %d bank %p room %p\n",i,a->id,a->mode0,a->mode1,a->mode2,a->mode3,a->ct2,a->flg,a->stflg,a->mtn_no,a->frm_no,(void*)a->mnwP,(void*)sys->rmthp);}}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);if(argc!=2)return 2;RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);if(!e)e=rx_android_start_disc(&disc,&boot);if(!e)e=rx_opening_start(&o,0);if(e){fprintf(stderr,"FAIL setup: %s\n",e);return 1;}unsigned ticks=0;int saw_movie_skip=0;e=rx_events_start_room();if(e){const RxEventFault *f=rx_events_fault();printf("BLOCKED entry opcode%x offset%zu: %s\n",f->opcode,f->offset,e);state();}else{o.events_ready=1;for(unsigned frame=0;frame<3000;++frame){Pad[CurrentPortId].press=(rx_game_movie_active()&&rx_game_movie_frame(rx_game_movie_active())>30)?0x100:0;if(Pad[CurrentPortId].press)saw_movie_skip=1;++ticks;sys->pad_old=sys->pad_on;sys->pad_on=sys->pad_ps=(frame%30==29)?0x800:0;e=rx_opening_move_player(&o);if(e){const RxEventFault *f=rx_events_fault();printf("BLOCKED frame%u opcode%x offset%zu: %s\n",frame,f->opcode,f->offset,e);state();break;}if(rx_event_movie_running())usleep(33333);if(frame%300==0)printf("TRACE tick%u movie%d phase%u frame%u\n",frame,rx_event_movie_running(),sys->mvi_md,rx_game_movie_active()?rx_game_movie_frame(rx_game_movie_active()):0);}if(!e){puts("TRACE reached 3000 simulated ticks; not a playability assertion");state();}o.events_ready=0;}int reached_idle=ticks==3000&&saw_movie_skip&&!e&&plp->mode0==1&&plp->mode2==13&&plp->mode3==2&&
 plp->mtn_no==PlMtnAct[0][0][6]+1&&plp->ct3==301;
int cancelled=0;unsigned walked=0,ran=0,returned=0;
if(reached_idle){o.events_ready=1;sys->pad_on=sys->pad_ps=1;e=rx_opening_move_player(&o);
 cancelled=!e&&!(plp->flg&0x10000)&&!(sys->pad_on&15);
 if(cancelled){for(unsigned t=0;t<90;++t){sys->pad_old=sys->pad_on;sys->pad_on=1;sys->pad_ps=t?0:1;e=rx_opening_move_player(&o);if(e){printf("BLOCKED walking update %u: %s\n",t,e);state();break;}++walked;}}
 if(!e&&walked==90){sys->pad_old=sys->pad_on;sys->pad_on=0x401;sys->pad_ps=0x400;e=rx_opening_move_player(&o);}
 if(!e&&plp->mode2==6){for(unsigned t=0;t<90;++t){sys->pad_old=sys->pad_on;sys->pad_on=0x401;sys->pad_ps=0;e=rx_opening_move_player(&o);if(e){printf("BLOCKED run update %u: %s\n",t,e);state();break;}++ran;}}
 if(!e&&ran==90){for(unsigned t=0;t<12;++t){sys->pad_old=sys->pad_on;sys->pad_on=1;sys->pad_ps=0;e=rx_opening_move_player(&o);if(e){printf("BLOCKED walk-return update %u: %s\n",t,e);state();break;}++returned;}}
 if(!e&&returned==12){sys->pad_old=sys->pad_on;sys->pad_on=sys->pad_ps=2;e=rx_opening_move_player(&o);if(e)printf("BLOCKED original reverse request after %u run and %u return updates: %s\n",ran,returned,e);}
 o.events_ready=0;
}
int expected=reached_idle&&cancelled&&walked==90&&ran==90&&returned==12&&e&&strstr(e,"Original normal player action/interaction mode is not ported yet");
if(expected)puts("PASS original opening trace runs 3000 simulated ticks, cancels idle, walks 90 updates, runs 90 and returns to walking for 12 updates in the authored room; reverse input reaches the explicit unported-movement guard; full opening remains gated");
else fprintf(stderr,"FAIL opening trace: unexpected progress boundary\n");
rx_opening_end(&o);rx_android_stop_disc();rx_android_unmount_disc(&disc);return expected?0:1;}
