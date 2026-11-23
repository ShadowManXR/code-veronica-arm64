#include "opening.h"
#include "face_pose.h"
#include "face_control_runtime.h"
#include "face_sampling.h"
#include "face.h"
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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL live face: %s\n",why);exit(1);}}
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


static BH_PWORK saved[2];static NJS_CNK_OBJECT nodes[2][64];static O_WORK work[2][64];
static BH_PWORK *actor(unsigned i){return &ene[rom->enep[i].wrk_no];}
static void restore(void){for(unsigned i=0;i<2;++i){*actor(i)=saved[i];memcpy(actor(i)->mlwP->objP,nodes[i],sizeof(*nodes[i])*actor(i)->mlwP->obj_num);memcpy(actor(i)->mlwP->owP,work[i],sizeof(*work[i])*actor(i)->mlwP->obj_num);}}
static void setup(unsigned i,unsigned clip,unsigned mode,unsigned reset,int fixed,int step,unsigned md){BH_PWORK *a=actor(i);a->flg=(a->flg&~0x1080u)|1;a->stflg=0;a->mode0=mode;a->mode1=clip;a->mode2=0;a->mode3=reset?0:1;a->ct2=6;a->mnwP=sys->rmthp;a->mtn_tp=NULL;a->mtn_md=md;a->mtn_no=reset?9:clip;a->frm_no=fixed;a->mtn_add=step;a->hokan_count=reset?17:0;a->frm_mode=3;a->spd=0.125f;a->px=3.5f;a->py=-2;a->pz=7;a->ax=137;a->ay=251;a->az=17;}


