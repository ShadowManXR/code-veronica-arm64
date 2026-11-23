#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_effects.h"
#include "effect_runtime.h"
#include "room_scripts.h"
#include "effect.h"
#include "main.h"
#include "android_egl.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL effect clear: %s\n",why);exit(1);}}
static O_WRK initial[512],snapshot[512];static short mapping[256];
static unsigned char code[32];static RxRoomScripts scripts;
static const char *run(unsigned pad){memset(code,0,sizeof(code));code[0]=8;code[4]=22;unsigned char commands[]={5,1,30,0,0,0,0xca,pad,5,1,29,0,0,0};memcpy(code+8,commands,sizeof(commands));check(!rx_room_scripts_open(code,24,&scripts),"clear fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~6u;return rx_events_start_room();}
static int cleared(unsigned id){static const unsigned ids[]={5,11,22,24,25,27,113,114,136,159,225,250,253,254,256,257,260,272,296,297,298,301,302,398};for(unsigned i=0;i<sizeof(ids)/sizeof(*ids);++i)if(ids[i]==id)return 1;return 0;}
static int needs_payload(unsigned id){return id==254||id==260||id==302;}
static void one(unsigned id,unsigned flags,unsigned pad,unsigned *cases){unsigned slot=id&511;O_WRK before=eff[slot];eff[slot].id=id;eff[slot].flg=flags;eff[slot].exp0=eff[slot].exp1=NULL;O_WRK expected=eff[slot];
 const char *error=run(pad);if((flags&1)&&needs_payload(id)){check(error&&!(sys->ev_flg[0]&6),"unowned required payload executed prefix");}else{check(!error,error);check((sys->ev_flg[0]&6)==6,"clear length/continuation");if((flags&1)&&cleared(id))expected.flg=0;}
 check(!memcmp(&expected,eff+slot,sizeof(expected)),"whole work differs from independent cleanup classification");eff[slot]=before;++*cases;
}
static void reject(void){memcpy(snapshot,eff,sizeof(eff));check(run(255)!=NULL&&!(sys->ev_flg[0]&6)&&!memcmp(snapshot,eff,sizeof(eff)),"invalid payload mutated prefix or effect pool");}
static unsigned char pixels[640*480*4],reference[sizeof(pixels)];
static void capture(RxOpening *opening,unsigned char *out){const NJS_CNK_VIEW view={2,20000,320,320,240,1,1};glViewport(0,0,640,480);glDepthMask(GL_TRUE);glClearDepthf(1);glClearColor(32/255.0f,32/255.0f,32/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);const char *error=rx_particle_present(&opening->particles,&view);check(!error,error);error=rx_overlay_present(&opening->particles,&view);check(!error,error);error=rx_callback_present(&opening->particles,&view);check(!error,error);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,out);check(!glGetError(),"surviving effects GPU readback");}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};RxEgl egl={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full opening stays gated at live runtime");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 memcpy(initial,eff,sizeof(eff));memcpy(mapping,sys->efid,sizeof(mapping));
 O_WRK canary;memset(&canary,0xa5,sizeof(canary));canary.flg=2;for(unsigned i=0;i<512;++i)eff[i]=canary;
 unsigned cases=0;for(unsigned id=0;id<65536;++id){one(id,0,0,&cases);one(id,0xffffffffu,255,&cases);one(id,0x80000001u,id&255,&cases);}
 for(unsigned id=0;id<65536;++id)if(cleared(id))for(unsigned bit=0;bit<32;++bit){one(id,1u|(1u<<bit),bit,&cases);one(id,1u|~(1u<<bit),bit,&cases);}
 for(unsigned i=0;i<512;++i)check(!memcmp(&canary,eff+i,sizeof(canary)),"cleanup changed inactive/reserved pool canaries");
 printf("PASS %u whole-work cleanup cases: all 65536 IDs, inactive/high-bit flags, every flag bit for all 24 target families; three unowned payload families reject atomically\n",cases);
 /* Each pool slot participates, including both ends of every original pass. */
 for(unsigned slot=0;slot<512;++slot){eff[slot].id=slot&1?301:250;eff[slot].flg=0x80000001;O_WRK expected=eff[slot];expected.flg=0;error=run(slot&255);check(!error,error);check(!memcmp(&expected,eff+slot,sizeof(expected)),"pool boundary cleanup differs");eff[slot]=canary;}
 const unsigned nullable[]={22,24,25,27},required[]={254,260,302};const uintptr_t pointers[]={1,(uintptr_t)&eff[300].flg,UINT64_C(0x100000001)};
 eff[0].id=5;eff[0].flg=1;for(unsigned family=0;family<4;++family)for(unsigned p=0;p<3;++p){eff[511].id=nullable[family];eff[511].flg=1;eff[511].exp0=(void *)pointers[p];reject();}eff[511]=canary;
 for(unsigned family=0;family<3;++family){eff[511].id=required[family];eff[511].flg=1;eff[511].exp0=eff[511].exp1=NULL;reject();}eff[511]=canary;
 unsigned char deferred[]={12,0,0,0,18,0,0,0,20,0,0,0,0x14,0,0,0,0,0,0,0,0xfe,0xca,0,5,1,29,0,0,0,0xff,0};RxRoomScripts delayed;check(!rx_room_scripts_open(deferred,sizeof(deferred),&delayed),"deferred clear fixture");rom->evtp=&delayed;check(!rx_events_start_room(),"start deferred clear");eff[511].id=22;eff[511].flg=1;eff[511].exp0=(void *)(uintptr_t)1;memcpy(snapshot,eff,sizeof(eff));check(rx_events_tick()!=NULL&&!memcmp(snapshot,eff,sizeof(eff)),"late invalid payload partially cleared pool");
 memcpy(eff,initial,sizeof(eff));memcpy(sys->efid,mapping,sizeof(mapping));for(unsigned pad=0;pad<256;++pad){error=run(pad);check(!error,error);check(!memcmp(initial,eff,sizeof(eff))&&!memcmp(mapping,sys->efid,sizeof(mapping)),"real opening effects/mapping changed by selective clear");}
 unsigned char truncated[]={8,0,0,0,15,0,0,0,5,1,30,0,0,0,0xca,0,0};RxRoomScripts short_script;check(!rx_room_scripts_open(truncated,sizeof(truncated),&short_script),"truncated clear fixture");rom->evtp=&short_script;sys->ev_flg[0]&=~2u;memcpy(snapshot,eff,sizeof(eff));check(rx_events_start_room()!=NULL&&!(sys->ev_flg[0]&2)&&!memcmp(snapshot,eff,sizeof(eff)),"truncated clear executed prefix or changed work");
 check(original->bytes>7626&&!memcmp(original->data+7282,(unsigned char[]){0xca,0},2)&&!memcmp(original->data+7624,(unsigned char[]){0xca,0},2),"actual opening cleanup bytes");
 for(unsigned record=0;record<14;++record){O_WRK *work=NULL;check(!rx_event_effect_work(record,&work)&&work==eff+mapping[record],"unrelated published room effect retired");}
 puts("PASS all 512 pool slots, foreign/null/aliased payload atomic preflight and late runtime rejection, all padding bytes and both authored opening cleanup sites preserve the 14 room effects/mapping");
 check(rx_egl_open(&egl,NULL,640,480),"effect clear EGL");gs_reset();sys->sp_flg|=8;sys->ef_flg=0;sys->gm_flg&=~1u;error=rx_effects_tick();check(!error,error);O_WRK *overlay=eff+mapping[9];overlay->mode1=1;overlay->px=74;overlay->py=120;error=rx_effects_tick();check(!error,error);
 int trs=sys->ef_trsn,screen=sys->ef_trs2dn,callbacks=sys->ef_fncn,modifiers=sys->ef_mdfn;capture(&opening,pixels);memcpy(snapshot,eff,sizeof(eff));error=run(0);check(!error,error);
 check(!memcmp(snapshot,eff,sizeof(eff))&&sys->ef_trsn==trs&&sys->ef_trs2dn==screen&&sys->ef_fncn==callbacks&&sys->ef_mdfn==modifiers,"selective clear reset native effect state/queues");capture(&opening,reference);check(!memcmp(pixels,reference,sizeof(pixels)),"unrelated native effect GPU frame changed");unsigned visible=0;for(unsigned i=0;i<640*480;++i)visible+=pixels[i*4]!=32||pixels[i*4+1]!=32||pixels[i*4+2]!=32;check(visible>100,"surviving effects GPU frame empty");printf("PASS unchanged original render queues/cache/work and exact %u visible Quest GPU pixels after selective cleanup\n",visible);
 rx_events_reset();rom->evtp=original;rx_opening_close_graphics(&opening);rx_egl_close(&egl);rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
