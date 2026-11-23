#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event.h"
#include "room_scripts.h"
#include "room.h"
#include "android_egl.h"
#include "ps2_NaView.h"
#include <GLES3/gl3.h>
#include "main.h"
#include "ps2_dummy.h"
#include "ps2_NaMatrix.h"
#include "player_control_runtime.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL camera init: %s\n",why);exit(1);}}
typedef double Matrix[16];
static void identity(Matrix m){memset(m,0,sizeof(Matrix));m[0]=m[5]=m[10]=m[15]=1;}
static float sine(int angle){unsigned n=(unsigned)angle&65535,index=n&16383;if(n&16384)index=16383-index;float s=sinf(0.0000958738f*index);return n&32768?-s:s;}
static void product(Matrix a,const Matrix b){Matrix r;for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row){double v=0;for(unsigned k=0;k<4;++k)v+=a[k*4+row]*b[c*4+k];r[c*4+row]=v;}memcpy(a,r,sizeof(r));}
static void rotation(Matrix m,unsigned axis,int angle){Matrix r;identity(r);unsigned p=(axis+1)%3,q=(axis+2)%3;double s=sine(angle),c=sine(angle+16384);r[p*4+p]=r[q*4+q]=c;r[p*4+q]=s;r[q*4+p]=-s;product(m,r);}
static void transform(Matrix m,float x,float y,float z,int ax,int ay,int az){Matrix t;identity(t);t[12]=x;t[13]=y;t[14]=z;product(m,t);rotation(m,2,az);rotation(m,1,ay);rotation(m,0,ax);}
static unsigned channels;static double maximum;
static void matrix(const float *got,const Matrix expected){for(unsigned i=0;i<16;++i){double d=fabs(got[i]-expected[i]);if(d>maximum)maximum=d;check(isfinite(got[i])&&d<0.001,"native world/link matrix differs from independent hierarchy");++channels;}}
static unsigned char bytes[16];static RxRoomScripts script;
static const char *run(unsigned padding){memset(bytes,0,sizeof(bytes));bytes[0]=8;bytes[4]=12;bytes[8]=0x5b;bytes[9]=padding;check(!rx_room_scripts_open(bytes,14,&script),"camera init fixture");rom->evtp=&script;return rx_events_start_room();}
static void view_reference(void){
 Matrix view;for(unsigned i=0;i<16;++i)view[i]=(*cmat)[i];
 Matrix t;identity(t);t[14]=-cam.ln;product(view,t);rotation(view,2,cam.az);rotation(view,0,cam.ax);rotation(view,1,cam.ay);
 identity(t);t[12]=-(cam.px+cam.ofx);t[13]=-(cam.py+cam.ofy);t[14]=-(cam.pz+cam.ofz);product(view,t);matrix((float *)cam.mtx,view);
 Matrix world;identity(world);t[12]=cam.px+cam.ofx;t[13]=cam.py+cam.ofy;t[14]=cam.pz+cam.ofz;product(world,t);rotation(world,1,cam.ay);rotation(world,0,cam.ax);rotation(world,2,cam.az);
 check(fabs(cam.wpx-(world[12]-cam.ln*world[8]))<0.001&&fabs(cam.wpy-(world[13]-cam.ln*world[9]))<0.001&&fabs(cam.wpz-(world[14]-cam.ln*world[10]))<0.001,"camera world position reference");
 check(fabs(cam.vx-world[8])<0.00001&&fabs(cam.vy-world[9])<0.00001&&fabs(cam.vz+world[10])<0.00001,"camera view direction reference");
}

