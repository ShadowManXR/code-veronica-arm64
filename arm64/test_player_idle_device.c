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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL normal player entry: %s\n",why);exit(1);}}
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




static unsigned cases;
static void preview(RxOpening *o,int reject){BH_PWORK a=*plp;EXP_WORK w=o->player_work;unsigned head=o->pose_flags,st=sys->st_flg;NJS_CNK_OBJECT nodes[64];O_WORK work[64];unsigned n=plp->mlwP->obj_num;memcpy(nodes,plp->mlwP->objP,n*sizeof(*nodes));memcpy(work,plp->mlwP->owP,n*sizeof(*work));VuState vu=g_vu0;const char *e=rx_player_control_script_check();if(!reject&&e)fprintf(stderr,"normal preflight: %s\n",e);check(reject?(e!=NULL):(e==NULL),"normal controller acceptance");check(!memcmp(&a,plp,sizeof(a))&&!memcmp(&w,&o->player_work,sizeof(w))&&head==o->pose_flags&&st==sys->st_flg&&!memcmp(nodes,plp->mlwP->objP,n*sizeof(*nodes))&&!memcmp(work,plp->mlwP->owP,n*sizeof(*work))&&!memcmp(&vu,&g_vu0,sizeof(vu)),"normal controller preview modified owned state");}
static void step(RxOpening *o){const char *audio=rx_event_se_tick();check(!audio,audio);preview(o,0);const char *e=rx_player_control_script_tick();check(!e,e);Matrix root;identity(root);transform(root,plp->px,plp->py,plp->pz,0,plp->ay,0);matrix((float*)plp->mtx,root);hierarchy(plp->mlwP,plp->mlwP->objP,root);++cases;}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);check(o.commands_ready&&!o.events_ready,"complete opening stays gated");BH_PWORK startup=*plp;EXP_WORK scalar=o.player_work;unsigned pose=o.pose_flags,flags=sys->sp_flg,cb=sys->cb_flg;unsigned char position=sys->pos_no;NJS_CNK_OBJECT nodes[64];O_WORK matrices[64];unsigned n=plp->mlwP->obj_num;check(n<=64,"player skeleton fixture extent");memcpy(nodes,plp->mlwP->objP,n*sizeof(*nodes));memcpy(matrices,plp->mlwP->owP,n*sizeof(*matrices));
 unsigned placements=0;for(int i=0;i<rom->pos_n;++i){sys->cb_flg&=~0x800000u;sys->pos_no=i;check(!rx_player_control_place_room(),"owned original room placement");const POS *p=rom->posp+i;check(plp->px==p->px&&plp->pz==p->pz&&plp->ay==p->ay&&plp->py==rom->grand[plp->flr_no+2],"authored room spawn differs");check(o.player_work.spx==p->px&&o.player_work.plx==p->px&&o.player_work.spy==p->py&&o.player_work.ply==p->py&&o.player_work.spz==p->pz&&o.player_work.plz==p->pz&&o.player_work.bpx==p->px&&o.player_work.bpxb==p->px&&o.player_work.bpy==p->py+12.5f&&o.player_work.bpyb==p->py+12.5f&&o.player_work.bpz==p->pz&&o.player_work.bpzb==p->pz&&!o.player_work.arn&&!o.player_work.arp,"original collision/safe-position history was not initialized");++placements;}
 NJS_POINT3 old_pos=sys->ply_pos;int old_angle=sys->ply_ang;char old_floor=sys->flr_no;unsigned old_status=sys->ply_stflg[0];char old_weapon=sys->ply_wno[0];sys->cb_flg|=0x800000;sys->ply_pos=(NJS_POINT3){10.25f,-3.5f,42.125f};sys->ply_ang=57123;sys->flr_no=-1;sys->ply_stflg[0]=0x200000;sys->ply_wno[0]=3;check(!rx_player_control_place_room(),"saved-position source branch");check(!(sys->cb_flg&0x800000)&&plp->px==10.25f&&plp->py==-3.5f&&plp->pz==42.125f&&plp->ay==57123&&plp->flr_no==-1&&plp->stflg==(0x200000|0x40000000)&&plp->wpnr_no==3&&o.player_work.bpy==9&&o.player_work.spx==10.25f,"saved placement/history fields differ");sys->ply_pos=old_pos;sys->ply_ang=old_angle;sys->flr_no=old_floor;sys->ply_stflg[0]=old_status;sys->ply_wno[0]=old_weapon;sys->pos_no=position;sys->cb_flg=cb;
 printf("PASS %u authored spawn records and original saved-position branch, exact collision/safe-position history and original flag consumption\n",placements);
 for(unsigned bad=0;bad<4;++bad){*plp=startup;o.player_work=scalar;POS *owned=rom->posp;unsigned char selector=sys->pos_no;float floor=rom->grand[0];unsigned char character=sys->ply_id;if(bad==0)sys->pos_no=-1;if(bad==1)rom->posp=(void*)(uintptr_t)1;if(bad==2)rom->grand[0]=NAN;if(bad==3)sys->ply_id=4;BH_PWORK a=*plp;EXP_WORK w=o.player_work;check(rx_player_control_place_room()!=NULL&&!memcmp(&a,plp,sizeof(a))&&!memcmp(&w,&o.player_work,sizeof(w)),"invalid spawn changed player prefix");rom->posp=owned;sys->pos_no=selector;rom->grand[0]=floor;sys->ply_id=character;}
 puts("PASS four malformed original room placements reject before changing player or scalar work");
 *plp=startup;o.player_work=scalar;o.pose_flags=pose;memcpy(plp->mlwP->objP,nodes,n*sizeof(*nodes));memcpy(plp->mlwP->owP,matrices,n*sizeof(*matrices));sys->sp_flg|=1;sys->pad_on=sys->pad_old=sys->pad_ps=0;plp->ct3=0;
 for(unsigned i=0;i<300;++i){step(&o);check(plp->mode0==1&&!plp->mode1&&!plp->mode2&&plp->mode3==1&&plp->ct3==(int)i+1&&plp->mtn_no==PlMtnAct[0][0][0],"original idle state/timer differs");}preview(&o,0);step(&o);check(plp->mode2==13&&plp->mode3==0&&plp->ct3==301&&(plp->flg&0x10000),"original 301st idle update did not request gesture");puts("PASS original normal idle entry/300-frame clock, blend settling and original 301st-update gesture request");
 const int hp[]={160,119,30,29,1};unsigned turns=0;for(unsigned h=0;h<5;++h)for(unsigned heading=0;heading<8;++heading)for(unsigned side=0;side<2;++side){*plp=startup;o.player_work=scalar;o.pose_flags=pose;memcpy(plp->mlwP->objP,nodes,n*sizeof(*nodes));memcpy(plp->mlwP->owP,matrices,n*sizeof(*matrices));plp->hp=hp[h];plp->ay=heading*8192;plp->ct3=0;sys->pad_on=side?4:8;sys->pad_ps=0;sys->pad_old=0;
  for(unsigned t=0;t<20;++t){float rate=o.player_work.dmlvl==2?.8f:1;int delta=(int)(182.04445f*(7.2f*rate)),angle=plp->ay+(side?delta:-delta);step(&o);check(plp->mode0==1&&!plp->mode1&&plp->mode2==(side?2:1)&&plp->ay==angle&&(int)plp->mtn_add==(side?65536:-65536),"original turn direction/speed/health state differs");sys->pad_old=sys->pad_on;++turns;}}
 printf("PASS %u original normal turn updates: both directions/eight headings/five health boundaries, source clip/blend clocks and %u independent world matrix channels (max error %.9g)\n",turns,channels,maximum);
 *plp=startup;o.player_work=scalar;o.pose_flags=pose;sys->pad_on=2;preview(&o,1);sys->pad_on=0;sys->sp_flg&=~1u;BH_PWORK before=*plp;check(!rx_player_control_script_tick()&&!memcmp(&before,plp,sizeof(before)),"paused normal controller changed player");puts("PASS unported normal reverse movement rejects before mutation and original pause gate is preserved");
 sys->sp_flg=flags;sys->cb_flg=cb;sys->pad_on=sys->pad_old=sys->pad_ps=0;rx_opening_end(&o);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
