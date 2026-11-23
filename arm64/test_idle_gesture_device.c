#include "opening.h"
#include "face_pose.h"
#include "player_control_runtime.h"
#include "pl_evt.h"
#include "event_audio.h"
#include "njplus.h"
#include "ps2_NaMath.h"
#include "ps2/vu0.h"
#include "android_egl.h"
#include "camera.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "room.h"
#include "main.h"
#include "pwksub.h"
#include "MdlPut.h"
#include "ps2_NaMatrix.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
extern BH_SCEWORK bhEtask[16];extern int lNaMatMatrixStuckMax,lNaMatMatrixStuckCnt;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL player idle gesture: %s\n",why);exit(1);}}
typedef double Matrix[16];
static void identity(Matrix m){memset(m,0,sizeof(Matrix));m[0]=m[5]=m[10]=m[15]=1;}
static float sine(int angle){unsigned n=(unsigned)angle&65535,index=n&16383;if(n&16384)index=16383-index;float s=sinf(0.0000958738f*index);return n&32768?-s:s;}
static void product(Matrix a,const Matrix b){Matrix r;for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row){double v=0;for(unsigned k=0;k<4;++k)v+=a[k*4+row]*b[c*4+k];r[c*4+row]=v;}memcpy(a,r,sizeof(r));}
static void rotation(Matrix m,unsigned axis,int angle){Matrix r;identity(r);unsigned p=(axis+1)%3,q=(axis+2)%3;double s=sine(angle),c=sine(angle+16384);r[p*4+p]=r[q*4+q]=c;r[p*4+q]=s;r[q*4+p]=-s;product(m,r);}
static void transform(Matrix m,float x,float y,float z,int ax,int ay,int az){Matrix t;identity(t);t[12]=x;t[13]=y;t[14]=z;product(m,t);rotation(m,2,az);rotation(m,1,ay);rotation(m,0,ax);}
static unsigned channels;static double maximum;
static void matrix(const float *got,const Matrix expected){for(unsigned i=0;i<16;++i){double d=fabs(got[i]-expected[i]);if(d>maximum)maximum=d;check(isfinite(got[i])&&d<0.001,"native world/link matrix differs from independent hierarchy");++channels;}}
/* Original facial flat entries retain links into the private source tree.
   Traverse that tree for parent relationships, using preorder to select the
   separately animated flat entries. */
static void hierarchy_nodes(ML_WORK *model,const NJS_CNK_OBJECT *node,const Matrix parent,unsigned *next){while(node){unsigned index=(*next)++;check(index<model->obj_num,"reference node extent");const NJS_CNK_OBJECT *flat=model->objP+index;Matrix current;memcpy(current,parent,sizeof(current));transform(current,flat->pos[0],flat->pos[1],flat->pos[2],flat->ang[0],flat->ang[1],flat->ang[2]);check(!(model->owP[index].flg&1),"fixture has an unhandled matrix override");matrix(model->owP[index].mtx,current);if(node->child)hierarchy_nodes(model,node->child,current,next);node=node->sibling;}}
static void hierarchy(ML_WORK *model,const NJS_CNK_OBJECT *node,const Matrix parent){unsigned next=0;hierarchy_nodes(model,node,parent,&next);check(next==model->obj_num,"reference facial tree count");}

