#include "opening.h"
#include "face_pose.h"
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
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL actor skin: %s\n",why);exit(1);}}
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
static void setup(unsigned i,unsigned clip,unsigned mode,unsigned reset,int fixed,int step,unsigned md){BH_PWORK *a=actor(i);a->mskp=NULL;a->flg=(a->flg&~0x1080u)|1;a->stflg=0;a->mode0=mode;a->mode1=clip;a->mode2=0;a->mode3=reset?0:1;a->ct2=6;a->mnwP=sys->rmthp;a->mtn_tp=NULL;a->mtn_md=md;a->mtn_no=reset?9:clip;a->frm_no=fixed;a->mtn_add=step;a->hokan_count=reset?17:0;a->frm_mode=3;a->spd=0.125f;a->px=3.5f;a->py=-2;a->pz=7;a->ax=137;a->ay=251;a->az=17;}

static unsigned cases,vertex_channels,parts_seen,levels_seen;static double maximum_vertex;
static void verify(unsigned record,unsigned clip,unsigned frame,unsigned fraction,unsigned rotation_index){
 restore();setup(record,clip,1,0,frame*65536+fraction,32768,0x28);actor(1-record)->stflg=0x40000000;
 BH_PWORK *a=actor(record);a->ax=rotation_index*911;a->ay=rotation_index*7001;a->az=rotation_index*307;
 a->px=rotation_index*17.25f;a->py=-(float)rotation_index*2.5f;a->pz=100+rotation_index*3.125f;
 check(!bhStepNativeRoomActors(),"original actor world motion phase");
 RxFacePose *pose=bhNativeRoomActorPose(record);const ML_WORK *model=rx_face_pose_model(pose);const RxFaceMesh *mesh=rx_face_pose_mesh(pose);const MASK_WORK *fm=rx_face_mesh_work(mesh);check(model->objP[1].evalflags&0x80000000u,"original skin owner must skip joint at draw");size_t bytes=rx_face_mesh_bytes(mesh);
 unsigned char *source=malloc(bytes);check(source!=NULL,"source snapshot");memcpy(source,fm->dst->vlist,bytes);BH_PWORK before=*a;NJS_CNK_OBJECT object_before[31];O_WORK matrix_before[31];memcpy(object_before,model->objP,sizeof(object_before));memcpy(matrix_before,model->owP,sizeof(matrix_before));
 const char *error=bhPublishNativeRoomActors();if(error)fprintf(stderr,"actor%u mdflg%x gm%x st%x: %s\n",record,a->mdflg,sys->gm_flg,sys->st_flg,error);check(!error,error);
 check(!memcmp(&before,a,sizeof(before))&&!memcmp(object_before,model->objP,sizeof(object_before))&&!memcmp(matrix_before,model->owP,sizeof(matrix_before)),"skin publication reevaluated motion or modified world joints");
 check(!memcmp(source,fm->dst->vlist,bytes),"skin publication deformed facial source");
 size_t count;const unsigned *weights=rx_face_pose_weights(pose,&count);const float *rest=(const float*)fm->dst->vlist+16,*got=(const float*)model->objP[1].model->vlist+16;
 const float levels[]={.1f,.2f,.3f,.4f,.5f,.6f,.7f,.8f,.9f,1};
 for(size_t v=0;v<count;++v){unsigned joint=weights[v]>>24,level=(weights[v]>>16)&255;if(joint>10)joint+=4;levels_seen|=1u<<level;
  Matrix full,partial;for(unsigned k=0;k<16;++k)full[k]=model->owP[joint].mtx[k];full[12]-=a->px;full[13]-=a->py;full[14]-=a->pz;memcpy(partial,full,sizeof(full));rotation(partial,0,-model->objP[joint].ang[0]);rotation(partial,1,-model->objP[joint].ang[1]);rotation(partial,2,-model->objP[joint].ang[2]);
  for(unsigned normal=0;normal<2;++normal)for(unsigned k=0;k<3;++k){const float *p=rest+v*8+normal*4;
   double base=partial[k]*p[0]+partial[4+k]*p[1]+partial[8+k]*p[2]+(normal?0:partial[12+k]);double end=full[k]*p[0]+full[4+k]*p[1]+full[8+k]*p[2]+(normal?0:full[12+k]);double expected=level==9?end:base+(end-base)*levels[level],d=fabs(got[v*8+normal*4+k]-expected);if(d>maximum_vertex)maximum_vertex=d;check(isfinite(got[v*8+normal*4+k])&&d<.00005,"original world skin position/normal differs from independent blend");++vertex_channels;}
  check(!memcmp(got+v*8+3,rest+v*8+3,4)&&!memcmp(got+v*8+7,rest+v*8+7,4),"skin metadata lane changed");
 }
 NJS_MATRIX view;njUnitMatrix(&view);njTranslate(&view,3,5,7);njRotateY(&view,2711);RxRoomDrawPart parts[31];size_t n=0;check(!rx_face_pose_parts(pose,&view,parts,&n)&&n,"published actor draw parts");size_t at=0;
 for(unsigned i=0;i<31;++i){const NJS_CNK_OBJECT *node=model->objP+i;if(!node->model||(node->evalflags&8))continue;Matrix expected;for(unsigned k=0;k<16;++k)expected[k]=view[k];if(node->evalflags&0x80000000u){Matrix t;identity(t);t[12]=a->px;t[13]=a->py;t[14]=a->pz;product(expected,t);}else{Matrix t;for(unsigned k=0;k<16;++k)t[k]=model->owP[i].mtx[k];product(expected,t);}matrix(*parts[at].matrix,expected);check(parts[at].model==node->model,"flat draw order changed");++at;++parts_seen;}
 check(at==n,"draw part extent");unsigned char *snapshot=malloc(bytes);check(snapshot!=NULL,"repeat snapshot");memcpy(snapshot,model->objP[1].model->vlist,bytes);check(!bhPublishNativeRoomActors()&&!memcmp(snapshot,model->objP[1].model->vlist,bytes),"repeated skin accumulated deformation");free(snapshot);free(source);++cases;
}
static unsigned char baseline[640*480*4],rendered[sizeof(baseline)],repeat[sizeof(baseline)];
static void capture(RxOpening *o,unsigned char *out){glViewport(0,0,640,480);const char *e=rx_opening_present(o);check(!e,e);glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,out);check(!glGetError(),"Quest actor GPU capture");}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&opening,0);check(!e,e);check(opening.commands_ready&&!opening.events_ready,"full opening stays gated");check(bhNativeRoomActorCount()==2,"opening actor count");
 for(unsigned i=0;i<2;++i){saved[i]=*actor(i);memcpy(nodes[i],saved[i].mlwP->objP,sizeof(*nodes[i])*31);memcpy(work[i],saved[i].mlwP->owP,sizeof(*work[i])*31);}unsigned flags=sys->sp_flg;sys->sp_flg|=2;
 for(unsigned i=0;i<2;++i)for(unsigned clip=0;clip<10;++clip){unsigned last=sys->rmthp[clip].frm_num-1;unsigned frames[]={0,1,last/2,last};for(unsigned f=0;f<4;++f)for(unsigned fraction=0;fraction<=32768;fraction+=32768)for(unsigned r=0;r<3;++r)verify(i,clip,frames[f],fraction,r);}
 check(levels_seen==1023,"all ten original weights");printf("PASS %u world-skin samples, %u independent position/normal channels (max error %.9g), %u draw parts / %u matrix channels (max error %.9g); immutable actor clock/joints/facial source and repeat stability\n",cases,vertex_channels,maximum_vertex,parts_seen,channels,maximum);
 restore();for(unsigned i=0;i<2;++i){setup(i,0,1,0,0,32768,0x28);}check(!bhStepNativeRoomActors()&&!bhPublishNativeRoomActors(),"prepare atomic batch");size_t size[2];unsigned char *snap[2];for(unsigned i=0;i<2;++i){RxFacePose *p=bhNativeRoomActorPose(i);size[i]=rx_face_mesh_bytes(rx_face_pose_mesh(p));snap[i]=malloc(size[i]);check(snap[i]!=NULL,"batch snapshot");memcpy(snap[i],actor(i)->mlwP->objP[1].model->vlist,size[i]);}
 for(unsigned bad=0;bad<9;++bad){BH_PWORK *a=actor(1);BH_PWORK before=*a;O_WORK mat=a->mlwP->owP[0];NJS_CNK_OBJECT node=a->mlwP->objP[0];switch(bad){case 0:a->draw_tp=1;break;case 1:a->mdflg|=2;break;case 2:a->px=NAN;break;case 3:a->mlwP->owP[0].mtx[0]=NAN;break;case 4:a->mlwP->objP[0].model=(void*)(uintptr_t)1;break;case 5:a->mlwP->objP[0].ang[0]=INT_MIN;break;case 6:a->flg|=0x80;break;case 7:a->mdl_no=1;break;case 8:a->mlwP->owP[0].mtx[12]=1e30f;break;}
  check(bhPublishNativeRoomActors()!=NULL,"invalid second actor skin accepted");for(unsigned i=0;i<2;++i)check(!memcmp(snap[i],actor(i)->mlwP->objP[1].model->vlist,size[i]),"invalid second actor skin published a prefix");*a=before;a->mlwP->owP[0]=mat;a->mlwP->objP[0]=node;}
 puts("PASS nine malformed second-actor skin batches reject before skin output writes");for(unsigned i=0;i<2;++i)free(snap[i]);
 RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"Quest GLES pbuffer");restore();for(unsigned i=0;i<2;++i){setup(i,0,1,0,0,0,0x28);actor(i)->px=plp->px+(i?4:-4);actor(i)->py=plp->py;actor(i)->pz=plp->pz;actor(i)->ax=actor(i)->az=0;actor(i)->ay=plp->ay;}
 check(!bhStepNativeRoomActors(),"GPU world motion");
 /* Isolate the GPU actor at Claire's position: the authored opening clip has
    a large stage-space root offset. Production keeps that offset unchanged. */
 for(unsigned i=0;i<2;++i){BH_PWORK *a=actor(i);memset(a->mlwP->objP[0].pos,0,12);bhCalcModel(a);}
 sys->sp_flg&=~0x80u;sys->ts_flg|=0x200;capture(&opening,baseline);capture(&opening,baseline);opening.events_ready=1;capture(&opening,rendered);capture(&opening,repeat);opening.events_ready=0;
 unsigned changed=0;for(unsigned i=0;i<640*480;++i)if(memcmp(baseline+i*4,rendered+i*4,3))++changed;printf("Actor GPU changed pixels %u\n",changed);check(changed>100,"no visible actor publication on Quest GPU");unsigned differences=0;for(unsigned i=0;i<640*480;++i)if(memcmp(rendered+i*4,repeat+i*4,3))++differences;printf("Repeat GPU differences %u\n",differences);
 FILE *debug=fopen("/data/local/tmp/v71-skin-frames.rgba","wb");if(debug){fwrite(baseline,1,sizeof(baseline),debug);fwrite(rendered,1,sizeof(rendered),debug);fwrite(repeat,1,sizeof(repeat),debug);fclose(debug);}
 check(!memcmp(rendered,repeat,sizeof(rendered)),"repeated actor rendering unstable");check(opening.actor_texture_count&&opening.actor_textures,"actor textures not cached");rx_opening_close_graphics(&opening);check(!opening.actor_textures&&!opening.actor_texture_count,"actor texture cache not released");rx_egl_close(&egl);puts("PASS original actor textures and published skin render through Quest GLES, stable repeats and cache cleanup");
 restore();sys->sp_flg=flags;rx_opening_end(&opening);check(bhPublishNativeRoomActors()!=NULL&&!bhNativeRoomActorPose(0),"retired actor publisher accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