static unsigned bit(const unsigned *words,unsigned i){return !!(words[i/32]&(0x80000000u>>(i%32)));}
static int cut_reference(float x,float z,char floor){
 for(int c=0;c<rom->cut_n;++c)if(rom->cutp[c].flg&1)for(unsigned a=0;a<rom->cutp[c].ctab_n;++a){const CUT_WRK *r=rom->cutp[c].cuttp+a;
  if(r->flr_no!=floor||x<r->minx||z<r->minz)continue;
  if(!r->atr_tp){if(x<r->maxx&&z<r->maxz)return c;continue;}
  if(x>r->maxx||z>r->maxz)continue;
  double u=((double)x-r->minx)/(r->maxx-r->minx),v=((double)z-r->minz)/(r->maxz-r->minz);
  if((r->atr_tp==1&&u+v>1)||(r->atr_tp==2&&v>u)||(r->atr_tp==3&&u+v<1)||(r->atr_tp==4&&u>v))return c;
 }return -1;
}
static void reject(void){
 unsigned char data[20]={8,0,0,0,16,0,0,0,5,1,30,0,0,0,0x5b,0};RxRoomScripts bad;
 check(!rx_room_scripts_open(data,18,&bad),"rejection fixture");rom->evtp=&bad;
 CAM_WORK before=cam;BH_PWORK player=*plp;unsigned st=sys->st_flg,gm=sys->gm_flg,flags=sys->ev_flg[0];sys->ev_flg[0]=0;
 check(rx_events_start_room()!=NULL&&!sys->ev_flg[0]&&!memcmp(&before,&cam,sizeof(cam))&&!memcmp(&player,plp,sizeof(player))&&sys->st_flg==st&&sys->gm_flg==gm,"invalid native camera executed prefix or changed view/player");sys->ev_flg[0]=flags;
}
int main(int argc,char **argv){check(argc==2,"supply OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"full opening must stay gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next campaign dependency");
 error=bhCheckNativeCameraInit();check(!error,error);
 check(original->bytes>2240&&original->data[2238]==0x5b,"actual opening InitCamSet bytes");
 CAM_WORK saved=cam;BH_PWORK actor=*plp;EXP_WORK scalar=opening.player_work;unsigned sp=sys->sp_flg;
 for(unsigned padding=0;padding<256;++padding){cam=saved;*plp=actor;opening.player_work=scalar;sys->sp_flg=sp;sys->st_flg|=0x1000001;sys->gm_flg&=~0x40u;
  error=run(padding);check(!error,error);check(plp->gpx==plp->px&&plp->gpy==plp->py&&plp->gpz==plp->pz,"original camera player position snapshot");
  check(!(sys->st_flg&0x1000001)&&!(cam.flg&2)&&sys->lgtp==rom->lgtp,"original room-camera/light handoff");view_reference();
 }
 printf("PASS all ignored InitCamSet padding bytes, room camera/light handoff and %u independent view channels (max error %.9g)\n",channels,maximum);

 unsigned samples=0;
 for(int c=0;c<rom->cut_n;++c)for(unsigned a=0;a<rom->cutp[c].ctab_n;++a)for(unsigned u=0;u<3;++u)for(unsigned v=0;v<3;++v)for(unsigned version=0;version<2;++version){
  CUT_WRK *area=rom->cutp[c].cuttp+a;cam=saved;*plp=actor;opening.player_work=scalar;sys->sp_flg=sp;sys->ts_flg=(sys->ts_flg&~0x200u)|(version?0:0x200);sys->gm_flg=(sys->gm_flg&~0x40u)|1;cam.camver=version;
  plp->px=area->minx+(area->maxx-area->minx)*(0.17f+0.31f*u);plp->pz=area->minz+(area->maxz-area->minz)*(0.19f+0.29f*v);plp->py=0;plp->flr_no=area->flr_no;
  int selected=cut_reference(plp->px,plp->pz,plp->flr_no);if(selected<0)selected=cam.ncut;
  int oldcut=cam.ncut;error=run(0);check(!error,error);check(cam.ncut==selected&&cam.ocut==oldcut&&cam.camver==(int)version,"native cut selection differs from independent geometry");
  CAM_WRK *key=&rom->cutp[selected].cam[version];check(opening.player_work.plx==plp->px&&opening.player_work.plz==plp->pz&&opening.player_work.ply==plp->py+key->h,"original active-camera player scalar publication");
  for(unsigned n=0;n<rom->mdl.obj_num;++n)if(rom->mdl.objP[n].model)check(!!(rom->mdl.objP[n].evalflags&8)==bit(key->hidobj,n),"return-to-room object visibility");
  if(!version)check(fNaViwClipNear==(selected==1?-10.0f:-2.0f)&&fNaViwClipFar==(selected==1?-1000.0f:-20000.0f),"original room near/far table reset");
  view_reference();++samples;
 }
 printf("PASS %u authored room-camera positions/versions, independent cut geometry, player scalar work and hide masks\n",samples);
 cam=saved;*plp=actor;opening.player_work=scalar;sys->sp_flg=sp;sys->gm_flg&=~0x41u;
 /* CamSet mode 1 ignores both trailing bytes and does not snapshot player position,
    publish a view matrix or run lighting. Its original clip reset still applies. */
 for(unsigned enhanced=0;enhanced<2;++enhanced)for(unsigned ignored=0;ignored<256;++ignored){
  sys->ts_flg=(sys->ts_flg&~0x200u)|(enhanced?0x200:0);
  memset(bytes,0,sizeof(bytes));bytes[0]=8;bytes[4]=14;bytes[8]=0x13;bytes[9]=1;bytes[10]=ignored;bytes[11]=255-ignored;
  check(!rx_room_scripts_open(bytes,16,&script),"camera return fixture");rom->evtp=&script;
  float before[16];memcpy(before,cam.mtx,sizeof(before));plp->gpx=plp->px;plp->gpy=plp->py;plp->gpz=plp->pz;
  error=rx_events_start_room();check(!error,error);check(!(sys->st_flg&1)&&!(cam.flg&0x46)&&!memcmp(before,cam.mtx,sizeof(before)),"original CamSet return unexpectedly published view");
  check(fNaViwClipNear==(enhanced?GameNear:-1)&&fNaViwClipFar==(enhanced?GameFar:-99),"original return camera clip range");
 }
 puts("PASS 512 CamSet return states, ignored operand bytes, both original clip modes and deferred view publication");
 cam=saved;*plp=actor;opening.player_work=scalar;sys->sp_flg=sp;sys->gm_flg&=~0x40u;
 CUT_WORK *cut_bank=rom->cutp;rom->cutp=(void *)(uintptr_t)1;reject();rom->cutp=cut_bank;
 CUT_WRK *areas=rom->cutp[0].cuttp;rom->cutp[0].cuttp=(void *)(uintptr_t)1;reject();rom->cutp[0].cuttp=areas;
 unsigned char count=rom->cutp[0].ctab_n;rom->cutp[0].ctab_n=count+1;reject();rom->cutp[0].ctab_n=count;
 LGT_WORK *lights=rom->lgtp;rom->lgtp=(void *)(uintptr_t)1;reject();rom->lgtp=lights;
 NJS_MATRIX *basis=cmat;cmat=(void *)(uintptr_t)1;reject();cmat=basis;
 NJS_MATRIX *matrix_owned=cam.mtx;cam.mtx=(void *)(uintptr_t)1;reject();cam.mtx=matrix_owned;
 int oldcut=cam.ncut;cam.ncut=rom->cut_n;reject();cam.ncut=oldcut;
 char floor=plp->flr_no;plp->flr_no=-3;reject();plp->flr_no=floor;
 float value=rom->cutp[0].cam[0].w;rom->cutp[0].cam[0].w=NAN;reject();rom->cutp[0].cam[0].w=value;
 float extent=rom->cutp[0].cw;rom->cutp[0].cw=0;reject();rom->cutp[0].cw=extent;
 unsigned link=rom->lgtp[0].lkflg;rom->lgtp[0].lkflg=5;reject();rom->lgtp[0].lkflg=link;
 sys->gm_flg|=0x40;reject();sys->gm_flg&=~0x40u;char oldroom=sys->rom_no;sys->rom_no=7;sys->ts_flg|=0x200;reject();sys->rom_no=oldroom;sys->ts_flg&=~0x200u;sys->sp_flg&=~0x40u;reject();sys->sp_flg=sp;
 unsigned char *scalar_owned=plp->exp0;plp->exp0=(void *)(uintptr_t)1;reject();plp->exp0=scalar_owned;
 unsigned char short_data[12]={8,0,0,0,9,0,0,0,0x5b};RxRoomScripts truncated;check(!rx_room_scripts_open(short_data,11,&truncated),"truncated initialization fixture");rom->evtp=&truncated;CAM_WORK before_short=cam;check(rx_events_start_room()!=NULL&&!memcmp(&before_short,&cam,sizeof(cam)),"truncated camera initialization executed");
 puts("PASS atomic foreign-bank/area/count/matrix/light/scalar, invalid floor/cut/extent, nonfinite, truncated and unported camera-mode rejection");
 /* Execute the actual disc instruction and present its resulting original shot. */
 cam=saved;*plp=actor;opening.player_work=scalar;error=run(original->data[2239]);check(!error,error);
 RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"camera initialization GLES");sys->sp_flg&=~0x80u;
 error=rx_opening_present(&opening);check(!error,error);static unsigned char image[640*480*4];glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,image);check(!glGetError(),"camera initialization GPU readback");
 unsigned visible=0;for(unsigned i=0;i<sizeof(image);i+=4)visible+=image[i]||image[i+1]||image[i+2];check(visible>100,"return-to-room camera rendered no geometry");
 printf("PASS actual InitCamSet bytes and room-camera GLES presentation (%u nonblack pixels)\n",visible);
 rx_opening_close_graphics(&opening);rx_egl_close(&egl);
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
