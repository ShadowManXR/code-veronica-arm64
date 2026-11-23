#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_effects.h"
#include "effect_runtime.h"
#include "room_effects.h"
#include "room_scripts.h"
#include "main.h"
#include "android_egl.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <float.h>
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL effect work: %s\n",why);exit(1);}}
static unsigned char code[256];static RxRoomScripts scripts;
static O_WRK initial[512],snapshot[512];static short mapping[256];
static const char *run(unsigned record,unsigned pad,const unsigned char *commands,unsigned bytes,int delayed){
 memset(code,0,sizeof(code));code[0]=12;code[4]=18;code[8]=20;code[12]=0x14;
 code[20]=0x65;code[21]=4;code[22]=record;code[23]=pad;
 unsigned at=24;if(delayed)code[at++]=0xfe;if(bytes)memcpy(code+at,commands,bytes);at+=bytes;
 const unsigned char marker[]={5,1,30,0,0,0,0xff,0};memcpy(code+at,marker,sizeof(marker));
 check(!rx_room_scripts_open(code,at+sizeof(marker),&scripts),"effect task fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~2u;
 return rx_events_start_room();
}
static void reject(unsigned record,unsigned bytes){
 unsigned char data[32]={8,0,0,0,0,0,0,0,5,1,30,0,0,0,0x65,4,record,0};data[4]=14+bytes;
 RxRoomScripts bad;check(!rx_room_scripts_open(data,16+bytes,&bad),"invalid selection fixture");rom->evtp=&bad;
 memcpy(snapshot,eff,sizeof(eff));unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;
 check(rx_events_start_room()!=NULL&&!sys->ev_flg[0]&&!memcmp(snapshot,eff,sizeof(eff)),"bad selection executed prefix or changed effects");sys->ev_flg[0]=flags;
}
static const char *deferred(unsigned record,const unsigned char *command,unsigned bytes){const char *error=run(record,0,command,bytes,1);check(!error,error);return error;}
static void tick_reject(void){memcpy(snapshot,eff,sizeof(eff));check(rx_events_tick()!=NULL&&!memcmp(snapshot,eff,sizeof(eff)),"invalid effect task reached original common handler");}
static unsigned char pixels[640*480*4],reference[sizeof(pixels)],stationary[sizeof(pixels)];
static void capture(RxOpening *opening,unsigned char *out){
 const NJS_CNK_VIEW view={.1f,20000,320,320,240,1,1};glViewport(0,0,640,480);glDepthMask(GL_TRUE);glClearDepthf(1);glClearColor(32/255.0f,32/255.0f,32/255.0f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 const char *error=rx_overlay_present(&opening->particles,&view);check(!error,error);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,out);check(!glGetError(),"scripted overlay readback");
}
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);
 check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full opening stays gated at next original opcode");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 memcpy(initial,eff,sizeof(eff));memcpy(mapping,sys->efid,sizeof(mapping));
 unsigned selections=0;for(unsigned record=0;record<14;++record)for(unsigned pad=0;pad<256;++pad){
  error=run(record,pad,NULL,0,0);check(!error,error);O_WRK *work=NULL;check(!rx_event_effect_work(record,&work)&&work==eff+mapping[record]&&(uintptr_t)work>UINT32_MAX,"owned high-address effect work resolution");
  check(bhEtask[0].work==(BH_PWORK *)work&&!bhEtask[0].model_cno&&(sys->ev_flg[0]&2),"original effect LoadWork mapping/padding/task/continuation");++selections;
 }
 check(!memcmp(initial,eff,sizeof(eff))&&!memcmp(mapping,sys->efid,sizeof(mapping)),"selection changed effect pool/mapping");
 printf("PASS %u original effect work selections: 14 records/all padding bytes, exact native task target/model selector, unchanged effect pool\n",selections);
 float ground[32];memcpy(ground,rom->grand,sizeof(ground));memset(rom->grand,0,sizeof(ground));rom->grand[0]=-50;rom->grand[1]=-25;rom->grand[3]=10;rom->grand[4]=20;rom->grand[30]=50;
 unsigned cases=0;for(unsigned record=0;record<14;++record)for(unsigned sign=0;sign<8;++sign)for(unsigned value=0;value<256;++value){
  unsigned slot=mapping[record];eff[slot]=initial[slot];O_WRK expected=eff[slot];unsigned x=value*257,y=(255-value)*257,z=value*127;
  unsigned char sequence[]={0x69,12,sign&1,sign&2,sign&4,255,0x69,7,x,x>>8,y,y>>8,z,z>>8,
   0x69,5,value,255-value,17,255,0x69,2,255,0x69,13,sign&1,sign&2,sign&4,255,
   0x69,11,value,255-value,128,255,0x69,6,value,255-value,1,255,0x69,3,255};
  expected.px=expected.pxb=(sign&1?-1.0f:1.0f)*(x/100.0f);expected.py=expected.pyb=(sign&2?-1.0f:1.0f)*(y/100.0f);expected.pz=expected.pzb=(sign&4?-1.0f:1.0f)*(z/100.0f);
  float h=expected.py;expected.flr_no=h>=50?28:h>=20?2:h>=10?1:h>=0?0:h>=-25?-1:h>=-50?-2:0;
  expected.px+=(sign&1?-1.0f:1.0f)*(value*0.1f);expected.py+=(sign&2?-1.0f:1.0f)*((255-value)*0.1f);expected.pz+=(sign&4?-1.0f:1.0f)*(17*0.1f);
  expected.ax=expected.axb=(int)((sign&1?-(int)value:(int)value)*182.04445f);expected.ay=expected.ayb=(int)((sign&2?-(int)(255-value):(int)(255-value))*182.04445f);expected.az=expected.azb=(int)((sign&4?-128:128)*182.04445f);
  expected.ax+=(sign&1?-1:1)*((int)(value*(65536.0f/360.0f))>>1);expected.ay+=(sign&2?-1:1)*((int)((255-value)*(65536.0f/360.0f))>>1);expected.az+=(sign&4?-1:1)*((int)(65536.0f/360.0f)>>1);
  error=run(record,255,sequence,sizeof(sequence),0);check(!error,error);check(!memcmp(&expected,eff+slot,sizeof(expected)),"original whole effect transform differs from independent scalar reference");++cases;
 }
 memcpy(rom->grand,ground,sizeof(ground));memcpy(eff,initial,sizeof(eff));
 printf("PASS %u whole-effect transform sequences: all room records/eight signs/all byte values, unsigned positions/floors/backups, translation and rotation steps\n",cases);
 unsigned flag_cases=0;for(unsigned record=0;record<14;++record)for(unsigned selector=0;selector<4;++selector)for(unsigned pad=0;pad<256;++pad){unsigned slot=mapping[record];eff[slot]=initial[slot];eff[slot].flg^=0x40118u;O_WRK expected=eff[slot];
  const unsigned mask[]={~0x118u,0x118,~0x18u,0x18};expected.flg=selector&1?expected.flg|mask[selector]:expected.flg&mask[selector];
  error=run(record,0,(unsigned char[]){0x69,23,selector,pad},4,0);check(!error,error);check(!memcmp(&expected,eff+slot,sizeof(expected)),"effect display mask changed unrelated work");++flag_cases;}
 for(unsigned operand=0;operand<65536;++operand){unsigned slot=mapping[9];eff[slot]=initial[slot];eff[slot].flg^=0x40000u;O_WRK expected=eff[slot];expected.flg=operand?expected.flg|0x40000u:expected.flg&~0x40000u;
  error=run(9,0,(unsigned char[]){0x69,41,operand,operand>>8},4,0);check(!error,error);check(!memcmp(&expected,eff+slot,sizeof(expected)),"effect word-controlled mask changed unrelated work");++flag_cases;}
 memcpy(eff,initial,sizeof(eff));printf("PASS %u whole-effect display/word-controlled flag cases: all records/selectors/padding and every 16-bit word\n",flag_cases);
 /* Actual opening effect 9 is the first screen overlay. Execute its authored
    transform prefix and the original 250-frame For/Next translation loop. */
 check(!memcmp(original->data+6694,(unsigned char[]){0x65,4,9,0},4),"authored effect selection");
 error=run(9,0,original->data+6698,40,0);check(!error,error);
 for(unsigned frame=0;frame<251;++frame){error=rx_events_tick();check(!error,error);}
 O_WRK *overlay=eff+mapping[9];float wanted=300;for(unsigned frame=0;frame<251;++frame)wanted-=12*0.1f;
 check(overlay->px==74&&overlay->py==wanted&&overlay->pz==0&&overlay->pxb==74&&overlay->pyb==300,"authored opening overlay trajectory");
 puts("PASS actual opening effect selection/position/step bytes and suspended 250-iteration For/Next overlay trajectory");
 /* Initialize the original overlay controller before positioning it, as the
    live effect phase does. Compare its moved GPU image with direct scalar work. */
 RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"effect work EGL");gs_reset();rx_effects_reset();memcpy(eff,initial,sizeof(eff));memcpy(sys->efid,mapping,sizeof(mapping));
 unsigned flags=sys->sp_flg,game=sys->gm_flg;sys->sp_flg|=8;sys->gm_flg&=~1u;
 for(unsigned i=0;i<512;++i)if(i!=(unsigned)mapping[9])eff[i].stflg|=0x1000000u;
 error=rx_effects_tick();check(!error,error);overlay=eff+mapping[9];O_WRK initialized=*overlay;overlay->mode1=1;
 error=run(9,0,original->data+6698,26,0);check(!error,error);error=rx_effects_tick();check(!error,error);capture(&opening,stationary);
 error=run(9,0,original->data+6698,40,0);check(!error,error);for(unsigned frame=0;frame<120;++frame){error=rx_events_tick();check(!error,error);}
 wanted=300;for(unsigned frame=0;frame<121;++frame)wanted-=12*0.1f;check(overlay->py==wanted,"intermediate original overlay position");
 error=rx_effects_tick();check(!error,error);capture(&opening,pixels);O_WRK held=*overlay;capture(&opening,reference);
 check(!memcmp(pixels,reference,sizeof(pixels))&&!memcmp(&held,overlay,sizeof(held)),"overlay presentation advanced script/controller state");
 *overlay=initialized;overlay->mode1=1;overlay->px=overlay->pxb=74;overlay->py=wanted;overlay->pyb=300;overlay->pz=overlay->pzb=0;
 error=rx_effects_tick();check(!error,error);capture(&opening,reference);check(!memcmp(pixels,reference,sizeof(pixels)),"scripted overlay GPU image differs from independent native position");
 unsigned moved=0,visible=0;for(unsigned i=0;i<640*480;++i){moved+=memcmp(stationary+i*4,pixels+i*4,4)!=0;visible+=pixels[i*4]!=32||pixels[i*4+1]!=32||pixels[i*4+2]!=32;}
 check(moved>10&&visible>10,"authored overlay GPU movement absent");printf("PASS authored scripted overlay GLES movement: %u changed/%u visible pixels, exact direct-position image and repeat-render stability\n",moved,visible);
 rx_opening_close_graphics(&opening);rx_egl_close(&egl);sys->sp_flg=flags;sys->gm_flg=game;rx_effects_reset();memcpy(sys->efid,mapping,sizeof(mapping));
 memcpy(eff,initial,sizeof(eff));for(unsigned record=14;record<256;++record)reject(record,4);for(unsigned bytes=1;bytes<4;++bytes)reject(9,bytes);
 int slot=mapping[9];short saved_slot=sys->efid[9];sys->efid[9]=511;reject(9,4);sys->efid[9]=saved_slot;
 eff[slot].flg&=~1u;reject(9,4);eff[slot]=initial[slot];eff[slot].type++;reject(9,4);eff[slot]=initial[slot];eff[slot].mtx=(void *)(uintptr_t)1;reject(9,4);eff[slot]=initial[slot];
 EF_WRK record=rom->effp[9];rom->effp[9].id++;reject(9,4);rom->effp[9]=record;EF_WRK *bank=rom->effp;rom->effp=(void *)(uintptr_t)1;reject(9,4);rom->effp=bank;
 const unsigned char step[]={0x69,2,0};
 deferred(9,step,3);sys->efid[9]=511;tick_reject();sys->efid[9]=saved_slot;
 deferred(9,step,3);bhEtask[0].work=(void *)((uintptr_t)(eff+slot)+1);tick_reject();
 deferred(9,step,3);bhEtask[0].work=(void *)(eff+511);tick_reject();
 deferred(9,step,3);bhEtask[0].addpy=NAN;tick_reject();
 deferred(9,step,3);eff[slot].py=FLT_MAX;bhEtask[0].addpy=FLT_MAX;tick_reject();eff[slot]=initial[slot];
 deferred(9,(unsigned char[]){0x69,7,0,0,0,0,0,0},8);rom->grand[0]=NAN;tick_reject();memcpy(rom->grand,ground,sizeof(ground));
 unsigned char unsupported[24]={8,0,0,0,22,0,0,0,5,1,30,0,0,0,0x65,4,9,0,0x69,36,0,0};RxRoomScripts bad;
 check(!rx_room_scripts_open(unsupported,sizeof(unsupported),&bad),"unsupported model preflight fixture");rom->evtp=&bad;sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"unsupported effect model operation executed prefix");
 deferred(9,step,3);code[26]=36;tick_reject();
 puts("PASS malformed/remapped/retired/foreign effect selection and task guards, nonfinite/overflow translation, floor bank and unsupported model operations");
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);O_WRK *out=(void *)(uintptr_t)1;check(rx_event_effect_work(9,&out)!=NULL&&out==(void *)(uintptr_t)1&&rx_event_effect_work_check(eff+slot)!=NULL,"retired effect context accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);
 puts("PASS effect work registry retirement and output preservation");return 0;
}
