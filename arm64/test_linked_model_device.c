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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL linked model: %s\n",why);exit(1);}}
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

static unsigned char data[64];static RxRoomScripts script;
static const char *run(unsigned record,unsigned update){
 memset(data,0,sizeof(data));data[0]=12;data[4]=18;data[8]=20;data[12]=20;
 data[20]=0x65;data[21]=1;data[22]=record;
 const unsigned char command[]={0x69,24,0,1,1,0,update,6};memcpy(data+24,command,8);data[32]=0xff;
 check(!rx_room_scripts_open(data,34,&script),"link task bank");rom->evtp=&script;return rx_events_start_room();
}
static void rejected(BH_PWORK *actor,O_WRK *child){
 BH_PWORK before=*actor;O_WRK cb=*child;unsigned n=actor->mlwP->obj_num;
 NJS_CNK_OBJECT nodes[64];O_WORK work[64];memcpy(nodes,actor->mlwP->objP,n*sizeof(*nodes));memcpy(work,actor->mlwP->owP,n*sizeof(*work));
 check(run(1,0)!=NULL,"malformed link accepted");
 check(!memcmp(&before,actor,sizeof(before))&&!memcmp(&cb,child,sizeof(cb))&&
       !memcmp(nodes,actor->mlwP->objP,n*sizeof(*nodes))&&!memcmp(work,actor->mlwP->owP,n*sizeof(*work)),"malformed link changed parent/child pose before rejection");
}
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&opening,0);check(!e,e);
 RxRoomScripts *original=rom->evtp;check(!memcmp(original->data+5294,(unsigned char[]){0x69,24,0,1,1,0,0,6},8),"actual opening link-update command");
 O_WRK objects[32],items[32];memcpy(objects,sys->obwp,sizeof(objects));memcpy(items,sys->itwp,sizeof(items));unsigned cases=0,models=0;
 for(unsigned record=0;record<2;++record){BH_PWORK *actor=ene+rom->enep[record].wrk_no;BH_PWORK saved=*actor;
  for(unsigned family=0;family<2;++family){O_WRK *pool=family?sys->itwp:sys->obwp;unsigned count=family?rom->itm_n:rom->obj_n;
   for(unsigned i=family?0:4;i<count;++i){O_WRK *child=pool+i;if(!child->mlwP||!child->mlwP->obj_num)continue;
    unsigned n=child->mlwP->obj_num;check(n<=64,"linked child fixture capacity");O_WORK work[64];memcpy(work,child->mlwP->owP,n*sizeof(*work));O_WRK saved_child=*child;
    printf("LINK record%u family%u slot%u nodes%u\n",record,family,i,n);++models;
    for(unsigned test=0;test<512;++test){unsigned update=test%8,heading=test/8;
     *actor=saved;actor->flg&=~0x1080u;actor->hokan_count=0;actor->mtn_no=0;actor->px=3.5f;actor->py=-2;actor->pz=7;actor->ax=heading*137;actor->ay=heading*997;actor->az=heading*67;
     *child=saved_child;child->flg=(child->flg|0x81)&~0x1000u;if(heading&1)child->flg|=0x1000;
     child->lkwkp=(void *)actor;child->lkono=heading%actor->mlwP->obj_num;child->lox=1.25f;child->loy=-3.5f;child->loz=7.125f;
     child->ax=heading*113;child->ay=heading*331;child->az=heading*37;
     memcpy(child->mlwP->owP,work,n*sizeof(*work));O_WRK before=*child;
     e=run(record,update);check(!e,e);
     if(update==0||update==4||update==6||update==7){
      Matrix root;for(unsigned k=0;k<16;++k)root[k]=actor->mlwP->owP[child->lkono].mtx[k];
      transform(root,child->lox,child->loy,child->loz,0,0,0);
      if(child->flg&0x1000){double x=root[12],y=root[13],z=root[14];identity(root);root[12]=x;root[13]=y;root[14]=z;}
      rotation(root,2,child->az);rotation(root,1,child->ay);rotation(root,0,child->ax);
      matrix((float *)child->mtx,root);hierarchy(child->mlwP,child->mlwP->objP,root);
      memcpy(before.mtxbuf,child->mtxbuf,64);check(!memcmp(&before,child,sizeof(before)),"link update changed non-matrix child state");
      O_WORK once[64];memcpy(once,child->mlwP->owP,n*sizeof(*once));O_WRK stable=*child;e=run(record,4);check(!e,e);
      check(!memcmp(&stable,child,sizeof(stable))&&!memcmp(once,child->mlwP->owP,n*sizeof(*once)),"repeated link-only publication drifted");
     }else check(!memcmp(&before,child,sizeof(before))&&!memcmp(work,child->mlwP->owP,n*sizeof(*work)),"non-link selector changed child matrices");
     ++cases;
    }
    *child=saved_child;memcpy(child->mlwP->owP,work,n*sizeof(*work));
   }
  }*actor=saved;
 }
 check(models>2,"no original prop/item link models exercised");
 printf("PASS %u original linked prop/item updates across %u models, both parents, all eight selectors, 64 headings and rotation-reset branches; %u independent matrix channels (maximum %.9g)\n",cases,models,channels,maximum);
 /* Original effects use the parent's joint plus offsets/rotation, without
    bhCalcTree or the object-only rotation-reset flag. */
 O_WRK original_effect=eff[511];
 for(unsigned record=0;record<2;++record){BH_PWORK *parent=ene+rom->enep[record].wrk_no;BH_PWORK saved_parent=*parent;
  for(unsigned test=0;test<256;++test){unsigned update=test%8,heading=test/8;
   memset(eff+511,0,sizeof(*eff));O_WRK *effect=eff+511;effect->flg=0x201081;effect->id=effect->type=1;effect->mlwP=&sys->efm[1];effect->mtx=(void *)effect->mtxbuf;
   effect->lkwkp=(void *)parent;effect->lkono=heading%parent->mlwP->obj_num;effect->lox=1.25;effect->loy=-2.5;effect->loz=5;
   effect->ax=heading*133;effect->ay=heading*1017;effect->az=heading*17;O_WRK before=*effect;
   e=run(record,update);check(!e,e);
   if(update==0||update==4||update==6||update==7){Matrix root;for(unsigned k=0;k<16;++k)root[k]=parent->mlwP->owP[effect->lkono].mtx[k];
    transform(root,effect->lox,effect->loy,effect->loz,effect->ax,effect->ay,effect->az);matrix((float *)effect->mtx,root);memcpy(before.mtxbuf,effect->mtxbuf,64);
   }check(!memcmp(&before,effect,sizeof(before)),"effect update altered scalar state or wrong selector wrote matrix");
  }*parent=saved_parent;
 }eff[511]=original_effect;
 puts("PASS 512 original linked shadow root updates, all selectors, both parents, with original effect rotation semantics");
 BH_PWORK *actor=ene+rom->enep[1].wrk_no;O_WRK *child=sys->obwp+4;O_WRK saved=*child;check(child->mlwP&&child->mlwP->obj_num,"negative fixture model");
 child->flg|=0x81;child->lkwkp=(void *)actor;child->lkono=0;O_WRK valid=*child;ML_WORK model=*child->mlwP;
 child->lkono=actor->mlwP->obj_num;rejected(actor,child);*child=valid;
 child->mtx=(void *)(uintptr_t)1;rejected(actor,child);*child=valid;
 child->lox=NAN;rejected(actor,child);*child=valid;
 child->mlwP=(void *)(uintptr_t)1;rejected(actor,child);*child=valid;
 child->mlwP->owP=(void *)(uintptr_t)1;rejected(actor,child);*child->mlwP=model;
 child->mlwP->objP=(void *)(uintptr_t)1;rejected(actor,child);*child->mlwP=model;
 child->mlwP->obj_num++;rejected(actor,child);*child->mlwP=model;
 NJS_CNK_OBJECT *node=model.objP;NJS_CNK_OBJECT old=*node;node->child=node->child?NULL:(void *)(uintptr_t)1;rejected(actor,child);*node=old;
 node->pos[0]=NAN;rejected(actor,child);*node=old;
 float scalar=model.owP[0].mtx[0];model.owP[0].mtx[0]=NAN;rejected(actor,child);model.owP[0].mtx[0]=scalar;
 O_WRK *pool=sys->obwp;sys->obwp=(void *)(uintptr_t)1;rejected(actor,child);sys->obwp=pool;
 int depth=lNaMatMatrixStuckMax;lNaMatMatrixStuckMax=lNaMatMatrixStuckCnt;rejected(actor,child);lNaMatMatrixStuckMax=depth;
 /* An invalid late child must reject before the valid earlier child's matrix
    is written by the original whole-pool traversal. */
 O_WRK tail=eff[511];eff[511].flg=0x81;eff[511].lkwkp=(void *)actor;O_WORK before[64];memcpy(before,model.owP,model.obj_num*sizeof(*before));
 rejected(actor,child);check(!memcmp(before,model.owP,model.obj_num*sizeof(*before)),"late invalid child changed an earlier child");eff[511]=tail;
 *child=saved;memcpy(sys->obwp,objects,sizeof(objects));memcpy(sys->itwp,items,sizeof(items));
 puts("PASS foreign model/work/pool, invalid joint/transform/hierarchy/matrix/stack and late-child rejection before original pose writes");
 rx_events_reset();rom->evtp=original;check(!opening.events_ready,"full opening unexpectedly enabled");rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}

