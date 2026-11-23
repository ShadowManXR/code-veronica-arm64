#include "face_mesh.h"
#include "face_sampling.h"
#include "room_engine.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaMath.h"
#include "ps2/vu0.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern NJS_MATRIX *pNaMatMatrixStuckPtr,*pNaMatMatrixStuckTop;
extern int lNaMatMatrixStuckCnt,lNaMatMatrixStuckMax,lNaMatIsUnitMatrix;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL native face auxiliaries: %s\n",why);exit(1);}}
static unsigned word(const void *p){unsigned n;memcpy(&n,p,4);return n;}
typedef double Matrix[16];
static void identity(Matrix m){memset(m,0,sizeof(Matrix));m[0]=m[5]=m[10]=m[15]=1;}
static float sine(int angle){unsigned n=(unsigned)angle&65535u,index=n&16383u;if(n&16384u)index=16383-index;
    float s=sinf(0.0000958738f*index);return n&32768u?-s:s;}
static void product(Matrix a,const Matrix b)
{Matrix r;for(unsigned c=0;c<4;++c)for(unsigned row=0;row<4;++row){double s=0;for(unsigned k=0;k<4;++k)s+=a[k*4+row]*b[c*4+k];r[c*4+row]=s;}memcpy(a,r,sizeof(r));}
static void rotate(Matrix m,unsigned axis,int angle)
{Matrix r;identity(r);unsigned p=(axis+1)%3,q=(axis+2)%3;double s=sine(angle),c=sine(angle+16384);
    r[p*4+p]=r[q*4+q]=c;r[p*4+q]=s;r[q*4+p]=-s;product(m,r);}
static const NJS_CNK_OBJECT *find(const NJS_CNK_OBJECT *root,int ordinal)
{const NJS_CNK_OBJECT *stack[512],*n=root;unsigned depth=0,mesh=0,visited=0;
    while(n&&visited++<512){if(n->model&&mesh++==(unsigned)ordinal)return n;if(n->sibling)stack[depth++]=n->sibling;
        n=n->child?n->child:(depth?stack[--depth]:NULL);}return NULL;}
