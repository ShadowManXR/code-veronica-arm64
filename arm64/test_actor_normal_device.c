#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "room.h"
#include "main.h"
#include "pwksub.h"
#include "MdlPut.h"
#include "dread.h"
#include "expand.h"
#include "ps2_sg_gd.h"
#include "ps2_NaMatrix.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
extern BH_SCEWORK bhEtask[16];extern int lNaMatMatrixStuckMax,lNaMatMatrixStuckCnt;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL normal actor: %s\n",why);exit(1);}}
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

static unsigned cases;
static void verify(unsigned rec,unsigned clip,int fixed,int step,unsigned md,int count,int initialize){
 restore();BH_PWORK *a=actor(rec);actor(1-rec)->stflg=0x40000000;a->stflg=0;a->flg=(a->flg&~0x1080u)|1;a->mskp=NULL;
 a->mode0=initialize?0:1;a->mode1=clip;a->mode2=0;a->mode3=count==8?0:1;a->ct2=0;a->mnwP=sys->emtp[a->id];a->mtn_no=clip;a->mtn_md=md;a->mtn_tp=NULL;a->frm_no=fixed;a->mtn_add=step;a->hokan_count=count;a->hokan_rate=0;
 NJS_CNK_OBJECT ref[31];memcpy(ref,a->mlwP->objP,sizeof(ref));BH_PWORK expected=*a;expected.pxb=a->px;expected.pyb=a->py;expected.pzb=a->pz;expected.axb=a->ax;expected.ayb=a->ay;expected.azb=a->az;expected.flg=(a->flg|0x8100)&~2u;
 if(initialize){clip=0;fixed=0;step=32768;md=0x20;count=0;expected.mode0=1;expected.mode3=3;expected.hokan_rate=0;expected.hokan_count=0;expected.mtn_add=32768;expected.frm_no=0;expected.mtn_no=0;expected.mtn_md=0x20;expected.mtn_tp=NULL;expected.frm_mode=0;}
 else if(count==8){expected.mode3=1;expected.mtn_no=clip;expected.hokan_count=8;expected.hokan_rate=0;expected.mtn_md&=~0x100u;expected.frm_mode=0;}
 MN_WORK *m=a->mnwP+clip;
 if(m->datP){unsigned frame=(unsigned)fixed>>16,next=frame+1<m->frm_num?frame+1:0;float frac=(fixed&65535)*0.000015258789f;int slow=(m->flg&0x20)||(md&8),normal=!slow&&(m->flg&0x10);float rate=count?1.0f/count:1;
  unsigned joint=0;for(unsigned track=0;track<m->obj_num;++track){if(!normal)while(a->mlwP->owP[joint].flg&4)++joint;check(joint<31,"motion reference extent");
   if(!(a->mlwP->owP[joint].flg&4)){const float *pos=m->md2P[track].p[0];if(pos&&(slow||normal||!track))for(unsigned axis=0;axis<3;++axis){float target=pos[frame*3+axis];if(slow)target+=frac*(pos[next*3+axis]-target);if(count)ref[joint].pos[axis]+=rate*(target-ref[joint].pos[axis]);else ref[joint].pos[axis]=target;}
    if(!(a->mlwP->owP[joint].flg&2)){const unsigned short *ang=m->md2P[track].p[1];int target[3]={0};for(unsigned axis=0;axis<3;++axis)if(ang){float key=slow?(short)ang[frame*3+axis]:ang[frame*3+axis];if(slow)key+=frac*(short)(ang[next*3+axis]-ang[frame*3+axis]);target[axis]=(int)key;}
     if(count){check(!slow||ang!=NULL,"fixture requires slow blend angle keys");int alt[3]={ref[joint].ang[0]+32768,32768-ref[joint].ang[1],ref[joint].ang[2]+32768},d0=0,d1=0;for(unsigned axis=0;axis<3;++axis){d0+=abs((short)(target[axis]-ref[joint].ang[axis]));d1+=abs((short)(target[axis]-alt[axis]));}if(d1<d0)memcpy(ref[joint].ang,alt,sizeof(alt));for(unsigned axis=0;axis<3;++axis)ref[joint].ang[axis]+=(int)(rate*(short)(target[axis]-ref[joint].ang[axis]));}
     else if(ang||!slow&&!normal)memcpy(ref[joint].ang,target,sizeof(target));else ref[joint].ang[0]=0;
    }
   }++joint;
  }
  expected.frm_no=fixed+step;int period=m->frm_num*65536;if(expected.frm_no<0)expected.frm_no+=period;if(expected.frm_no>=period)expected.frm_no-=period;if(m->atrP)expected.mtn_attr=m->atrP[frame];if(count)--expected.hokan_count;
 }
 const char *e=bhStepNativeRoomActors();if(e)fprintf(stderr,"record%u clip%u frame%d count%d: %s\n",rec,clip,fixed,count,e);check(!e,e);
 for(unsigned j=0;j<31;++j){for(unsigned k=0;k<3;++k)check(fabsf(ref[j].pos[k]-a->mlwP->objP[j].pos[k])<0.00001f,"disc translation/blend differs");check(!memcmp(ref[j].ang,a->mlwP->objP[j].ang,12),"disc Euler key/blend differs");}
 Matrix root;identity(root);transform(root,a->px,a->py,a->pz,a->ax,a->ay,a->az);matrix((float*)a->mtx,root);hierarchy(a->mlwP,a->mlwP->objP,root);memcpy(expected.mtxbuf,a->mtxbuf,64);if(memcmp(&expected,a,sizeof(expected)))for(unsigned b=0;b<sizeof(expected);++b)if(((unsigned char*)&expected)[b]!=((unsigned char*)a)[b])fprintf(stderr,"rec%u slot%u init%d count%d offset%u expected%02x got%02x\n",rec,clip,initialize,count,b,((unsigned char*)&expected)[b],((unsigned char*)a)[b]);check(!memcmp(&expected,a,sizeof(expected)),"original normal callback/controller clock differs");++cases;
}
static void capture(void){for(unsigned i=0;i<2;++i){saved[i]=*actor(i);check(saved[i].mlwP->obj_num==31,"fixture model extent");memcpy(nodes[i],saved[i].mlwP->objP,31*sizeof(*nodes[i]));memcpy(work[i],saved[i].mlwP->owP,31*sizeof(*work[i]));}}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);sys->sp_flg|=2;sys->loop_ct=1;sys->loop_now=0;capture();
 for(unsigned i=0;i<2;++i){verify(i,0,0,32768,0x20,0,1);for(unsigned slot=0;slot<512;++slot)verify(i,slot,0,32768,0x20,slot<256?8:0,0);}
 printf("PASS %u original empty-bank initialization/reset callbacks, all 512 slots, both opening actors; no synthetic body motion\n",cases);
 /* Test-only fixture: use the original room clips as enemy banks, so native
    ownership and real disc keys/blending can be exercised before campaign AI. */
 GDFS file=gdFsOpen("RM_0000.RDX",NULL);Sint32 packed_bytes;check(file&&gdFsGetFileSize(file,&packed_bytes)&&packed_bytes>0,"fresh fixture disc read");unsigned sectors=((unsigned)packed_bytes+2047)/2048;unsigned char *packed=malloc(sectors*2048),*fixture=malloc(sys->rdtsz);check(packed&&fixture,"fresh fixture buffers");check(!gdFsRead(file,sectors,packed),"fresh fixture payload");gdFsClose(file);check(ExpandBounded(packed,packed_bytes,fixture,sys->rdtsz)==(int)sys->rdtsz,"fresh fixture expansion");free(packed);sys->rdtp=fixture;
 unsigned *header=(unsigned*)sys->rdtp;unsigned *table=(unsigned*)(sys->rdtp+header[6]);check(rom->ene_n==2&&table[2],"original room-motion package");table[0]=table[1]=table[2];
 e=bhStageNativeRoom();check(!e,e);bhBindNativeRoomMotions();
 for(unsigned i=0;i<2;++i)for(int j=0;j<actor(i)->mdl_n;++j){ML_WORK *m=&actor(i)->mdl[j];sys->memp=bhKeepObjWork(m,sys->memp);bhCalcTree(NULL,m);}
 for(int i=4;i<rom->obj_n;++i){ML_WORK *m=sys->obwp[i].mlwP;sys->memp=bhKeepObjWork(m,sys->memp);bhCalcTree(NULL,m);}
 for(int i=0;i<rom->itm_n;++i){ML_WORK *m=sys->itwp[i].mlwP;sys->memp=bhKeepObjWork(m,sys->memp);bhCalcTree(NULL,m);}
 e=bhRetainNativeRoomActorWork();check(!e,e);capture();
 for(unsigned i=0;i<2;++i){verify(i,0,0,32768,0x20,0,1);for(unsigned clip=0;clip<10;++clip){MN_WORK *m=sys->emtp[actor(i)->id]+clip;check(m->datP&&m->obj_num<=22,"real motion fixture");for(unsigned md=0x20;md<=0x28;md+=8){for(unsigned f=0;f<m->frm_num;++f)verify(i,clip,f*65536,32768,md,0,0);for(unsigned count=1;count<=8;++count){verify(i,clip,32768,32768,md,count,0);verify(i,clip,(m->frm_num-1)*65536+32768,-32768,md,count,0);}}}}
 printf("PASS %u total original normal callbacks, ten disc clips, initialization, reset/eight blend counts, fast/slow/fractional/reverse/wrap and %u independent matrix channels (max %.9g)\n",cases,channels,maximum);

 unsigned face_samples=0;
 for(unsigned rec=0;rec<2;++rec)for(unsigned expression=0;expression<20;++expression){restore();BH_PWORK *a=actor(rec);actor(1-rec)->stflg=0x40000000;a->flg=(a->flg&~0x1080u)|1;a->stflg=0;a->mode0=1;a->mode1=expression%10;a->mode2=a->mode3=a->ct2=0;a->mtn_md=0x28;a->mtn_tp=NULL;a->frm_no=0;a->mtn_add=32768;bhInitMask(a);check(bhSetMask(a,expression,0),"normal actor expression request");
  for(unsigned tick=0;tick<16;++tick){e=bhStepNativeRoomActors();check(!e,e);check(a->hokan_count==(tick<8?7-(int)tick:0)&&a->frm_no==(int)(tick+1)*32768,"normal live-face blend/clock");Matrix root;identity(root);transform(root,a->px,a->py,a->pz,a->ax,a->ay,a->az);hierarchy(a->mlwP,a->mlwP->objP,root);e=bhPublishNativeRoomActors();check(!e,e);++face_samples;}
 }
 printf("PASS %u combined normal body/eight-frame blend/live face/world/skin updates, all 20 expressions and both actors\n",face_samples);
 restore();for(unsigned bad=0;bad<12;++bad){restore();for(unsigned i=0;i<2;++i){BH_PWORK *a=actor(i);a->flg=(a->flg&~0x1080u)|1;a->stflg=0;a->mskp=NULL;a->mode0=1;a->mode1=0;a->mode2=0;a->mode3=1;a->ct2=0;a->mtn_no=0;a->mtn_md=0x20;a->mtn_tp=NULL;a->frm_no=0;a->hokan_count=0;a->hokan_rate=0;}
 BH_PWORK *a=actor(1);MN_WORK *m=sys->emtp[a->id],original=*m;void *key=m->md2P[0].p[0];switch(bad){case 0:a->mnwP=(void*)(uintptr_t)1;break;case 1:a->mtn_no=512;break;case 2:a->frm_no=-1;break;case 3:a->frm_no=m->frm_num*65536;break;case 4:a->hokan_count=9;break;case 5:a->mtn_tp=(void*)(uintptr_t)1;break;case 6:a->mtn_add=INT_MAX;break;case 7:m->md2P=(void*)(uintptr_t)1;break;case 8:m->datP=(void*)(uintptr_t)1;break;case 9:m->md2P[0].p[0]=(void*)(uintptr_t)1;break;case 10:a->mlwP->owP[0].flg=1;break;case 11:a->mode2=1;break;}
 BH_PWORK before[2]={*actor(0),*actor(1)};check(bhStepNativeRoomActors()!=NULL,"malformed normal actor context accepted");check(!memcmp(before,actor(0),sizeof(*before))&&!memcmp(before+1,actor(1),sizeof(*before)),"malformed second actor advanced batch prefix");*m=original;m->md2P[0].p[0]=key;
 }
 puts("PASS twelve malformed second-actor contexts reject before batch advancement; installed full opening remains gated");restore();check(!o.events_ready,"full opening unexpectedly enabled");rx_opening_end(&o);free(fixture);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
