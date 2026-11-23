#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "room.h"
#include "main.h"
#include "pwksub.h"
#include "ps2_NaMatrix.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
extern BH_SCEWORK bhEtask[16];extern int lNaMatMatrixStuckMax,lNaMatMatrixStuckCnt;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL held actor: %s\n",why);exit(1);}}
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

#include "subpl.h"
#include "face_control_runtime.h"
#include "face_sampling.h"
#include "face_pose.h"
extern void (*subpl_mv00_tbl[15])(BH_PWORK*);
static BH_PWORK saved[2];static NJS_CNK_OBJECT nodes[2][64];static O_WORK work[2][64];
static BH_PWORK *actor(unsigned i){return ene+rom->enep[i].wrk_no;}
static void restore(void){for(unsigned i=0;i<2;++i){*actor(i)=saved[i];memcpy(saved[i].mlwP->objP,nodes[i],31*sizeof(*nodes[i]));memcpy(saved[i].mlwP->owP,work[i],31*sizeof(*work[i]));}}
static unsigned char data[64];static RxRoomScripts script;
static void begin(unsigned record){
 memset(data,0,sizeof(data));data[0]=12;data[4]=18;data[8]=20;data[12]=20;data[20]=0x65;data[21]=1;data[22]=record;
 const unsigned char commands[]={0x69,24,0,1,1,0,0,6,0x2b,0,0x67,128,0xff};memcpy(data+24,commands,sizeof(commands));
 check(!rx_room_scripts_open(data,38,&script),"original hold setup task");rom->evtp=&script;const char *e=rx_events_start_room();check(!e,e);
 check(actor(record)->mode3==4&&actor(record)->ct2==0&&actor(record)->mnwP==sys->emtp[actor(record)->id],"original MotionPauseSet/SubCtr.128 setup");
 actor(record)->stflg=0;actor(1-record)->stflg=0x40000000;
}
static void step(void){const char *e=bhStepNativeRoomActors();check(!e,e);}
static void reject(void){BH_PWORK before[2]={*actor(0),*actor(1)};NJS_CNK_OBJECT n[2][31];O_WORK w[2][31];
 for(unsigned i=0;i<2;++i){memcpy(n[i],saved[i].mlwP->objP,sizeof(n[i]));memcpy(w[i],saved[i].mlwP->owP,sizeof(w[i]));}
 check(bhStepNativeRoomActors()!=NULL,"malformed held actor batch accepted");
 for(unsigned i=0;i<2;++i)check(!memcmp(before+i,actor(i),sizeof(*before))&&!memcmp(n[i],saved[i].mlwP->objP,sizeof(n[i]))&&!memcmp(w[i],saved[i].mlwP->owP,sizeof(w[i])),"malformed second actor advanced an earlier pose/controller");
}
static size_t bank_bytes(const unsigned char *p){size_t at=0;for(;;){unsigned n;memcpy(&n,p+at,4);at+=4;if(n==UINT_MAX)return at;check(n<1000000,"bank fixture extent");at+=n;}}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};
 const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);
 RxRoomScripts *original=rom->evtp;unsigned sp=sys->sp_flg;sys->sp_flg|=2;sys->loop_ct=1;sys->loop_now=0;
 for(unsigned i=0;i<2;++i){saved[i]=*actor(i);check(saved[i].mlwP->obj_num==31,"facial fixture");memcpy(nodes[i],saved[i].mlwP->objP,31*sizeof(*nodes[i]));memcpy(work[i],saved[i].mlwP->owP,31*sizeof(*work[i]));}
 unsigned samples=0;
 for(unsigned i=0;i<2;++i)for(unsigned held=3;held<=4;++held){restore();begin(i);BH_PWORK *a=actor(i);a->mskp=NULL;a->mode3=held;
  NJS_CNK_OBJECT pose[31];memcpy(pose,a->mlwP->objP,sizeof(pose));
  for(unsigned tick=0;tick<1024;++tick){a->px=3.5f;a->py=-2;a->pz=7;a->ax=tick*137;a->ay=tick*251;a->az=tick*17;
   /* These motion selectors are not consumed by the original held callback.
      They must never be dereferenced, sampled or advanced in this state. */
   a->mtn_no=tick&1?INT_MAX:1;a->frm_no=tick&2?INT_MIN:65536;a->mtn_tp=tick&4?(void *)(uintptr_t)1:NULL;a->mtn_add=INT_MAX;
   BH_PWORK expected=*a;expected.pxb=a->px;expected.pyb=a->py;expected.pzb=a->pz;expected.axb=a->ax;expected.ayb=a->ay;expected.azb=a->az;expected.flg=(expected.flg|0x8100)&~2u;
   step();Matrix root;identity(root);transform(root,a->px,a->py,a->pz,a->ax,a->ay,a->az);matrix((float *)a->mtx,root);hierarchy(a->mlwP,a->mlwP->objP,root);
   check(!memcmp(pose,a->mlwP->objP,sizeof(pose)),"held source callback resampled body motion");memcpy(expected.mtxbuf,a->mtxbuf,64);check(!memcmp(&expected,a,sizeof(expected)),"held callback changed unexpected clock/controller state");++samples;
  }
 }
 printf("PASS %u original held actor callbacks, both actors/modes3-4, original pause/handoff setup, immutable body pose/clock and %u independent matrix channels (max %.9g)\n",samples,channels,maximum);
 unsigned face_samples=0;
 for(unsigned i=0;i<2;++i)for(unsigned clip=0;clip<20;++clip){restore();begin(i);BH_PWORK *a=actor(i);bhInitMask(a);check(bhSetMask(a,clip,0),"held expression request");
  RxFaceClip view;check(!rx_face_clip(sys->mspp,bank_bytes((unsigned char*)sys->mspp),clip,&view),"expression reference");
  NJS_CNK_OBJECT before[31];memcpy(before,a->mlwP->objP,sizeof(before));int frame=a->frm_no;
  for(unsigned t=0;t<4;++t){PARAM_WORK expected;unsigned facial_frame=((int32_t *)a->exp0)[5];check(!rx_face_sample(&view,a->id,facial_frame,&expected),"independent held expression sample");step();
   MASK_WORK *mask=(MASK_WORK *)a->exp1;check(!memcmp(&expected,&mask->param,160),"held body suppressed/changed original face sampling");
   check(a->frm_no==frame,"facial callback advanced held body clock");for(unsigned node=0;node<31;++node)if(node<6||node>14)check(!memcmp(before+node,a->mlwP->objP+node,sizeof(*before)),"face update changed held body node");
   Matrix root;identity(root);transform(root,a->px,a->py,a->pz,a->ax,a->ay,a->az);hierarchy(a->mlwP,a->mlwP->objP,root);e=bhPublishNativeRoomActors();check(!e,e);++face_samples;
  }
 }
 printf("PASS %u live held-body/face/world/skin samples across all 20 expressions/both actors; original face continues while body clock stays fixed\n",face_samples);
 for(unsigned bad=0;bad<15;++bad){restore();begin(0);actor(1)->stflg=0;actor(1)->mode0=1;actor(1)->mode2=0;actor(1)->mode3=4;actor(1)->ct2=0;actor(1)->mnwP=sys->emtp[actor(1)->id];actor(0)->mskp=actor(1)->mskp=NULL;
  BH_PWORK *a=actor(1);MN_WORK *bank=sys->emtp[a->id];void (*callback)(BH_PWORK*)=subpl_mv00_tbl[0];
  switch(bad){case 0:a->mnwP=(void *)(uintptr_t)1;break;case 1:sys->emtp[a->id]=(void *)(uintptr_t)1;break;case 2:a->mlwP=(void *)(uintptr_t)1;break;case 3:a->mtx=(void *)(uintptr_t)1;break;
   case 4:a->flg|=0x80;break;case 5:a->mode2=1;break;case 6:a->ct2=15;break;case 7:subpl_mv00_tbl[0]=mv00_subpl0_1;break;case 8:a->px=NAN;break;case 9:a->mlwP->objP[0].child=NULL;break;case 10:a->mlwP->owP[0].mtx[0]=NAN;break;case 11:a->id=159;break;
   case 12:a->px=1e30f;break;case 13:a->mlwP->objP[0].pos[0]=1e30f;break;case 14:a->mlwP->owP[0].flg|=1;break;}
  reject();sys->emtp[saved[1].id]=bank;subpl_mv00_tbl[0]=callback;
 }
 restore();begin(1);actor(1)->ct2=1;check(bhStepNativeRoomActors()!=NULL,"unported alternate normal callback was admitted");
 sys->sp_flg&=~2u;BH_PWORK before=*actor(1);check(!bhStepNativeRoomActors()&&!memcmp(&before,actor(1),sizeof(before)),"task pause changed held actor");
 puts("PASS fifteen malformed second-actor contexts reject atomically; alternate normal callback remains gated; original task pause is a no-op");
 rx_events_reset();rom->evtp=original;restore();sys->sp_flg=sp;check(!o.events_ready,"full opening unexpectedly enabled");rx_opening_end(&o);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}