static int world(const NJS_CNK_OBJECT *n,const NJS_CNK_OBJECT *target,const Matrix parent,Matrix result,unsigned depth)
{
    if(!n||depth>512)return 0;
    Matrix m,r;memcpy(m,parent,sizeof(m));identity(r);r[12]=n->pos[0];r[13]=n->pos[1];r[14]=n->pos[2];product(m,r);
    rotate(m,2,n->ang[2]);rotate(m,1,n->ang[1]);rotate(m,0,n->ang[0]);
    identity(r);r[0]=n->scl[0];r[5]=n->scl[1];r[10]=n->scl[2];product(m,r);
    if(n==target){memcpy(result,m,sizeof(m));return 1;}
    return world(n->child,target,m,result,depth+1)||world(n->sibling,target,parent,result,depth+1);
}
static unsigned frames,channels;static double maximum;
static void frame(RxFaceMesh *mesh,const PARAM_WORK *p)
{
    const MASK_WORK *fm=rx_face_mesh_work(mesh);size_t body_bytes=rx_face_mesh_bytes(mesh),bytes=rx_face_mesh_tongue_bytes(mesh);
    unsigned char *body=malloc(body_bytes),*expected=malloc(bytes),*once=malloc(bytes);check(body&&expected&&once,"reference frame allocation");
    check(!rx_face_mesh_deform(mesh,p),"prepare body reference");memcpy(body,fm->dst->vlist,body_bytes);
    memcpy(expected,fm->tangsrc->vlist,bytes);float *dest=(float *)expected+16;const float *src=(const float *)fm->tangsrc->vlist+16;
    for(int i=0;i<fm->tnum;++i){const TANG_WORK *t=fm->tang+i;Matrix m;identity(m);
        rotate(m,0,(int)(182.04445f*(p->tangx*t->rate))&65535);rotate(m,1,(int)(182.04445f*(p->tangy*t->rate))&65535);
        for(unsigned k=0;k<3;++k)dest[t->id*8+k]=(float)(m[k]*src[t->id*8]+m[4+k]*src[t->id*8+1]+m[8+k]*src[t->id*8+2]*p->tangz+m[12+k]);}
    int angles[9][3]={{0}};
    for(unsigned i=0;i<9;++i)if(fm->eyesrc[i]){
        const float *m=(const float *)&fm->eyemat[i],*pos=fm->eyesrc[i]->pos;float q[3],r[3],gaze[3]={p->eye.x,p->eye.y,p->eye.z};
        for(unsigned k=0;k<3;++k){float point=m[k]*pos[0];point+=m[4+k]*pos[1];point+=m[8+k]*pos[2];point+=m[12+k];
            q[k]=(point+gaze[k])-point;r[k]=m[k]*0;r[k]+=m[4+k]*0;r[k]+=m[8+k]*-100;r[k]+=m[12+k];}
        float vertical=q[1]*(1/sqrtf(q[0]*q[0]+q[2]*q[2]));
        angles[i][0]=(int)(10430.381f*atanf(vertical));if(r[2]>=0)angles[i][0]=-angles[i][0];
        angles[i][1]=(int)(10430.381f*atanf(q[0]/q[2]));if(q[2]>=0)angles[i][1]+=32768;
    }
    PARAM_WORK saved=*p;check(!rx_face_mesh_animate(mesh,p)&&!memcmp(p,&saved,sizeof(saved)),"original full facial frame rejected or changed sample");
    check(!memcmp(body,fm->dst->vlist,body_bytes),"tongue/eye update changed original body/jaw output");
    const float *got=(const float *)fm->tangdst->vlist+16;unsigned count=(unsigned)((bytes-68)/32);
    check(!memcmp(expected,fm->tangdst->vlist,64)&&!memcmp(expected+bytes-4,(const char *)fm->tangdst->vlist+bytes-4,4),"tongue packet framing changed");
    for(unsigned i=0;i<count;++i){for(unsigned k=0;k<3;++k){double error=fabs((double)got[i*8+k]-dest[i*8+k]);if(error>maximum)maximum=error;
        check(isfinite(got[i*8+k])&&error<0.000001,"tongue differs from independent double rotation/scale reference");channels++;}
        check(!memcmp(got+i*8+3,src+i*8+3,20),"tongue changed skin metadata/normal/weight lanes");}
    for(unsigned i=0;i<9;++i)if(fm->eyesrc[i])check(!memcmp(fm->eyesrc[i]->ang,angles[i],sizeof(angles[i])),"original eye angles differ from independent float reference");
    int jaw=(int)(182.04445f*-p->jawang)&65535;
    check(fm->toothsrc->ang[0]==jaw&&fm->tangorg->ang[0]==jaw,"original jaw-to-tooth/tongue parent rotation");
    memcpy(once,fm->tangdst->vlist,bytes);NJS_CNK_OBJECT eyes[9];for(unsigned i=0;i<9;++i)if(fm->eyesrc[i])eyes[i]=*fm->eyesrc[i];
    check(!rx_face_mesh_animate(mesh,p)&&!memcmp(once,fm->tangdst->vlist,bytes)&&!memcmp(body,fm->dst->vlist,body_bytes),"repeated full facial frame drifted");
    for(unsigned i=0;i<9;++i)if(fm->eyesrc[i])check(!memcmp(&eyes[i],fm->eyesrc[i],sizeof(eyes[i])),"repeated eye pose drifted");
    frames++;free(body);free(expected);free(once);
}
void rx_check_face_aux(RxEngineRoom *room,unsigned index,const unsigned char *rdt,size_t rdt_bytes,const unsigned char *backing)
{
    RxFaceMesh *mesh=rx_engine_enemy_face_mesh(room,index);if(!mesh)return;
    const ML_WORK *live=&rx_engine_enemy_model(room,index,0)->model;const MASK_WORK *fm=rx_face_mesh_work(mesh);
    size_t native_bytes=live->obj_num*sizeof(NJS_CNK_OBJECT);NJS_CNK_OBJECT *live_copy=malloc(native_bytes);check(live_copy!=NULL,"source hierarchy snapshot");memcpy(live_copy,live->objP,native_bytes);
    PARAM_WORK p={.eye={0,0,-6.2f},.tangz=1};check(rx_face_mesh_animate(mesh,&p)!=NULL,"uninitialized auxiliaries accepted");
    NJS_MATRIX *old_ptr=pNaMatMatrixStuckPtr,*old_top=pNaMatMatrixStuckTop;
    int old_cnt=lNaMatMatrixStuckCnt,old_max=lNaMatMatrixStuckMax,old_unit=lNaMatIsUnitMatrix;VuState old_vu=g_vu0;
    static NJS_MATRIX stack[128];_Make_SinTable();njInitMatrix(stack,128,0);njUnitMatrix(NULL);njTranslate(NULL,3,4,5);
    NJS_MATRIX stack_before;memcpy(&stack_before,stack,sizeof(stack_before));
    check(!rx_face_mesh_aux_init(mesh),"private original tongue/tooth/eye initialization");
    check(pNaMatMatrixStuckPtr==stack&&lNaMatMatrixStuckCnt==0&&!memcmp(&stack_before,stack,sizeof(stack_before)),"auxiliary initialization changed caller matrix stack");
    const NJS_CNK_OBJECT *root=rx_face_mesh_root(mesh);unsigned eyes=0;
    for(unsigned i=0;i<9;++i)if(fm->head->eyeId[i]!=-1){const NJS_CNK_OBJECT *original=find(live->objP,fm->head->eyeId[i]);
        check(original&&fm->eyesrc[i]==root+(original-live->objP)&&fm->eyesrc[i]->child==fm->eye+i&&fm->eye[i].child==NULL,"native eye ordinal/hierarchy binding");
        check(!memcmp(fm->eyepos[i],original->pos,12)&&!memcmp(fm->eyeang[i],original->ang,12)&&fm->eye[i].model==original->model,"original eye transform/model preservation");
        Matrix identity_matrix,reference;identity(identity_matrix);check(world(root,fm->eyesrc[i],identity_matrix,reference,0),"independent eye hierarchy traversal");
        for(unsigned k=0;k<16;++k)check(fabs(((const float *)&fm->eyemat[i])[k]-reference[k])<0.000001,"eye bind matrix differs from independent hierarchy reference");eyes++;
    }
    const NJS_CNK_OBJECT *tongue=find(live->objP,fm->head->tangId),*tooth=find(live->objP,fm->head->toothId);
    size_t tongue_bytes=rx_face_mesh_tongue_bytes(mesh);check(tongue&&tooth&&eyes==2&&tongue_bytes==804,"opening auxiliary asset reference");
    check(fm->tangorg==root+(tongue-live->objP)&&fm->toothsrc==root+(tooth-live->objP)&&
          fm->tangorg->child==&fm->tangobj&&fm->toothsrc->child==&fm->tooth,"original tongue/tooth hierarchy insertion");
    const NJS_CNK_OBJECT *seen[512],*pending[512],*walk=root;unsigned visited=0,depth=0;
    while(walk){check(visited<512&&depth<512,"private hierarchy exceeds bounded traversal");
        for(unsigned i=0;i<visited;++i)check(seen[i]!=walk,"private hierarchy has duplicate/cyclic links");
        seen[visited++]=walk;if(walk->sibling)pending[depth++]=walk->sibling;
        walk=walk->child?walk->child:(depth?pending[--depth]:NULL);
    }
    check(visited==live->obj_num+eyes+2,"private hierarchy lost original or inserted nodes");
    check((uintptr_t)fm->tangsrc->vlist>UINT32_MAX&&fm->tangsrc->vlist!=tongue->model->vlist&&
          fm->tangsrc->vlist!=fm->tangdst->vlist&&!memcmp(fm->tangsrc->vlist,tongue->model->vlist,tongue_bytes),"bounded owned native tongue backing");
    const float xs[]={-30,0,30},ys[]={-20,0,20},zs[]={-1,0,0.5f,1,1.25f};
    for(unsigned x=0;x<3;++x)for(unsigned y=0;y<3;++y)for(unsigned z=0;z<5;++z){
        p=(PARAM_WORK){.jawang=(float)((int)x*12-12),.jawtrans=0.015f,.eye={(float)((int)y-1),0.3f,z%2?6.2f:-6.2f},.tangx=xs[x],.tangy=ys[y],.tangz=zs[z]};
        p.muscle[4]=0.4f;p.muscle[9]=-0.25f;frame(mesh,&p);}
    p=(PARAM_WORK){.eye={1.25f,0.3f,0},.tangz=1};frame(mesh,&p);
    unsigned mouth=word(rdt+word(rdt+24)+12);
    for(unsigned i=0;i<20;++i){RxFaceClip clip;check(!rx_face_clip(rdt+mouth,rdt_bytes-mouth,i,&clip),"auxiliary expression view");
        for(unsigned j=0;j<3;++j){check(!rx_face_sample(&clip,rx_engine_room_rom(room)->enep[index].id,clip.keys[clip.count-1].frame*j*0.5f,&p),"auxiliary expression sample");frame(mesh,&p);}}
    unsigned lip_offset=word(rdt+word(rdt+24)+16);
    for(int group=0;group+6<=fm->fnum;group+=6){RxLipClip clip;LIP_WORK lip;check(!rx_lip_clip(rdt+lip_offset,rdt_bytes-lip_offset,0,&clip)&&!rx_lip_begin(&clip,&lip),"auxiliary lip view");
        p=(PARAM_WORK){.eye={0,0,-6.2f},.tangz=1};for(unsigned tick=0;lip.flag&&tick<100;++tick){check(!rx_lip_sample(&clip,fm->face+group,&lip,&p),"auxiliary original lip sample");frame(mesh,&p);}check(!lip.flag,"auxiliary lip failed to end");}
    MASK_WORK saved_work=*fm;size_t body_bytes=rx_face_mesh_bytes(mesh);unsigned char *body=malloc(body_bytes),*tongue_copy=malloc(tongue_bytes);
    check(body&&tongue_copy,"invalid frame snapshot");memcpy(body,fm->dst->vlist,body_bytes);memcpy(tongue_copy,fm->tangdst->vlist,tongue_bytes);
    NJS_CNK_OBJECT *nodes=malloc(native_bytes);check(nodes!=NULL,"private hierarchy snapshot");memcpy(nodes,root,native_bytes);
    for(unsigned bad=0;bad<5;++bad){p=(PARAM_WORK){.eye={0,0,-6.2f},.tangz=1};
        if(bad==0)p.eye=(NJS_POINT3){0,0,0};else if(bad==1)p.eye.x=NAN;else if(bad==2)p.tangx=FLT_MAX;else if(bad==3)p.tangz=INFINITY;else p.jawang=NAN;
        check(rx_face_mesh_animate(mesh,&p)&&!memcmp(body,fm->dst->vlist,body_bytes)&&!memcmp(tongue_copy,fm->tangdst->vlist,tongue_bytes)&&
              !memcmp(nodes,root,native_bytes)&&!memcmp(&saved_work,fm,sizeof(saved_work)),"invalid full facial frame committed partial output");}
    check(rx_face_mesh_aux_init(mesh)&&!memcmp(&saved_work,fm,sizeof(saved_work)),"duplicate auxiliary initialization changed hierarchy");
    size_t mask_bytes;const unsigned char *mask=rx_engine_enemy_mask(room,index,&mask_bytes);size_t offset=(size_t)(mask-backing);
    unsigned char *scratch=malloc(rdt_bytes);check(scratch!=NULL,"invalid room allocation");
    /* Turn the authored root around to exercise the original pitch-sign branch
       for an eye whose transformed forward vector points toward positive Z. */
    memcpy(scratch,rdt,rdt_bytes);size_t mdl=(size_t)((const unsigned char *)live->datP-backing);
    unsigned short header;memcpy(&header,rdt+mdl+4,2);size_t yaw=mdl+header+word(rdt+mdl+12)+24;
    unsigned reversed=word(scratch+yaw)^32768u;memcpy(scratch+yaw,&reversed,4);
    RxEngineRoom *rotated=NULL;check(!rx_engine_room_load(scratch,rdt_bytes,&rotated),"reversed-eye room fixture");
    RxFaceMesh *turned=rx_engine_enemy_face_mesh(rotated,index);check(!rx_face_mesh_aux_init(turned),"reversed-eye initialization");
    const MASK_WORK *turned_work=rx_face_mesh_work(turned);const float *eye_matrix=(const float *)&turned_work->eyemat[0];
    check(-100*eye_matrix[10]+eye_matrix[14]>0,"reversed eye must exercise positive-Z pitch branch");
    p=(PARAM_WORK){.eye={1,0.5f,-6.2f},.tangx=15,.tangy=-10,.tangz=1};frame(turned,&p);p.eye.z=6.2f;frame(turned,&p);
    rx_engine_room_destroy(rotated);
    for(unsigned bad=0;bad<4;++bad){memcpy(scratch,rdt,rdt_bytes);if(bad==0)scratch[offset+53]=127;
        else if(bad==1)scratch[offset+55]=scratch[offset+53];else if(bad==2)scratch[offset+55]=127;
        else scratch[(const unsigned char *)tongue->model->vlist-backing]=34;
        RxEngineRoom *active=room;check(rx_engine_room_load(scratch,rdt_bytes,&active)&&active==room,"invalid auxiliary data replaced owned room");}
    check(!memcmp(live_copy,live->objP,native_bytes)&&!memcmp(fm->tangsrc->vlist,tongue->model->vlist,tongue_bytes),"private facial work changed live hierarchy/source vertices");
    printf("PASS native face auxiliaries actor %u: 2 original eyes, bounded tongue/tooth hierarchy, %u original full frames/%u tongue position channels, max error %.9g; independent matrices/eye angles, lip/expression chain, immutable live assets, exact repeats and invalid-frame/room rollback; actor context detached\n",index,frames,channels,maximum);
    pNaMatMatrixStuckPtr=old_ptr;pNaMatMatrixStuckTop=old_top;lNaMatMatrixStuckCnt=old_cnt;lNaMatMatrixStuckMax=old_max;lNaMatIsUnitMatrix=old_unit;g_vu0=old_vu;
    free(scratch);free(body);free(tongue_copy);free(nodes);free(live_copy);
}
