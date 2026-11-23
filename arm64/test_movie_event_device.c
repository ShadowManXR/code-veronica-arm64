#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_movie.h"
#include "message_runtime.h"
#include "movie_port.h"
#include "room_scripts.h"
#include "sdfunc.h"
#include "event.h"
#include "padman.h"
#include "main.h"
#include "android_egl.h"
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern unsigned char *bhScePtr;
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL movie event: %s\n",why);exit(1);}}
static unsigned char data[80];static RxRoomScripts scripts;
static const char *request(unsigned movie){memset(data,0,sizeof(data));data[0]=8;data[4]=12;data[8]=0x5e;data[9]=movie;check(!rx_room_scripts_open(data,14,&scripts),"movie request fixture");rom->evtp=&scripts;return rx_events_start_room();}
static void reject_code(unsigned opcode,unsigned movie,unsigned bytes){unsigned char raw[32]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};raw[14]=opcode;raw[15]=movie;raw[4]=14+bytes;RxRoomScripts bad;check(!rx_room_scripts_open(raw,16+bytes,&bad),"bad movie fixture");rom->evtp=&bad;unsigned flags=sys->ev_flg[0],cb=sys->cb_flg;int no=sys->mvi_no;sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0]&&sys->cb_flg==cb&&sys->mvi_no==no,"invalid movie executed prefix or changed request");sys->ev_flg[0]=flags;}
static void reject(unsigned movie,unsigned bytes){reject_code(0x5e,movie,bytes);}
static unsigned char pixels[640*480*4],reference[sizeof(pixels)];
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};RxEgl egl={0};RxMovieDraw draw={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full opening remains gated at effect clear");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 check(!rx_event_movie_status_check()&&!rx_game_movie_active(),"owned idle event movie context");
 unsigned old_cb=sys->cb_flg;int old_no=sys->mvi_no;MOV_INFO info=MovieInfo;unsigned char *memory=sys->memp;
 for(unsigned padding=0;padding<65536;++padding){unsigned char command[]={0x5e,0,padding,padding>>8};bhScePtr=command;sys->cb_flg=0xa15a5a5a;sys->mvi_no=255;check(bhMovieStart()==1&&bhScePtr==command+4&&sys->cb_flg==(0xa15a5a5a|0x4000000)&&sys->mvi_no==0&&!memcmp(&info,&MovieInfo,sizeof(info))&&sys->memp==memory,"original request bytes/flags/padding/state preservation");}
 sys->cb_flg=old_cb;sys->mvi_no=old_no;
 for(unsigned padding=0;padding<256;++padding){unsigned char stop[]={0x5f,padding};bhScePtr=stop;sys->cb_flg=0xa55a5a5a;check(bhMovieStop()==1&&bhScePtr==stop+2&&sys->cb_flg==(0xa55a5a5a&~0x4000000u)&&!memcmp(&info,&MovieInfo,sizeof(info)),"original MovieStop request flag semantics");}sys->cb_flg=old_cb;
 unsigned accepted=0;for(unsigned number=0;number<256;++number){int valid=number<22&&rx_game_movie_available(number);check((rx_event_movie_check(number)==NULL)==valid,"event asset guard differs from disc membership");if(!valid)reject(number,4);else ++accepted;}
 for(unsigned bytes=1;bytes<4;++bytes)reject(0,bytes);
 for(unsigned bytes=1;bytes<6;++bytes)reject_code(0x9b,0,bytes);reject_code(0x5f,0,1);
 int type=sys->mvi_tp;sys->mvi_tp=4;reject(0,4);sys->mvi_tp=type;
 check(original->bytes>6920&&!memcmp(original->data+6916,(unsigned char[]){0x5e,0,0,0},4),"actual opening movie bytes");
 printf("PASS 65536 original request padding pairs, all 256 asset selectors (%u available), exact flags, length and atomic malformed/missing-member guards\n",accepted);
 check(rx_egl_open(&egl,NULL,640,480),"movie event EGL");
 /* Start through real bytecode, then advance the original system phases.
    Preserve every unrelated saved flag bit and the high native arena mark. */
 unsigned task_seed=0x12ab3479,screen_seed=0x89abcdef;sys->sp_flg=task_seed;sys->ts_flg=screen_seed;sys->cb_flg=0x102000;
 error=request(0);check(!error,error);unsigned expected_tasks=sys->sp_flg|0x20,expected_screen=sys->ts_flg;unsigned char *aligned=(void *)(((uintptr_t)sys->memp+63)&~(uintptr_t)63);
 check(sys->cb_flg&0x4000000,"original movie request not latched");
 error=rx_event_movie_tick();check(!error,error);check(rx_event_movie_running()&&sys->mvi_md==1&&sys->mvi_flg==0&&sys->sp_flg==4&&sys->mvi_spb==expected_tasks&&sys->mvi_tsb==expected_screen&&sys->ts_flg==((expected_screen&~0x1000u)|0x7cf00)&&sys->mvi_memp==aligned&&(uintptr_t)aligned>UINT32_MAX,"original request handoff/phase0 differs");
 error=rx_event_movie_tick();check(!error&&sys->mvi_md==2,"original phase1");error=rx_event_movie_tick();check(!error,error);check(sys->mvi_md==3&&CheckPlayEndMovie()&&rx_game_movie_active(),"original phase2 movie start");
 check(rx_event_movie_bind(&opening)!=NULL,"active movie context was rebound");
 unsigned shown=0;int paused=0;for(unsigned wait=0;wait<600;++wait){
  error=rx_event_movie_tick();check(!error,error);RxMovie *movie=rx_game_movie_active();check(movie&&CheckPlayEndMovie(),"opening clip ended before playback check");
  if(sys->mvi_md==4&&GetTimeMoive()>=100){
   glViewport(0,0,640,480);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);check(rx_game_movie_draw(movie,&draw)&&draw.uploaded,"original event movie GPU frame");glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);check(!glGetError(),"movie GPU readback");
   unsigned colored=0;for(unsigned i=0;i<640*480;++i)colored+=pixels[i*4]>10||pixels[i*4+1]>10||pixels[i*4+2]>10;check(colored>1000,"movie image empty");shown=rx_game_movie_frame(movie);
   rx_game_movie_pause(movie,1);int time=GetTimeMoive();for(unsigned k=0;k<5;++k){usleep(20000);error=rx_event_movie_tick();check(!error,error);}check(GetTimeMoive()==time&&rx_game_movie_frame(movie)==shown,"paused event movie advanced");
   glClear(GL_COLOR_BUFFER_BIT);check(rx_game_movie_draw(movie,&draw),"repeat movie draw");glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,reference);check(!memcmp(pixels,reference,sizeof(pixels)),"paused movie frame changed");
   unsigned char message[]={0x1f,0,0,1};bhScePtr=message;check(bhMessageSet()==1,"original movie message request");sys->mes_ct=4096;error=rx_message_tick(1);check(!error,error);
   error=rx_message_present(&opening.particles);check(!error,error);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,reference);unsigned glyph_pixels=0;for(unsigned k=0;k<640*480;++k)glyph_pixels+=memcmp(pixels+k*4,reference+k*4,4)!=0;check(glyph_pixels>10,"original message not composited over movie");
   glClear(GL_COLOR_BUFFER_BIT);check(rx_game_movie_draw(movie,&draw),"movie draw after message");glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,reference);check(!memcmp(pixels,reference,sizeof(pixels)),"message renderer leaked state into subsequent movie draw");rx_message_clear();
   printf("PASS original movie message over video: %u glyph pixels, exact subsequent movie frame after font renderer\n",glyph_pixels);
   rx_game_movie_pause(movie,0);paused=1;break;
  }usleep(10000);
 }
 check(paused&&shown>15,"event movie did not advance on its soundtrack clock");
 /* The original MovieCk ignores all five operands and reports active=1. */
 for(unsigned byte=0;byte<256;++byte){unsigned char command[]={0x9b,byte,byte,byte,byte,byte};bhScePtr=command;check(bhMovieCk()==1&&bhScePtr==command+6,"active original MovieCk/ignored operands");}
 unsigned char waiting[40]={12,0,0,0,18,0,0,0,20,0,0,0,0x14,0,0,0,0,0,0,0,0xfc,8,0x9b,0,0,0,0,0,0xfe,0xf3,5,1,30,0,0,0,0xff,0};
 RxRoomScripts wait_script;check(!rx_room_scripts_open(waiting,38,&wait_script),"original movie wait fixture");rom->evtp=&wait_script;sys->ev_flg[0]&=~2u;error=rx_events_start_room();check(!error,error);
 for(unsigned i=0;i<3;++i){error=rx_events_tick();check(!error,error);check(!(sys->ev_flg[0]&2)&&bhEtask[0].status,"MovieCk wait exited while decoder active");}
 Pad[CurrentPortId].press=0x100;error=rx_event_movie_tick();check(!error,error);Pad[CurrentPortId].press=0;check(!rx_game_movie_active()&&!CheckPlayEndMovie()&&sys->mvi_md==5,"original movie cancellation phase");
 error=rx_events_tick();check(!error,error);check((sys->ev_flg[0]&2)&&!bhEtask[0].status,"original MovieCk loop did not continue after decoder stop");
 error=rx_event_movie_tick();check(!error&&sys->mvi_md==6&&sys->memp==aligned&&rx_event_movie_running(),"original phase5 arena restore");error=rx_event_movie_tick();check(!error,error);
 check(!rx_event_movie_running()&&sys->sp_flg==expected_tasks&&sys->ts_flg==expected_screen&&sys->bcl_ct==1&&(sys->gm_flg&0x8000),"original phase6 task/screen restoration");
 unsigned char predicate[]={0x9b,0,0,0,0,0};bhScePtr=predicate;check(!bhMovieCk()&&bhScePtr==predicate+6,"completed original MovieCk");
 printf("PASS actual opening movie request, original phases 0..6, high arena mark, soundtrack-driven frame %u, GLES/pause/exact repeat, cancellation, MovieCk and exact task/screen restoration\n",shown);
 puts("PASS original While/MovieCk/EvtNext/Ewhile2 task waits during playback and resumes exactly after completion");
 rx_game_movie_close_graphics(&draw);
 error=request(4);check(!error,error);unsigned natural_tasks=sys->sp_flg|0x20,natural_screen=sys->ts_flg;unsigned last=0;int complete=0;
 for(unsigned wait=0;wait<3000;++wait){error=rx_event_movie_tick();check(!error,error);RxMovie *movie=rx_game_movie_active();if(movie){unsigned frame=rx_game_movie_frame(movie);check(frame>=last,"natural movie frame moved backwards");last=frame;}
  if(!rx_event_movie_running()){complete=1;break;}usleep(10000);
 }
 check(complete&&last>30&&!CheckPlayEndMovie()&&sys->sp_flg==natural_tasks&&sys->ts_flg==natural_screen,"natural movie completion/task restore failed");
 printf("PASS natural MV_004 completion through original system controller, final frame %u and exact restored tasks\n",last);
 error=request(0);check(!error,error);for(unsigned phase=0;phase<3;++phase){error=rx_event_movie_tick();check(!error,error);}check(sys->mvi_md==3,"prebuffer cancellation setup");Pad[CurrentPortId].press=0x100;error=rx_event_movie_tick();check(!error,error);Pad[CurrentPortId].press=0;check(sys->mvi_md==5&&!CheckPlayEndMovie(),"prebuffer cancellation failed");for(unsigned i=0;i<2;++i){error=rx_event_movie_tick();check(!error,error);}check(!rx_event_movie_running(),"prebuffer cancellation cleanup");
 /* Guards reject a foreign decoder, an overlapping request and a corrupted
    arena mark without calling the original unsafe controller phase. */
 check(!rx_game_movie_start(0,0,80,640,320,0x100,0,0),"foreign decoder fixture");check(rx_event_movie_status_check()!=NULL,"foreign decoder accepted");reject(0,4);rx_game_movie_stop();
 error=request(0);check(!error,error);error=rx_event_movie_tick();check(!error,error);unsigned char *mark=sys->mvi_memp;sys->mvi_memp=(void *)(uintptr_t)1;check(rx_event_movie_tick()!=NULL&&sys->mvi_md==1,"foreign arena mark reached original controller");sys->mvi_memp=mark;
 sys->mvi_md=7;check(rx_event_movie_tick()!=NULL&&sys->mvi_md==7,"invalid movie controller phase accepted");sys->mvi_md=1;
 sys->cb_flg|=0x4000000;check(rx_event_movie_tick()!=NULL&&sys->mvi_md==1,"overlapping request reached original controller");sys->cb_flg&=~0x4000000u;rx_event_movie_end(&opening);check(!rx_event_movie_running(),"pending movie teardown retained controller");
 check(!rx_event_movie_bind(&opening),"event movie rebind after teardown");
 puts("PASS original pending-stop flags/all padding, truncated request/status/stop preflight, invalid mode, prebuffer cancellation, foreign decoder/arena/phase and overlapping request guards");
 rx_game_movie_close_graphics(&draw);rx_egl_close(&egl);rx_events_reset();rom->evtp=original;rx_opening_end(&opening);check(rx_event_movie_status_check()!=NULL,"retired event movie context accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);puts("PASS event movie teardown");return 0;
}
