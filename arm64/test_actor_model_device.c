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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL actor model: %s\n",why);exit(1);}}
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
static void hierarchy_nodes(ML_WORK *model,const NJS_CNK_OBJECT *node,const Matrix parent,unsigned *next){while(node){unsigned index=(*next)++;check(index<model->obj_num,"reference node extent");const NJS_CNK_OBJECT *flat=model->objP+index;Matrix current;memcpy(current,parent,sizeof(current));transform(current,flat->pos[0],flat->pos[1],flat->pos[2],flat->ang[0],flat->ang[1],flat->ang[2]);if(model->owP[index].flg&1)for(unsigned k=0;k<16;++k)current[k]=model->owP[index].mtx[k];matrix(model->owP[index].mtx,current);if(node->child)hierarchy_nodes(model,node->child,current,next);node=node->sibling;}}
static void hierarchy(ML_WORK *model,const NJS_CNK_OBJECT *node,const Matrix parent){unsigned next=0;hierarchy_nodes(model,node,parent,&next);check(next==model->obj_num,"reference facial tree count");}

static unsigned char data[64];static RxRoomScripts script;
static const char *run(unsigned record){
 memset(data,0,sizeof(data));data[0]=12;data[4]=18;data[8]=20;data[12]=20;
 data[20]=0x65;data[21]=1;data[22]=record;data[24]=0x69;data[25]=47;data[26]=0xff;
 check(!rx_room_scripts_open(data,28,&script),"actor recalc task bank");rom->evtp=&script;return rx_events_start_room();
}
static ML_WORK owned;static unsigned rejects;
static void reject(BH_PWORK *actor){
 BH_PWORK before=*actor;NJS_CNK_OBJECT nodes[64];O_WORK work[64];
 memcpy(nodes,owned.objP,owned.obj_num*sizeof(*nodes));memcpy(work,owned.owP,owned.obj_num*sizeof(*work));
 check(run(1)!=NULL,"invalid actor model reached source");
 check(!memcmp(&before,actor,sizeof(before))&&!memcmp(nodes,owned.objP,owned.obj_num*sizeof(*nodes))&&
       !memcmp(work,owned.owP,owned.obj_num*sizeof(*work)),"rejected recalc modified actor or model");++rejects;
}
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&opening,0);check(!e,e);
 RxRoomScripts *original=rom->evtp;check(original->data[5304]==0x69&&original->data[5305]==47,"actual opening actor recalc bytes");
 O_WRK old_child=sys->obwp[4];unsigned child_n=old_child.mlwP->obj_num;check(child_n<=64,"child snapshot capacity");O_WORK child_work[64];memcpy(child_work,old_child.mlwP->owP,child_n*sizeof(*child_work));
 unsigned samples=0;
 for(unsigned record=0;record<2;++record){BH_PWORK *actor=ene+rom->enep[record].wrk_no;BH_PWORK saved=*actor;ML_WORK *model=actor->mlwP;unsigned n=model->obj_num;
  check(n==31,"owned facial skeleton fixture");NJS_CNK_OBJECT nodes[64];O_WORK work[64];memcpy(nodes,model->objP,n*sizeof(*nodes));memcpy(work,model->owP,n*sizeof(*work));
  for(unsigned sample=0;sample<4096;++sample){unsigned heading=sample/64,override=(sample/2)%32,retained=sample%2;
   *actor=saved;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));
   actor->flg&=~0x1080u;if(retained)actor->flg|=0x1000;
   actor->px=13.5f;actor->py=-2.75f;actor->pz=7.125f;actor->ax=heading*137;actor->ay=heading*1024;actor->az=heading*293;
   Matrix root;identity(root);transform(root,retained?-3:actor->px,retained?5:actor->py,retained?11:actor->pz,retained?4096:actor->ax,retained?8192:actor->ay,retained?2048:actor->az);
   if(retained)for(unsigned k=0;k<16;++k)((float *)actor->mtx)[k]=root[k];
   if(override<n){model->owP[override].flg|=1;Matrix replace;identity(replace);transform(replace,1.5,2.25,-3.75,2048,4096,8192);for(unsigned k=0;k<16;++k)model->owP[override].mtx[k]=replace[k];}
   O_WORK expected_override;if(override<n)expected_override=model->owP[override];
   sys->obwp[4]=old_child;sys->obwp[4].flg|=0x81;sys->obwp[4].lkwkp=(void *)actor;sys->obwp[4].lkono=heading%n;O_WRK child_before=sys->obwp[4];
   BH_PWORK before=*actor;e=run(record);check(!e,e);matrix((float *)actor->mtx,root);hierarchy(model,model->objP,root);
   if(override<n)check(!memcmp(&expected_override,model->owP+override,sizeof(expected_override)),"original matrix override was overwritten");
   memcpy(before.mtxbuf,actor->mtxbuf,64);check(!memcmp(&before,actor,sizeof(before)),"recalc advanced motion/controller/scalar state");
   check(!memcmp(nodes,model->objP,n*sizeof(*nodes)),"recalc changed model pose keys");
   check(!memcmp(&child_before,sys->obwp+4,sizeof(child_before))&&!memcmp(child_work,old_child.mlwP->owP,child_n*sizeof(*child_work)),"model-only operation recalculated linked child");
   O_WORK once[64];memcpy(once,model->owP,n*sizeof(*once));e=run(record);check(!e,e);check(!memcmp(once,model->owP,n*sizeof(*once)),"repeated model-only publication drifted");++samples;
  }
  *actor=saved;memcpy(model->objP,nodes,n*sizeof(*nodes));memcpy(model->owP,work,n*sizeof(*work));
 }
 sys->obwp[4]=old_child;
 printf("PASS %u original actor-only recalculations, both actors/64 headings, retained root and all 31 joint overrides; %u independent matrix channels (max %.9g), immutable motion/pose/linked child and exact repeat\n",samples,channels,maximum);
 BH_PWORK *actor=ene+rom->enep[1].wrk_no;BH_PWORK saved=*actor;owned=*actor->mlwP;
 actor->mlwP=(void *)(uintptr_t)1;reject(actor);*actor=saved;
 actor->mlwP->objP=(void *)(uintptr_t)1;reject(actor);*actor->mlwP=owned;
 actor->mlwP->owP=(void *)(uintptr_t)1;reject(actor);*actor->mlwP=owned;
 actor->mlwP->obj_num++;reject(actor);*actor->mlwP=owned;
 actor->mtx=(void *)(uintptr_t)1;reject(actor);*actor=saved;
 actor->flg|=0x80;actor->lkwkp=(void *)(uintptr_t)1;reject(actor);*actor=saved;
 actor->px=NAN;reject(actor);*actor=saved;
 actor->id=1;reject(actor);*actor=saved;
 NJS_CNK_OBJECT node=owned.objP[0];owned.objP[0].pos[0]=NAN;reject(actor);owned.objP[0]=node;
 owned.objP[0].child=NULL;reject(actor);owned.objP[0]=node;
 float scalar=owned.owP[0].mtx[0];owned.owP[0].mtx[0]=NAN;reject(actor);owned.owP[0].mtx[0]=scalar;
 actor->flg|=0x1000;((float *)actor->mtx)[0]=NAN;reject(actor);*actor=saved;
 int stack_max=lNaMatMatrixStuckMax;lNaMatMatrixStuckMax=lNaMatMatrixStuckCnt;reject(actor);lNaMatMatrixStuckMax=stack_max;
 ETTY_WORK *records=rom->enep;rom->enep=(void *)(uintptr_t)1;check(bhCheckNativeActorModel(actor)!=NULL,"foreign room mapping accepted");rom->enep=records;
 check(bhCheckNativeActorModel((void *)(uintptr_t)1)!=NULL,"foreign actor accepted");
 /* Dispatch rechecks the selected task after a frame yield. */
 memset(data,0,sizeof(data));data[0]=12;data[4]=18;data[8]=20;data[12]=20;data[20]=0x65;data[21]=1;data[22]=1;data[24]=0xfe;data[25]=0x69;data[26]=47;data[27]=0xff;
 check(!rx_room_scripts_open(data,29,&script),"delayed model fixture");rom->evtp=&script;check(!rx_events_start_room(),"delayed model start");bhEtask[0].work=(void *)(uintptr_t)1;
 check(rx_events_tick()!=NULL&&!memcmp(&saved,actor,sizeof(saved)),"foreign late task reached original recalc");
 printf("PASS %u malformed actor/model/transform/hierarchy/matrix/stack guards, foreign mapping/target and delayed task guard before pose writes\n",rejects);
 rx_events_reset();rom->evtp=original;check(!opening.events_ready,"full opening unexpectedly enabled");rx_opening_end(&opening);
 check(bhCheckNativeActorModel(actor)!=NULL,"actor model owner survived teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}

