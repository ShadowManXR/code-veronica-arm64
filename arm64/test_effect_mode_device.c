#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_effects.h"
#include "effect_runtime.h"
#include "room_effects.h"
#include "room_scripts.h"
#include "main.h"
#include "random_runtime.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaFog.h"
#include "gs/gs.h"
#include "android_egl.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GLES3/gl3.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL effect mode: %s\n",why);exit(1);}}
static unsigned char code[32];static RxRoomScripts script;
static const char *run(const unsigned char *command,unsigned bytes){memset(code,0,sizeof(code));code[0]=8;code[4]=8+bytes+6;memcpy(code+8,command,bytes);unsigned char marker[]={5,1,30,0,0,0};memcpy(code+8+bytes,marker,6);check(!rx_room_scripts_open(code,bytes+16,&script),"effect mode fixture");rom->evtp=&script;sys->ev_flg[0]&=~2u;return rx_events_start_room();}
static O_WRK initial[512],before[512];static short initial_map[256];
static void reject(const unsigned char *command,unsigned bytes){unsigned char bad[32]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};bad[4]=14+bytes;memcpy(bad+14,command,bytes);RxRoomScripts bank;check(!rx_room_scripts_open(bad,bytes+16,&bank),"bad effect mode fixture");rom->evtp=&bank;memcpy(before,eff,sizeof(eff));short mapping[256];memcpy(mapping,sys->efid,sizeof(mapping));unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"invalid effect mode executed prefix");check(!memcmp(before,eff,sizeof(eff))&&!memcmp(mapping,sys->efid,sizeof(mapping)),"invalid mode changed effect pool or mapping");sys->ev_flg[0]=flags;}
static void tick(void){const char *error=rx_effects_tick();check(!error,error);}
static void reset_pool(void){rx_effects_reset();memcpy(eff,initial,sizeof(eff));memcpy(sys->efid,initial_map,sizeof(initial_map));rx_ps2_srand(12345);}
static int smoke_child(void){for(unsigned i=0;i<512;++i)if((eff[i].flg&1)&&eff[i].id==124)return (int)i;return -1;}
static unsigned char pixels[640*480*4],reference[640*480*4];
int main(int argc,char **argv){check(argc==2,"supply OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};RxEgl egl={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full opening remains gated at next original opcode");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 check(rom->eff_n==14&&rx_room_effects_count(opening.room_effects)==14,"actual RM_0000 effect table");check(original->bytes>5694&&!memcmp(original->data+5690,(unsigned char[]){0x91,0,1,0},4),"actual opening mode bytes");memcpy(initial,eff,sizeof(eff));memcpy(initial_map,sys->efid,sizeof(initial_map));unsigned accepted=0,rejected=0;
 for(unsigned record=0;record<14;++record){int slot=sys->efid[record];check(slot==rx_room_effects_slot(opening.room_effects,record),"owned native mapping");for(unsigned mode=0;mode<256;++mode)for(unsigned p=0;p<2;++p){eff[slot]=initial[slot];unsigned char command[]={0x91,record,mode,p?255:0};if(initial[slot].id==125&&mode>7){reject(command,4);++rejected;continue;}O_WRK expected=eff[slot];expected.mode1=mode;O_WRK neighbor=eff[(slot+1)%512];short mapping[256];memcpy(mapping,sys->efid,sizeof(mapping));const char *error=run(command,4);check(!error,error);check(sys->ev_flg[0]&2,"EffModeSet length/continuation");check(!memcmp(&expected,eff+slot,sizeof(expected))&&!memcmp(&neighbor,eff+(slot+1)%512,sizeof(neighbor))&&!memcmp(mapping,sys->efid,sizeof(mapping)),"original mode write changed unrelated fields/work/mapping");++accepted;}}
 reset_pool();for(unsigned padding=0;padding<256;++padding){O_WRK expected=eff[sys->efid[0]];expected.mode1=1;check(!run((unsigned char[]){0x91,0,1,padding},4)&&!memcmp(&expected,eff+sys->efid[0],sizeof(expected)),"original mode padding consumed or changed state");++accepted;}
 printf("PASS %u original EffModeSet whole-work states across all 14 room records, mode bytes and padding; %u unsupported emitter requests reject atomically\n",accepted,rejected);
 reset_pool();int manager=sys->efid[0];sys->sp_flg|=8;sys->ef_flg=0;sys->gm_flg&=~1u;check(!run(original->data+5690,4),"actual smoke mode instruction");tick();int child=smoke_child();check(child>=0&&!eff[manager].mode1&&eff[child].mode0==1&&eff[child].type==0&&fabsf(eff[child].sx-initial[manager].sx*0.1f)<0.000001f,"original room smoke cue did not emit supported particle");check(!rx_event_effect_mode_check(2,1)&&!rx_event_effect_mode_check(9,1),"initialized native UV/overlay ownership rejected");puts("PASS actual opening mode command -> original smoke manager -> one-shot cue consumed and native child initialized; initialized UV/overlay owners remain valid");
 /* Isolate the same authored manager at a known view position for complete
    pause/loading, particle lifetime and GPU transport checks. */
 reset_pool();for(unsigned record=1;record<14;++record)eff[sys->efid[record]].stflg|=0x1000000u;
 O_WRK *op=eff+manager;op->px=0;op->py=5;op->pz=20;op->sx=5;op->sy=7;op->sz=5;check(!run(original->data+5690,4),"isolated original smoke cue");sys->sp_flg&=~8u;tick();check(op->mode1==1&&smoke_child()<0,"paused effects consumed original cue");sys->sp_flg|=8;sys->gm_flg|=1;tick();check(op->mode1==1&&smoke_child()<0,"loading effects consumed original cue");sys->gm_flg&=~1u;tick();child=smoke_child();check(child>=0&&op->mode1==0&&eff[child].py==5&&eff[child].gpy==0&&eff[child].sx==0.5f&&eff[child].sy==0.7f,"original smoke child state");tick();
 check(rx_egl_open(&egl,NULL,640,480),"effect mode EGL");static NJS_MATRIX camera;njUnitMatrix(&camera);NJS_MATRIX *previous_camera=cam.mtx;cam.mtx=&camera;njFogDisable();gs_reset();const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};
 glViewport(0,0,640,480);glDepthMask(GL_TRUE);glClearDepthf(1);glClearColor(32/255.0f,32/255.0f,32/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);error=rx_particle_present(&opening.particles,&view);check(!error,error);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);unsigned lit=0;for(unsigned i=0;i<640*480;++i)lit+=pixels[i*4]!=32||pixels[i*4+1]!=32||pixels[i*4+2]!=32;check(lit>10,"event-triggered smoke not visible on GLES");O_WRK held=eff[child];glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);check(!rx_particle_present(&opening.particles,&view),"repeated smoke GPU draw");glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,reference);check(!memcmp(pixels,reference,sizeof(pixels))&&!memcmp(&held,eff+child,sizeof(held)),"repeated smoke presentation advanced state");
 for(unsigned frame=0;frame<4;++frame)tick();const float heights[]={4.9f,4.7f,4.4f,4,3.5f,2.9f,2.2f,1.4f,0.5f,0};for(unsigned frame=0;frame<10;++frame){tick();check(fabsf(eff[child].py-heights[frame])<0.00001f,"original smoke fall timeline");}for(unsigned frame=0;frame<6;++frame)tick();check(!(eff[child].flg&1)&&!op->mode1,"original smoke did not expire/consumed cue retriggered");cam.mtx=previous_camera;printf("PASS pause/loading preserve event cue, original smoke falling/expiry and %u Quest GPU pixels; repeated rendering leaves state unchanged\n",lit);
 reset_pool();RxRoomEffects *pending=NULL;error=rx_room_effects_prepare(&pending);check(!error,error);memcpy(before,eff,sizeof(eff));short existing_map[256];memcpy(existing_map,sys->efid,sizeof(existing_map));check(rx_room_effects_publish(pending)!=NULL&&!memcmp(before,eff,sizeof(eff))&&!memcmp(existing_map,sys->efid,sizeof(existing_map))&&!rx_event_effect_mode_check(0,1),"second effect owner replaced live publication or changed pool");rx_room_effects_destroy(pending);check(!rx_event_effect_mode_check(0,1),"discarded staging owner retired live mapping");puts("PASS occupied publication rejects atomically and discarding a staged owner preserves the active context");
 reset_pool();for(unsigned index=14;index<256;++index)reject((unsigned char[]){0x91,index,1,0},4);for(unsigned bytes=1;bytes<4;++bytes)reject((unsigned char[]){0x91,0,1,0},bytes);
 short slot=sys->efid[0];sys->efid[0]=-1;reject((unsigned char[]){0x91,0,1,0},4);sys->efid[0]=512;reject((unsigned char[]){0x91,0,1,0},4);sys->efid[0]=slot;
 short flame=sys->efid[2];sys->efid[2]=sys->efid[3];reject((unsigned char[]){0x91,2,1,0},4);sys->efid[2]=flame;
 EF_WRK *records=rom->effp;rom->effp=(void*)(uintptr_t)1;reject((unsigned char[]){0x91,0,1,0},4);rom->effp=records;int count=rom->eff_n;rom->eff_n=13;reject((unsigned char[]){0x91,0,1,0},4);rom->eff_n=count;
 unsigned short id=records[0].id;records[0].id=9;reject((unsigned char[]){0x91,0,1,0},4);records[0].id=id;op=eff+slot;O_WRK saved=*op;op->id=9;reject((unsigned char[]){0x91,0,1,0},4);*op=saved;op->type=1;reject((unsigned char[]){0x91,0,1,0},4);*op=saved;op->flg&=~1u;reject((unsigned char[]){0x91,0,1,0},4);*op=saved;op->mtx=(void*)(uintptr_t)1;reject((unsigned char[]){0x91,0,1,0},4);*op=saved;op->pvp=(void*)(uintptr_t)1;reject((unsigned char[]){0x91,0,1,0},4);*op=saved;op->tvp=(void*)(uintptr_t)1;reject((unsigned char[]){0x91,0,1,0},4);*op=saved;
 O_WRK *overlay=eff+sys->efid[9];float depth=overlay->sz;overlay->sz=0;reject((unsigned char[]){0x91,9,1,0},4);overlay->sz=depth;
 puts("PASS malformed/truncated/index/mapping/same-family remap/foreign bank/retired slot/self-pointer and prospective-mode guards preserve the full effect pool");
 rx_events_reset();rom->evtp=original;rx_opening_close_graphics(&opening);rx_egl_close(&egl);rx_opening_end(&opening);check(rx_event_effect_mode_check(0,1)!=NULL,"retired room effect context accepted");puts("PASS room effect owner retired during teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