static void preview(RxOpening *o,int reject){
 BH_PWORK before=*plp;EXP_WORK scalar=o->player_work;unsigned pose=o->pose_flags,st=sys->st_flg,on=sys->pad_on,ps=sys->pad_ps,old=sys->pad_old;VuState vu=g_vu0;
 NJS_CNK_OBJECT nodes[64];O_WORK work[64];unsigned n=plp->mlwP->obj_num;memcpy(nodes,plp->mlwP->objP,n*sizeof(*nodes));memcpy(work,plp->mlwP->owP,n*sizeof(*work));
 const char *e=rx_player_control_script_check();if(!reject&&e)fprintf(stderr,"preflight: %s\n",e);check(reject?e!=NULL:e==NULL,"gesture validation acceptance");
 check(!memcmp(&before,plp,sizeof(before))&&!memcmp(&scalar,&o->player_work,sizeof(scalar))&&pose==o->pose_flags&&st==sys->st_flg&&on==sys->pad_on&&ps==sys->pad_ps&&old==sys->pad_old&&
       !memcmp(&vu,&g_vu0,sizeof(vu))&&!memcmp(nodes,plp->mlwP->objP,n*sizeof(*nodes))&&!memcmp(work,plp->mlwP->owP,n*sizeof(*work)),"gesture preview changed player/scalar/flags/input/VU/pose");
}
static unsigned samples,key_samples;static int changed;
static void step(RxOpening *o){
 const char *e=rx_event_se_tick();check(!e,e);preview(o,0);NJS_CNK_OBJECT before[64];unsigned n=plp->mlwP->obj_num;memcpy(before,plp->mlwP->objP,n*sizeof(*before));
 NJS_CNK_OBJECT expected[64];memcpy(expected,before,n*sizeof(*expected));
 int raw=plp->mode2==13&&plp->mode3>0&&(plp->flg&0x10000)&&!plp->hokan_count;
 unsigned health_level=plp->hp>=120?((plp->stflg&0x280000)!=0):plp->hp>=30?1:2;
 if(o->player_work.dmlvl!=(int)health_level)raw=0; /* Source changes clip/frame before sampling. */
 const MN_WORK *clip=plp->mnwP+plp->mtn_no;unsigned frame=(unsigned)plp->frm_no>>16;
 if(plp->mode3==1&&frame>=clip->frm_num-1)raw=0;
 if(raw){memcpy(expected[0].pos,(float*)clip->md2P[0].p[0]+frame*3,12);
  for(unsigned j=0;j<clip->obj_num;++j)if(!(plp->mlwP->owP[j].flg&2)){
   const unsigned short *angles=clip->md2P[j].p[1];for(unsigned k=0;k<3;++k)expected[j].ang[k]=angles?angles[frame*3+k]:0;
  }
 }
 e=rx_player_control_script_tick();check(!e,e);
 if(raw){check(!memcmp(expected,plp->mlwP->objP,n*sizeof(*expected)),"settled gesture pose differs from independent original frame keys");++key_samples;}
 Matrix root;identity(root);transform(root,plp->px,plp->py,plp->pz,0,plp->ay,0);matrix((float*)plp->mtx,root);hierarchy(plp->mlwP,plp->mlwP->objP,root);
 if(memcmp(before,plp->mlwP->objP,n*sizeof(*before)))changed=1;
 check(rx_engine_player_world_pose(o->player),"gesture did not publish world-relative skin");++samples;
}
static BH_PWORK saved;static EXP_WORK scalar;static unsigned pose;static NJS_CNK_OBJECT nodes[64];static O_WORK work[64];
static void reset(RxOpening *o){*plp=saved;o->player_work=scalar;o->pose_flags=pose;memcpy(plp->mlwP->objP,nodes,plp->mlwP->obj_num*sizeof(*nodes));memcpy(plp->mlwP->owP,work,plp->mlwP->obj_num*sizeof(*work));sys->pad_on=sys->pad_ps=sys->pad_old=0;}
static float clamped(float f){unsigned bits;memcpy(&bits,&f,4);unsigned exponent=bits&0x7f800000;
 if(!exponent)bits&=0x80000000;else if(exponent==0x7f800000)bits=(bits&0x80000000)|0x7f7fffff;memcpy(&f,&bits,4);return f;}
