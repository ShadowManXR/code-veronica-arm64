#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event.h"
#include "main.h"
#include "android_egl.h"
#include <GLES3/gl3.h>
#include "room_scripts.h"
#include "ps2_dummy.h"
#include "ps2_event.h"
#include "ps2_NaView.h"
#include "ps2/vu0.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL camera: %s\n",why);exit(1);}}
extern unsigned Event_T_timer;
extern float ClipDispW,ClipDispH;
static unsigned char code[16]={8,0,0,0,14,0,0,0,0x13,0,0,0,0,0,0,0};
static RxRoomScripts script;
static const char *run(unsigned mode,unsigned camera,unsigned key){
    code[9]=mode;code[10]=camera;code[11]=key;
    check(!rx_room_scripts_open(code,sizeof(code),&script),"test script bank");
    rom->evtp=&script;return rx_events_start_room();
}
static unsigned bit(const unsigned *words,unsigned i){return !!(words[i/32]&(0x80000000u>>(i%32)));}
static void reject(unsigned mode,unsigned camera,unsigned key){
    CAM_WORK before=cam;unsigned status=sys->st_flg,game=sys->gm_flg;int timer=Event_T_timer;
    check(run(mode,camera,key)!=NULL,"invalid camera request executed");
    check(!memcmp(&before,&cam,sizeof(cam))&&sys->st_flg==status&&sys->gm_flg==game&&Event_T_timer==timer,"invalid camera request changed state");
}
int main(int argc,char **argv){
check(argc==2,"supply OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);
error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"campaign must stay gated");
printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next campaign dependency");
RxRoomScripts *original=rom->evtp;
check(original->bytes>1758&&original->data[1754]==0x13,"actual opening CamSet bytes");
unsigned char actual[4];memcpy(actual,original->data+1754,4);
CAM_WORK saved=cam;unsigned saved_ts=sys->ts_flg;int saved_background=BackColorFlag;
unsigned cases=0;
for(unsigned pass=0;pass<8;++pass){
 sys->ts_flg=pass&1?0x200:0;BackColorFlag=pass&2?1:0;
 for(int c=0;c<rom->evc_n;++c)for(int k=0;k<rom->evcp[c].keyf_n;++k){
 CAM_KEYF_WORK *key=&rom->evcp[c].keyf[k];if(key->lkflg)continue;
 cam=saved;cam.flg=0xabcdeffe;cam.mode0=3;cam.mode1=4;cam.mode2=5;cam.mode3=6;
 sys->gm_flg=0x5100;sys->st_flg=2;sys->evc_no=31;sys->evc_kn=7;sys->fog_cngct=pass*2;
 CAM_WORK expected=cam;expected.flg=((expected.flg&~0x46)|2)&~0x38;
 expected.evc_no=c;expected.keyf_no=k;expected.plx=plp->px;expected.plz=plp->pz;
 expected.px=expected.pxp=key->px;expected.py=expected.pyp=key->py;expected.pz=expected.pzp=key->pz;
 expected.ax=expected.axp=key->ax;expected.ay=expected.ayp=key->ay;expected.az=expected.azp=key->az;
 expected.ln=0;expected.pers=key->pers;expected.mode0=expected.mode1=expected.mode2=expected.mode3=0;
 expected.fog_col=key->fog_col;expected.fog_nr=key->fog_nr;expected.fog_fr=key->fog_fr;
 CAM_KEYF_WORK immutable=*key;uint64_t bg=Db.clear0.rgbaq;
 check(!run(0,c,k),"valid original camera failed");
 check(!memcmp(&cam,&expected,sizeof(cam)),"original CamSet complete camera state differs");
 check(!memcmp(key,&immutable,sizeof(*key)),"unlocked camera key was modified");
 check(sys->gm_flg==0x5130&&sys->st_flg==3&&sys->evc_ono==31&&sys->evc_okn==7&&sys->evc_no==c&&sys->evc_kn==k&&sys->fog_ct==((key->flg&8)&&pass*2>4?(int)pass*2-4:0),"original camera system state differs");
 for(unsigned n=0;n<rom->mdl.obj_num;++n)if(rom->mdl.objP[n].model)check(!!(rom->mdl.objP[n].evalflags&8)==bit(key->hidobj,n),"camera object hide bits");
 for(int n=0;n<rom->evl_n;++n)check(!!(rom->evlp[n].flg&2)!=bit(key->hidlgt,n),"camera light hide bits");
 check(fNaViwClipNear==(pass&1?-2.0f:-1.0f)&&fNaViwClipFar==(pass&1?-20000.0f:-99.0f),"original camera clip defaults");
 if(BackColorFlag){unsigned col=rom->bak_col;uint64_t rgba=((col>>16)&255)|((uint64_t)((col>>8)&255)<<8)|((uint64_t)(col&255)<<16)|((uint64_t)(col>>24)<<24);check(Db.clear0.rgbaq==rgba&&Db.clear1.rgbaq==rgba,"original camera background register");}
 else check(Db.clear0.rgbaq==bg,"disabled background changed");
 for(unsigned i=0;i<16;++i)check(isfinite(((float *)ClipMatrix2)[i]),"native clip matrix is nonfinite");
 for(unsigned r=0;r<4;++r)for(unsigned lane=0;lane<4;++lane)check(g_vu0.vf[24+r].f[lane]==ClipMatrix2[r][lane],"full-width clip matrix VU transfer");
 check(fabsf(ClipMatrix2[0][0]-_nj_screen_.dist/320.0f)<0.00001f&&fabsf(ClipMatrix2[1][1]-_nj_screen_.dist/240.0f)<0.00001f&&
       fabsf(ClipMatrix2[2][2]-(_fNaViwClipFar+_fNaViwClipNear)/(_fNaViwClipFar-_fNaViwClipNear))<0.00001f,
       "original clip matrix independent projection reference");
 check(ClipScreenMatrix[2][3]==ClipDispW&&ClipScreenMatrix[2][2]==ClipDispH,"full-width clip-display stores");++cases;
 }
}
check(!run(actual[1],actual[2],actual[3]),"actual RM_0000 camera bytes");
for(unsigned i=rom->evc_n;i<256;++i)reject(0,i,0);
for(unsigned i=rom->evcp[0].keyf_n;i<256;++i)reject(0,0,i);
for(unsigned i=2;i<256;++i)reject(i,0,0);
sys->ts_flg|=0x200;char old_room=sys->rom_no;sys->rom_no=2;
check(!run(0,1,0)&&fNaViwClipNear==-24&&fNaViwClipFar==-5000,"original authored view clip override");sys->rom_no=old_room;
char old_stage=sys->stg_no,old_case=sys->rcase;sys->stg_no=3;sys->rom_no=23;sys->rcase=0;BackColorFlag=1;
check(!run(0,6,0)&&((uint64_t)Db.clear0.rgbaq&0xffffff)==0x806838,"original authored background override");
sys->rom_no=15;reject(0,3,0);sys->stg_no=old_stage;sys->rom_no=old_room;sys->rcase=old_case;
CAM_KEYF_WORK *key=&rom->evcp[0].keyf[0],old=*key;
key->px=NAN;reject(0,0,0);*key=old;key->pers=0;reject(0,0,0);*key=old;key->lkflg=1;reject(0,0,0);*key=old;
int old_count=rom->evcp[0].keyf_n;rom->evcp[0].keyf_n=17;reject(0,0,0);rom->evcp[0].keyf_n=old_count;
unsigned models=rom->mdl.obj_num;rom->mdl.obj_num=257;reject(0,0,0);rom->mdl.obj_num=models;
int lights=rom->evl_n;rom->evl_n=129;reject(0,0,0);rom->evl_n=lights;
EVC_WORK *bank=rom->evcp;rom->evcp=NULL;reject(0,0,0);rom->evcp=bank;
char stage=sys->stg_no;sys->stg_no=10;reject(0,0,0);sys->stg_no=stage;
for(unsigned length=1;length<4;++length){
 unsigned char truncated[14]={8,0,0,0};truncated[4]=8+length;truncated[8]=0x13;
 RxRoomScripts short_script;check(!rx_room_scripts_open(truncated,10+length,&short_script),"truncated fixture");
 rom->evtp=&short_script;CAM_WORK before=cam;check(rx_events_start_room()&&!memcmp(&before,&cam,sizeof(cam)),"truncated camera changed state");
}
/* Preflight must reject a bad camera before a preceding flag command runs. */
unsigned char prefixed[22]={8,0,0,0,20,0,0,0,5,1,0,0,0,0,0x13,0,255,0,0,0,0,0};
RxRoomScripts bad;check(!rx_room_scripts_open(prefixed,sizeof(prefixed),&bad),"prefixed bank");rom->evtp=&bad;
unsigned flag=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()&&sys->ev_flg[0]==0,"camera preflight executed prefix");sys->ev_flg[0]=flag;
/* The same original commands feed the real GLES host's clipping and clear. */
RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"camera GPU EGL");
sys->ts_flg|=0x200;BackColorFlag=1;unsigned old_color=rom->bak_col;rom->bak_col=0xff123456;
check(!run(0,0,0),"camera GPU setup");sys->sp_flg&=~0x80u;glViewport(0,0,640,480);
error=rx_opening_present(&opening);check(!error,error);
static unsigned char image[640*480*4];glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,image);check(!glGetError(),"camera GPU readback");
unsigned visible=0;for(unsigned i=0;i<sizeof(image);i+=4)visible+=image[i]!=0x12||image[i+1]!=0x34||image[i+2]!=0x56;
check(visible>100,"original camera did not show room");
njClipZ(-60000,-65535);error=rx_opening_present(&opening);check(!error,error);
glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,image);check(!glGetError(),"clipped camera GPU readback");
for(unsigned i=0;i<sizeof(image);i+=4)check(image[i]==0x12&&image[i+1]==0x34&&image[i+2]==0x56&&image[i+3]==255,"GLES host did not use original background and clip range");
printf("PASS camera GLES presentation: %u visible pixels; original near/far removes geometry and preserves exact background color\n",visible);
rom->bak_col=old_color;rx_opening_close_graphics(&opening);rx_egl_close(&egl);
rom->evtp=original;cam=saved;sys->ts_flg=saved_ts;BackColorFlag=saved_background;
printf("PASS %u original CamSet states, actual opening bytes, hide masks, fog/background/near-far/VU transfer, bounded invalid selectors and atomic preflight rejection\n",cases);
rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}

