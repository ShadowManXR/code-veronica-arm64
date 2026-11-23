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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL normal running: %s\n",why);exit(1);}}
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

static BH_PWORK saved;static EXP_WORK scalar;static unsigned pose,nodes_n;static NJS_CNK_OBJECT nodes[64];static O_WORK matrices[64];
static unsigned steps,key_poses,footsteps;
static void reset(RxOpening *o,unsigned level,unsigned group,unsigned heading,int wall){
 *plp=saved;o->player_work=scalar;o->pose_flags=pose;memcpy(plp->mlwP->objP,nodes,nodes_n*sizeof(*nodes));memcpy(plp->mlwP->owP,matrices,nodes_n*sizeof(*matrices));
 plp->mode0=1;plp->mode1=plp->mode2=plp->mode3=0;plp->ct3=0;plp->stflg=0;plp->flg&=~0xC1000u;
 if(wall)plp->flg|=0x10;else plp->flg&=~0x10u;plp->hp=(int[]){160,80,20}[level];o->player_work.dmlvl=level;o->player_work.wpntp=group;plp->ay=heading;
 sys->pad_on=sys->pad_old=sys->pad_ps=0;
}
static unsigned u32(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static float *speed_block(unsigned level){unsigned char *p=sys->plzmtp;for(unsigned i=0;i<level;++i)p+=4+u32(p);check(u32(p)%4==0,"independent speed block stride");return (float*)(p+4);}
static void preview(RxOpening *o,int reject){
 BH_PWORK before=*plp;EXP_WORK w=o->player_work;unsigned head=o->pose_flags,st=sys->st_flg,on=sys->pad_on,old=sys->pad_old,ps=sys->pad_ps;VuState vu=g_vu0;
 NJS_CNK_OBJECT ns[64];O_WORK ws[64];memcpy(ns,saved.mlwP->objP,nodes_n*sizeof(*ns));memcpy(ws,saved.mlwP->owP,nodes_n*sizeof(*ws));
 const char *e=rx_player_control_script_check();if(!reject&&e)fprintf(stderr,"run preview: %s\n",e);check(reject?e!=NULL:e==NULL,"walking preview acceptance");
 check(!memcmp(&before,plp,sizeof(before))&&!memcmp(&w,&o->player_work,sizeof(w))&&head==o->pose_flags&&st==sys->st_flg&&on==sys->pad_on&&old==sys->pad_old&&ps==sys->pad_ps&&
 !memcmp(&vu,&g_vu0,sizeof(vu))&&!memcmp(ns,saved.mlwP->objP,nodes_n*sizeof(*ns))&&!memcmp(ws,saved.mlwP->owP,nodes_n*sizeof(*ws)),"walking preflight changed player/scalar/input/VU/pose");
}
static void step(RxOpening *o){const char *e=rx_event_se_tick();check(!e,e);preview(o,0);e=rx_player_control_script_tick();if(e)fprintf(stderr,"run tick: %s\n",e);check(!e,e);
 Matrix root;identity(root);transform(root,plp->px,plp->py,plp->pz,0,plp->ay,0);matrix((float*)plp->mtx,root);hierarchy(plp->mlwP,plp->mlwP->objP,root);check(rx_engine_player_world_pose(o->player),"walking skin did not use original world joints");++steps;}
static int remap(unsigned old_frames,unsigned new_frames,int fixed){
 float percent=100.f/(float)old_frames;float scaled=(float)new_frames*(percent*(fixed/65536));int value=(int)(scaled/100.f)*65536;
 if(value<0)value=0;if((unsigned)value>new_frames)value=new_frames;return value;
}
static void input(unsigned pad){sys->pad_old=sys->pad_on;sys->pad_ps=pad&~sys->pad_on;sys->pad_on=pad;}
static void request_run(RxOpening *o,unsigned group,unsigned level,unsigned pad){input(pad);step(o);
 check(plp->mode2==6&&!plp->mode3&&(plp->flg&0x10000)&&plp->mtn_no==PlMtnAct[group][level][2]&&plp->frm_no==6*65536,"source walking-to-running request/clock differs");}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};
 const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);
 saved=*plp;scalar=o.player_work;pose=o.pose_flags;nodes_n=plp->mlwP->obj_num;check(nodes_n<=64,"running skeleton capacity");memcpy(nodes,plp->mlwP->objP,nodes_n*sizeof(*nodes));memcpy(matrices,plp->mlwP->owP,nodes_n*sizeof(*matrices));unsigned tasks=sys->sp_flg;sys->sp_flg|=1;
 MN_WORK *bank=plp->mnwP;
 for(unsigned group=0;group<2;++group)for(unsigned level=0;level<3;++level)for(unsigned heading=0;heading<8;++heading)for(unsigned dir=0;dir<3;++dir){reset(&o,level,group,heading*8192,0);unsigned pad=0x400|(unsigned[]){1,9,5}[dir];request_run(&o,group,level,pad);
  for(unsigned t=0;t<96;++t){input(pad);unsigned slot=PlMtnAct[group][level][3];unsigned period=bank[slot].frm_num;
   int fixed=plp->mode3?plp->frm_no:remap(bank[plp->mtn_no].frm_num,period,plp->frm_no);unsigned frame=(unsigned)fixed>>16;
   float speed=speed_block(3+level)[frame+1];int angle=plp->ay,delta=(int)(182.04445f*(4.5f*(level==2?.8f:1.f)));if(dir==1)angle-=delta;if(dir==2)angle+=delta;
   float x=plp->px-speed*sine(angle),z=plp->pz-speed*sine(angle+16384);unsigned foot=frame==PlFootSnd[0][0][1]||frame==PlFootSnd[0][1][1];
   NJS_CNK_OBJECT expected[64];memcpy(expected,plp->mlwP->objP,nodes_n*sizeof(*expected));int raw=plp->mode3==1&&!plp->hokan_count;
   if(raw){memcpy(expected[0].pos,(float*)bank[slot].md2P[0].p[0]+frame*3,12);for(unsigned j=0;j<bank[slot].obj_num;++j)if(!(plp->mlwP->owP[j].flg&2)){
    const unsigned short *keys=bank[slot].md2P[j].p[1];for(unsigned k=0;k<3;++k)expected[j].ang[k]=keys?keys[frame*3+k]:0;
   }}
   step(&o);check(plp->mode2==6&&plp->mode3==1&&(plp->flg&0x10000)&&plp->mtn_no==slot,"original run state/lock/clip");
   int next=fixed+65536;if(next>=(int)period*65536)next-=period*65536;check(plp->frm_no==next,"original run entry/remap/wrap clock");
   check(plp->ay==angle&&plp->spd==speed&&fabsf(plp->px-x)<.00001f&&fabsf(plp->pz-z)<.00001f,"run displacement/turn differs from independent authored-speed reference");
   check(((plp->stflg&0x200)!=0)==(foot!=0),"original running footstep timing");footsteps+=foot;
   if(raw){check(!memcmp(expected,plp->mlwP->objP,nodes_n*sizeof(*expected)),"settled running pose differs from independent disc keys");++key_poses;}
  }
  float x=plp->px,z=plp->pz;input(1);step(&o);check(plp->mode2==3&&!plp->mode3&&!(plp->flg&0x10000)&&(plp->flg&0x800000)&&plp->px==x&&plp->pz==z,"run release must queue original walk handoff before displacement");
  unsigned slot=PlMtnAct[group][level][2];int fixed=remap(bank[plp->mtn_no].frm_num,bank[slot].frm_num,plp->frm_no);float speed=speed_block(level)[((unsigned)fixed>>16)+1];
  x=plp->px-speed*sine(plp->ay);z=plp->pz-speed*sine(plp->ay+16384);input(1);step(&o);
  check(plp->mode2==3&&plp->mode3==1&&plp->mtn_no==slot&&!(plp->flg&0x800000)&&plp->frm_no==fixed+65536&&fabsf(plp->px-x)<.00001f&&fabsf(plp->pz-z)<.00001f,"original run-to-walk remap/speed/pose handoff");
 }
 printf("PASS %u original run/walk-handoff updates, two groups/three health levels/eight headings/three directions; %u settled disc-key poses, %u source-timed run footstep ticks and %u independent matrix channels (max %.9g)\n",steps,key_poses,footsteps,channels,maximum);
 for(unsigned pad=0;pad<16;++pad){reset(&o,0,0,0,0);request_run(&o,0,0,0x401);step(&o);float x=plp->px,z=plp->pz;int angle=plp->ay;input(pad|0x400);step(&o);
  if(!(pad&1))check(!plp->mode2&&!plp->mode3&&!(plp->flg&0x10000)&&plp->px==x&&plp->pz==z&&plp->ay==angle,"release-forward stop must not turn or displace");
  else {int delta=(int)(182.04445f*4.5f);int expected_angle=angle+((pad&4)?delta:0)-((pad&8)?delta:0);
   check(plp->mode2==6&&(plp->flg&0x10000)&&plp->ay==expected_angle,"locked original run directional/turn mask differs");
  }
 }
 puts("PASS all 16 locked-run directional masks and source release-forward stop, including simultaneous turns");
 const int health[]={160,119,30,29,1};for(unsigned old=0;old<3;++old)for(unsigned h=0;h<5;++h){reset(&o,old,0,0,0);plp->hp=health[h];plp->mode2=6;plp->mode3=1;plp->flg|=0x10000;plp->mtn_no=PlMtnAct[0][old][3];plp->frm_no=3*65536;plp->mtn_add=65536;plp->hokan_count=0;
  input(0x401);float speed=speed_block(old+3)[4],z=plp->pz-speed*sine(plp->ay+16384);unsigned level=health[h]>=120?0:health[h]>=30?1:2;
  unsigned new_slot=PlMtnAct[0][level][3];int fixed=old==level?3*65536:remap(bank[plp->mtn_no].frm_num,bank[new_slot].frm_num,plp->frm_no);int next=fixed+65536;if(next>=(int)bank[new_slot].frm_num*65536)next-=bank[new_slot].frm_num*65536;step(&o);
  check(o.player_work.dmlvl==(int)level&&plp->mtn_no==PlMtnAct[0][level][3]&&fabsf(plp->pz-z)<.00001f,"original run health switch did not retain incoming speed and select correct new clip");
  check(plp->frm_no==next&&plp->hokan_count==(old==level?0:2),"original run health transition frame/blend mapping differs");
 }
 puts("PASS fifteen original running health transitions across all cached levels/five HP boundaries");
 ATR_WORK *walls=malloc((size_t)rom->wal_n*sizeof(*walls));check(walls&&rom->wal_n>0,"owned running wall fixture");memcpy(walls,rom->walp,(size_t)rom->wal_n*sizeof(*walls));for(int i=0;i<rom->wal_n;++i)rom->walp[i].flg=0;
 rom->walp[0]=(ATR_WORK){.flg=1,.type=0,.px=-20,.py=-20,.pz=0,.w=40,.h=100,.d=1};reset(&o,0,0,0,1);plp->px=0;plp->py=0;plp->pz=20;plp->pxb=0;plp->pzb=20;o.player_work.bpx=o.player_work.bpxb=0;o.player_work.bpy=o.player_work.bpyb=12.5;o.player_work.bpz=o.player_work.bpzb=20;o.player_work.spx=0;o.player_work.spz=20;
 request_run(&o,0,0,0x401);for(unsigned t=0;t<120;++t){input(0x401);step(&o);check(plp->pz>=1+plp->ar-.001&&o.player_work.bpz>=1+plp->ar-.001,"running crossed independent actor/body wall clearance");}
 check(plp->pz<10,"running did not approach wall");float wall_z=plp->pz;for(unsigned t=0;t<10;++t)step(&o);check(fabsf(plp->pz-wall_z)<.001,"running wall contact did not settle");memcpy(rom->walp,walls,(size_t)rom->wal_n*sizeof(*walls));free(walls);
 puts("PASS original running wall collision: independent actor/body clearance and stable sustained contact");
 for(unsigned bad=0;bad<14;++bad){reset(&o,0,0,0,0);plp->mode2=6;plp->mode3=1;plp->flg|=0x10000;unsigned slot=PlMtnAct[0][0][3];plp->mtn_no=slot;plp->frm_no=3*65536;plp->mtn_add=65536;input(0x401);
  unsigned char *translations=sys->plzmtp;unsigned first=u32(translations);float *speed=speed_block(3);unsigned header=u32((unsigned char*)speed-4);float value=speed[4];char selector=PlyTrsZ[3][0];MN_WORK old=bank[slot];unsigned health_slot=PlMtnAct[0][1][3];MN_WORK health_motion=bank[health_slot];
  switch(bad){case 0:sys->plzmtp=(void*)(uintptr_t)1;break;case 1:memset(translations,0,4);break;case 2:speed[4]=NAN;break;case 3:PlyTrsZ[3][0]=-1;break;case 4:bank[slot].md2P=(void*)(uintptr_t)1;break;
   case 5:plp->flg|=0x4000000;break;case 6:plp->stflg|=0x100000;break;case 7:plp->flg2|=8;break;case 8:plp->mode3=2;break;case 9:plp->ar=NAN;break;case 10:plp->frm_no=bank[slot].frm_num*65536;break;
   case 11:sys->pad_on=0;memset((unsigned char*)speed-4,0xff,4);break;case 12:plp->hp=119;bank[health_slot].datP=NULL;break;case 13:speed[4]=1e30f;break;}
  preview(&o,1);sys->plzmtp=translations;memcpy(translations,&first,4);memcpy((unsigned char*)speed-4,&header,4);speed[4]=value;PlyTrsZ[3][0]=selector;bank[slot]=old;bank[health_slot]=health_motion;
 }
 reset(&o,0,0,0,0);plp->mode2=6;plp->mode3=1;plp->flg|=0x10000;sys->sp_flg&=~1u;BH_PWORK before=*plp;check(!rx_player_control_script_tick()&&!memcmp(&before,plp,sizeof(before)),"paused running advanced state");
 puts("PASS fourteen malformed run bank/header/speed/selector/frame/bounds/health contexts and unsupported quick-turn/water states reject atomically, including speed validation before stop; task pause stays a no-op");
 sys->sp_flg=tasks;reset(&o,0,0,0,1);check(!o.events_ready,"full opening unexpectedly enabled");rx_opening_end(&o);check(!sys->plzmtp,"locomotion translation owner survived teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