typedef struct {BH_PWORK actor;int32_t words[273];MASK_WORK face;HEAD_WORK head;LIP_WORK lip;NJS_CNK_OBJECT nodes[31];O_WORK matrices[31];unsigned char *buffers;size_t geometry,tongue;} Snapshot;
static void take(unsigned i,Snapshot *s){BH_PWORK *a=actor(i);const RxFaceMesh *mesh=rx_face_pose_mesh(bhNativeRoomActorPose(i));s->actor=*a;memcpy(s->words,a->exp0,sizeof(s->words));s->face=*(MASK_WORK*)a->exp1;s->head=*s->face.head;s->lip=*(LIP_WORK*)a->exp2;memcpy(s->nodes,a->mlwP->objP,sizeof(s->nodes));memcpy(s->matrices,a->mlwP->owP,sizeof(s->matrices));s->geometry=rx_face_mesh_bytes(mesh);s->tongue=rx_face_mesh_tongue_bytes(mesh);s->buffers=malloc(s->geometry*2+s->tongue);check(s->buffers!=NULL,"snapshot allocation");memcpy(s->buffers,s->face.dst->vlist,s->geometry);memcpy(s->buffers+s->geometry,a->mlwP->objP[1].model->vlist,s->geometry);memcpy(s->buffers+s->geometry*2,s->face.tangdst->vlist,s->tongue);}
static void unchanged(unsigned i,Snapshot *s){BH_PWORK *a=actor(i);check(!memcmp(&s->actor,a,sizeof(*a))&&!memcmp(s->words,a->exp0,sizeof(s->words))&&!memcmp(&s->face,a->exp1,sizeof(s->face))&&!memcmp(&s->head,((MASK_WORK*)a->exp1)->head,sizeof(s->head))&&!memcmp(&s->lip,a->exp2,sizeof(s->lip))&&!memcmp(s->nodes,a->mlwP->objP,sizeof(s->nodes))&&!memcmp(s->matrices,a->mlwP->owP,sizeof(s->matrices)),"validation/paused/rejected batch changed actor, face, lip or joint state");check(!memcmp(s->buffers,s->face.dst->vlist,s->geometry)&&!memcmp(s->buffers+s->geometry,a->mlwP->objP[1].model->vlist,s->geometry)&&!memcmp(s->buffers+s->geometry*2,s->face.tangdst->vlist,s->tongue),"validation/paused/rejected batch changed mesh output");}
static void preview(unsigned i){Snapshot s;take(i,&s);VuState vu=g_vu0;const char *e=rx_face_control_tick_check(actor(i));check(!e,e);unchanged(i,&s);check(!memcmp(&vu,&g_vu0,sizeof(vu)),"facial validation changed VU registers");free(s.buffers);}
static void configure(unsigned i){setup(i,0,1,0,0,32768,0x28);bhInitMask(actor(i));}
static unsigned samples,lip_samples;
static void pose_check(unsigned i,const PARAM_WORK *expected){BH_PWORK *a=actor(i);MASK_WORK *fm=(MASK_WORK*)a->exp1;ML_WORK *m=a->mlwP;check(!memcmp(expected,&fm->param,160),"live actor sampled wrong original expression/lip parameters");check(m->objP[7].ang[0]==fm->toothsrc->ang[0]&&m->objP[11].ang[0]==fm->tangorg->ang[0]&&m->objP[9].ang[0]==-fm->eyesrc[0]->ang[0]&&m->objP[9].ang[1]==-fm->eyesrc[0]->ang[1]&&m->objP[13].ang[0]==-fm->eyesrc[1]->ang[0]&&m->objP[13].ang[1]==-fm->eyesrc[1]->ang[1],"body motion overwrote protected facial angles");Matrix root;identity(root);transform(root,a->px,a->py,a->pz,a->ax,a->ay,a->az);hierarchy(m,m->objP,root);BH_PWORK before=*a;check(!bhPublishNativeRoomActors(),"live face skin publication");check(!memcmp(&before,a,sizeof(before)),"skin advanced live face/body controller");++samples;}
static size_t bank_bytes(const unsigned char *p){size_t at=0;for(;;){unsigned n;memcpy(&n,p+at,4);at+=4;if(n==UINT_MAX)return at;check(n<1000000,"bank fixture extent");at+=n;}}
static void step(void){const char *e=bhStepNativeRoomActors();check(!e,e);}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&opening,0);check(!e,e);check(opening.commands_ready&&!opening.events_ready,"complete opening remains gated");for(unsigned i=0;i<2;++i){saved[i]=*actor(i);memcpy(nodes[i],saved[i].mlwP->objP,sizeof(*nodes[i])*31);memcpy(work[i],saved[i].mlwP->owP,sizeof(*work[i])*31);}unsigned flags=sys->sp_flg;int loop_ct=sys->loop_ct,loop_now=sys->loop_now;sys->sp_flg|=2;sys->loop_ct=1;sys->loop_now=0;size_t mouth=bank_bytes(sys->mspp),lips=bank_bytes(sys->lspp);
 for(unsigned i=0;i<2;++i)for(unsigned clip=0;clip<20;++clip){RxFaceClip view;check(!rx_face_clip(sys->mspp,mouth,clip,&view),"opening face clip");unsigned last=view.keys[view.count-1].frame,frames[]={0,1,last/2,last};for(unsigned f=0;f<4;++f){restore();configure(i);actor(1-i)->stflg=0x40000000;check(bhSetMask(actor(i),clip,frames[f]),"original expression request");preview(i);PARAM_WORK expected;check(!rx_face_sample(&view,actor(i)->id,frames[f],&expected),"independent facial sample");step();pose_check(i,&expected);check(actor(i)->frm_no==32768,"body clock double advanced during live facial update");}}
 for(unsigned i=0;i<2;++i)for(unsigned seq=0;seq<19;++seq){restore();configure(i);actor(1-i)->stflg=0x40000000;BH_PWORK *a=actor(i);const PARAM_WORK *base=rx_face_control_base(a);unsigned count=((int32_t*)a->exp0)[8],group=seq%(count/6);RxFaceClip neutral={base,count,0};RxLipClip clip;LIP_WORK ref;check(!rx_lip_clip(sys->lspp,lips,seq,&clip)&&!rx_lip_begin(&clip,&ref)&&bhSetLip(a,seq),"original live lip request");((int32_t*)a->exp0)[4]=group;((int32_t*)a->exp0)[0]|=4;
  for(unsigned t=0;ref.flag&&t<10000;++t){PARAM_WORK expected;check(!rx_face_sample(&neutral,a->id,0,&expected)&&!rx_lip_sample(&clip,base+group*6,&ref,&expected),"independent live lip sample");preview(i);step();pose_check(i,&expected);LIP_WORK *got=(LIP_WORK*)a->exp2;check(got->ptr==ref.ptr&&got->cnt==ref.cnt&&got->time==ref.time&&got->cur==ref.cur&&got->next==ref.next&&got->flag==ref.flag,"live lip cursor advanced twice in preflight");++lip_samples;}check(!ref.flag,"live lip did not terminate");}
 printf("PASS %u combined face/body/world/skin samples, %u lip frames across 20 expressions and 19 complete streams/both actors; %u independent protected-joint matrix channels (max error %.9g); side-effect-free preview including VU state\n",samples,lip_samples,channels,maximum);
 restore();for(unsigned i=0;i<2;++i){configure(i);check(bhSetMask(actor(i),4,0),"throttle expression request");}Snapshot before[2];for(unsigned i=0;i<2;++i)take(i,before+i);sys->sp_flg&=~2u;step();for(unsigned i=0;i<2;++i){unchanged(i,before+i);free(before[i].buffers);}sys->sp_flg|=2;
 sys->loop_ct=2;sys->loop_now=0;Snapshot throttle;take(0,&throttle);step();check(!memcmp(throttle.buffers,((MASK_WORK*)actor(0)->exp1)->dst->vlist,throttle.geometry),"geometry ran before original last sub-loop");free(throttle.buffers);sys->loop_now=1;step();step();check(actor(0)->flg2&0x80,"unchanged face did not freeze");sys->gm_flg|=0x200;step();check(!(actor(0)->flg2&0x80),"original forced-face update ignored");sys->gm_flg&=~0x200u;sys->loop_ct=1;sys->loop_now=0;
 puts("PASS original enemy pause, last-sub-loop geometry and unchanged-face freeze/forced-update gates");
 for(unsigned bad=0;bad<6;++bad){restore();for(unsigned i=0;i<2;++i){configure(i);check(bhSetMask(actor(i),2,0),"batch expression request");}BH_PWORK *a=actor(1);unsigned char *exp=a->exp1;int32_t *w=(int32_t*)a->exp0;const PARAM_WORK *base=rx_face_control_base(a);float key=base[0].muscle[0];switch(bad){case 0:w[265]=1;break;case 1:w[272]=INT_MAX;a->flg2&=~0x80u;break;case 2:w[1]=2;break;case 3:w[6]=-1;break;case 4:a->exp1=(void*)(uintptr_t)1;break;case 5:((PARAM_WORK*)base)[0].muscle[0]=NAN;w[0]&=~8;break;}
  /* Foreign exp1 is rejected before it is read; snapshot the valid pointer. */
  unsigned char *invalid=a->exp1;a->exp1=exp;for(unsigned i=0;i<2;++i)take(i,before+i);a->exp1=invalid;check(bhStepNativeRoomActors()!=NULL,"invalid late facial state accepted");a->exp1=exp;for(unsigned i=0;i<2;++i){unchanged(i,before+i);free(before[i].buffers);}((PARAM_WORK*)base)[0].muscle[0]=key;}
 puts("PASS six invalid second-actor facial batches, including derived nonfinite deformation, reject before clocks/controllers/joints/meshes change");
 restore();sys->sp_flg=flags;sys->loop_ct=loop_ct;sys->loop_now=loop_now;BH_PWORK *retired=actor(0);rx_opening_end(&opening);check(rx_face_control_tick_check(retired)!=NULL,"retired facial preflight accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