static void vectors(void){
 unsigned count=0;for(unsigned seed=0;seed<2048;++seed)for(unsigned alias=0;alias<4;++alias){
  float data[10];for(unsigned k=0;k<10;++k)data[k]=((int)((seed*3331+k*7919)&65535)-32768)*.03125f;
  if(seed==2046){data[2]=-3.402823466e38f;data[5]=3.402823466e38f;}if(seed==2047){data[2]=1.17549435e-38f;data[5]=1.17549421e-38f;}
  unsigned dst=2,src=alias==0?5:alias==1?2:alias==2?3:1;float expected[10];memcpy(expected,data,sizeof(data));
  for(unsigned k=0;k<3;++k)expected[dst+k]=clamped(data[dst+k]-data[src+k]);VuState vu=g_vu0;
  check((uintptr_t)(data+dst)>UINT32_MAX&&(uintptr_t)(data+src)>UINT32_MAX,"vector fixture must use native addresses above 4 GiB");
  njSubVector((NJS_VECTOR*)(data+dst),(NJS_VECTOR*)(data+src));check(!memcmp(expected,data,sizeof(data))&&!memcmp(&vu,&g_vu0,sizeof(vu)),"native vector subtraction differs from EE load-before-store/clamp semantics or corrupts neighbors/VU");++count;
 }
 printf("PASS %u native high-address vector subtractions, same/partially overlapping inputs, neighbor preservation, overflow/denormal clamp and unchanged VU state\n",count);
}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");vectors();RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);
 saved=*plp;scalar=o.player_work;pose=o.pose_flags;unsigned flags=sys->sp_flg,st=sys->st_flg;unsigned n=plp->mlwP->obj_num;check(n<=64,"gesture skeleton capacity");memcpy(nodes,plp->mlwP->objP,n*sizeof(*nodes));memcpy(work,plp->mlwP->owP,n*sizeof(*work));sys->sp_flg|=1;
 unsigned clips[2]={PlMtnAct[0][0][6],PlMtnAct[0][0][6]+1};MN_WORK *bank=plp->mnwP;
 printf("Original idle gesture clips %u/%u, lengths %u/%u\n",clips[0],clips[1],bank[clips[0]].frm_num,bank[clips[1]].frm_num);
 for(unsigned heading=0;heading<8;++heading){reset(&o);plp->ay=heading*8192;plp->ct3=0;
  for(unsigned t=0;t<301;++t)step(&o);
  check(plp->mode2==13&&!plp->mode3&&plp->ct3==301&&(plp->flg&0x10000),"original idle threshold request");
  unsigned length=bank[clips[0]].frm_num,loops=bank[clips[1]].frm_num;check(length>1&&loops>1,"idle clips unexpectedly empty");
  for(unsigned t=0;t<length+loops*2;++t){unsigned prior=plp->mode3;int prior_frame=plp->frm_no;step(&o);
   if(!prior)check(plp->mtn_no==clips[0]&&plp->mode3==1&&plp->frm_no==65536&&(plp->flg&0xd0000)==0xd0000&&(o.pose_flags&4),"gesture entry flags/clip/blend clock");
   else if(prior==1&&(unsigned)prior_frame/65536>=length-1)check(plp->mtn_no==clips[1]&&plp->mode3==2&&plp->frm_no==65536,"original last-entry-frame transition");
   else check(plp->mode3==prior,"gesture state advanced at wrong frame");
   check(plp->ct3==301&&(plp->flg&0x10000),"gesture unexpectedly reset idle count/unlocked controls");
  }
  check(plp->mode3==2&&plp->mtn_no==clips[1],"gesture never reached its loop");
  sys->pad_on=4;sys->pad_ps=4;sys->pad_old=0;preview(&o,0);step(&o);
  check(!(plp->flg&0x10000)&&!(sys->pad_on&15)&&sys->pad_ps==4,"original interruption did not clear directional input/unlock");
  sys->pad_ps=0;step(&o);check(!plp->mode2&&plp->mode3==1&&plp->ct3==1&&plp->mtn_no==PlMtnAct[0][0][0],"interruption did not return to original standing reset");
 }
 check(changed,"gesture pose never changed");
 printf("PASS %u original idle/entry/loop/interruption samples across eight headings; %u settled poses from independent disc keys, %u independent world-matrix channels (maximum %.9g), world skin publication and immutable validation including input/VU\n",samples,key_samples,channels,maximum);
 /* Explicit original states exercise fractional clocks and both directions.
    Input interruption must be safe at each of the three controller states. */
 for(unsigned mode=0;mode<3;++mode)for(unsigned direction=0;direction<2;++direction){reset(&o);plp->mode2=13;plp->mode3=mode;plp->flg|=0x10000;plp->mtn_no=clips[mode==2];plp->frm_no=32768;plp->mtn_add=direction?-32768:32768;
  step(&o);sys->pad_on=8;preview(&o,0);step(&o);check(!(plp->flg&0x10000)&&!(sys->pad_on&15),"state-specific gesture interruption");}
 puts("PASS all three original gesture states with fractional/reverse clocks and directional interruption");
 for(unsigned mode=0;mode<3;++mode)for(unsigned pad=1;pad<16;++pad){reset(&o);plp->mode2=13;plp->mode3=mode;plp->flg|=0x10000;plp->mtn_no=clips[mode==2];plp->frm_no=32768;sys->pad_on=pad;sys->pad_ps=pad;
  step(&o);check(!(plp->flg&0x10000)&&!(sys->pad_on&15)&&sys->pad_ps==pad,"original directional cancellation/input preservation");
 }
 puts("PASS all 15 directional masks interrupt all three locked gesture states with original input clearing");
 const int health[]={160,119,30,29,1};
 for(unsigned h=0;h<5;++h){reset(&o);plp->hp=health[h];plp->mode2=13;plp->mode3=2;plp->flg|=0x10000;plp->mtn_no=clips[1];plp->frm_no=3*65536;plp->mtn_add=65536;plp->hokan_count=0;
  unsigned level=health[h]>=120?0:health[h]>=30?1:2;step(&o);check(o.player_work.dmlvl==(int)level,"original gesture health-level transition");
  check(((plp->flg&0x10000)!=0)==(level==0),"original health change did not interrupt gesture");
  if(level){check(plp->mtn_no==clips[0]&&plp->frm_no==(int)bank[clips[0]].frm_num+65536,"original health clip/frame remapping differs");step(&o);check(!plp->mode2&&plp->mtn_no==PlMtnAct[0][level][0],"health interruption did not restore health-specific standing clip");}
 }
 puts("PASS five original health boundaries, locked/unlocked continuation, exact source frame remapping and health-specific standing return");
 for(unsigned bad=0;bad<9;++bad){reset(&o);plp->mode2=13;plp->mode3=0;plp->flg|=0x10000;MN_WORK before[2]={bank[clips[0]],bank[clips[1]]};unsigned *head=(unsigned*)plp->exp1;
  switch(bad){case 0:bank[clips[0]].frm_num=0;break;case 1:bank[clips[1]].md2P=(void*)(uintptr_t)1;break;case 2:plp->mode3=3;break;
   case 3:plp->exp1=(void*)(uintptr_t)1;break;case 4:plp->mnwP=(void*)(uintptr_t)1;break;case 5:plp->ct3=INT_MAX;break;case 6:plp->mode1=1;break;case 7:plp->wpnr_no=3;break;case 8:sys->pad_on=0x10;break;}
  preview(&o,1);bank[clips[0]]=before[0];bank[clips[1]]=before[1];plp->exp1=(void*)head;
 }
 reset(&o);plp->mode2=13;plp->mode3=1;plp->flg|=0x10000;sys->sp_flg&=~1u;BH_PWORK before=*plp;check(!rx_player_control_script_tick()&&!memcmp(&before,plp,sizeof(before)),"task pause advanced gesture");
 puts("PASS nine malformed gesture/clip/bank/flags/input/weapon contexts reject before any state change; original task pause stays a no-op");
 sys->sp_flg=flags;sys->st_flg=st;reset(&o);check(!o.events_ready,"full opening unexpectedly enabled");rx_opening_end(&o);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}




