#include "face_mesh.h"
#include "face_bh.h"
#include "ps2_NaMath.h"
#include "ps2_NaMatrix.h"
#include "ps2/vu0.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct RxFaceMesh {
    MASK_WORK work;
    NJS_CNK_MODEL source,destination;
    unsigned char *mask,*vertices,*output;
    const NJS_POINT3 **neighbors;
    size_t neighbor_count,vertex_bytes;
    int jaw_ready;
    NJS_CNK_OBJECT *nodes;
    unsigned *parents,selected[11];
    size_t node_count,tongue_bytes;
    unsigned char *tongue_vertices,*tongue_output;
    NJS_CNK_MODEL tongue_source,tongue_destination;
    int aux_ready;
};
_Static_assert(offsetof(RxFaceMesh,work)==0,"Native eye initializer requires first-member work");
_Static_assert(sizeof(HEAD_WORK)==64&&sizeof(VLIST_WORK)==4&&sizeof(LIST_WORK)==20&&sizeof(TANG_WORK)==8,
               "Facial disk scalar layouts differ");
static unsigned word(const void *p){unsigned n;memcpy(&n,p,4);return n;}
static unsigned half(const void *p){unsigned short n;memcpy(&n,p,2);return n;}
static int finite_words(const void *p,size_t count)
{for(size_t i=0;i<count;++i){float f;memcpy(&f,(const char *)p+i*4,4);if(!isfinite(f))return 0;}return 1;}
static int consume(size_t *offset,size_t count,size_t stride,size_t bytes)
{if(*offset>bytes||count>(bytes-*offset)/stride)return 0;*offset+=count*stride;return 1;}
void rx_face_mesh_destroy(RxFaceMesh *mesh)
{
    if(!mesh)return;
    free(mesh->nodes);free(mesh->parents);free(mesh->tongue_vertices);free(mesh->tongue_output);
    free(mesh->neighbors);free(mesh->output);free(mesh->vertices);free(mesh->mask);free(mesh);
}
const char *rx_face_mesh_create(const void *mask,size_t bytes,const RxChunk *chunk,RxFaceMesh **out)
{
    if(!mask||!chunk||!chunk->vertices||!out||*out||bytes<64||memcmp(mask,"MASK",4)||half((const char *)mask+4)!=1)
        return "Invalid native facial mesh source";
    HEAD_WORK h;memcpy(&h,mask,64);
    size_t o=64,lists=o;
    if(!h.nAttr||!h.nFace||!consume(&o,h.nList,4,bytes))return "Facial descriptors exceed MASK";
    const unsigned char *p=mask;size_t neighbors=0,connections=0;
    for(unsigned i=0;i<h.nList;++i){VLIST_WORK v;memcpy(&v,p+lists+i*4,4);
        if(v.id<0||(unsigned)v.id>=h.nAttr||v.mnum<0||v.mnum>32||v.nvnum<0||v.nvnum%2)
            return "Invalid facial vertex descriptor";
        neighbors+=(unsigned)v.nvnum;connections+=(unsigned)v.mnum;
    }
    size_t indices=o;
    if(connections!=h.nConnect||!consume(&o,neighbors,4,bytes))return "Facial neighbor/connection count differs";
    size_t con=o;if(!consume(&o,h.nConnect,20,bytes))return "Facial connections exceed MASK";
    size_t jaw=o;if(!consume(&o,h.nJaw,8,bytes))return "Facial jaw records exceed MASK";
    size_t tang=o;if(!consume(&o,h.nTang,8,bytes))return "Facial tongue records exceed MASK";
    size_t face=o;if(!consume(&o,h.nFace,164,bytes)||o!=bytes)return "Facial parameters exceed MASK";
    /* This original muscle body uses the PS2 skinned packet's 16-word header
       and eight-word vertex stride. Other chunk types need their own port. */
    const unsigned char *v=chunk->vertices;
    if(chunk->vertex_extent<68||half(v)!=51||half(v+4)!=0||half(v+6)!=h.nAttr)
        return "Native facial muscles require a matching type-51 mesh";
    size_t vertex_bytes=64;
    if(!consume(&vertex_bytes,h.nAttr,32,chunk->vertex_extent)||
       (size_t)(half(v+2)+1)*4!=vertex_bytes||chunk->vertex_extent-vertex_bytes<4||half(v+vertex_bytes)!=255)
        return "Facial vertex packet exceeds model";
    vertex_bytes+=4; /* bounded terminator, never the original magic 128-word tail */
    if(h.jawId[0]>=h.nAttr||h.jawId[1]>=h.nAttr)return "Facial jaw pivot exceeds mesh";
    for(size_t i=0;i<neighbors;++i)if(word(p+indices+i*4)>=h.nAttr)return "Facial neighbor exceeds mesh";
    for(unsigned i=0;i<h.nConnect;++i){LIST_WORK c;memcpy(&c,p+con+i*20,20);
        if(c.id<0||c.id>=32||!finite_words(&c.vec,4)||c.scal<=0)return "Invalid facial muscle connection";}
    for(unsigned i=0;i<h.nJaw;++i){TANG_WORK t;memcpy(&t,p+jaw+i*8,8);
        if(t.id<0||(unsigned)t.id>=h.nAttr||!isfinite(t.rate))return "Invalid facial jaw weight";}
    /* Tongue vertex bounds require its separate mesh; retain scalar records
       but do not claim that deformation stage has been initialized. */
    for(unsigned i=0;i<h.nTang;++i){TANG_WORK t;memcpy(&t,p+tang+i*8,8);
        if(t.id<0||!isfinite(t.rate))return "Invalid facial tongue weight";}
    for(unsigned i=0;i<h.nFace;++i) {
        const unsigned char *key=p+face+i*164;
        /* Lip parameters carry signed integer bits in jawang/tangx/tangy. */
        if(!finite_words(key,32)||!finite_words(key+132,4)||!finite_words(key+156,1))
            return "Nonfinite facial base parameter";
    }
    for(unsigned i=0;i<h.nAttr;++i)if(!finite_words(v+64+i*32,3))return "Nonfinite facial bind vertex";
    RxFaceMesh *m=calloc(1,sizeof(*m));if(!m)return "Cannot allocate facial mesh work";
    m->mask=malloc(bytes);m->vertices=malloc(vertex_bytes);m->output=malloc(vertex_bytes);
    if(neighbors)m->neighbors=calloc(neighbors,sizeof(*m->neighbors));
    if(!m->mask||!m->vertices||!m->output||(neighbors&&!m->neighbors)){
        rx_face_mesh_destroy(m);return "Cannot allocate native facial backing";}
    memcpy(m->mask,mask,bytes);memcpy(m->vertices,v,vertex_bytes);memcpy(m->output,v,vertex_bytes);
    m->vertex_bytes=vertex_bytes;m->neighbor_count=neighbors;
    m->source.vlist=(int *)m->vertices;m->destination.vlist=(int *)m->output;
    m->work=(MASK_WORK){.src=&m->source,.dst=&m->destination,.head=(HEAD_WORK *)m->mask,
        .vlist=(VLIST_WORK *)(m->mask+lists),.list=(LIST_WORK *)(m->mask+con),.jaw=(TANG_WORK *)(m->mask+jaw),
        .tang=(TANG_WORK *)(m->mask+tang),.face=(PARAM_WORK *)(m->mask+face),
        .lnum=(int)h.nList,.jnum=(int)h.nJaw,.tnum=(int)h.nTang,.fnum=(int)h.nFace,
        .vtype=51,.vtop=16,.ntop=20,.vofs=8,.mode=1};
    for(size_t i=0;i<neighbors;++i)m->neighbors[i]=(const NJS_POINT3 *)(m->output+64+word(p+indices+i*4)*32);
    *out=m;return NULL;
}
const MASK_WORK *rx_face_mesh_work(const RxFaceMesh *mesh){return mesh?&mesh->work:NULL;}
size_t rx_face_mesh_neighbors(const RxFaceMesh *mesh,const NJS_POINT3 *const **out)
{if(out)*out=mesh?mesh->neighbors:NULL;return mesh?mesh->neighbor_count:0;}
size_t rx_face_mesh_bytes(const RxFaceMesh *mesh){return mesh?mesh->vertex_bytes:0;}
static const char *muscles_check(const RxFaceMesh *mesh,const PARAM_WORK *param)
{
    if(!mesh||!param||!finite_words(param->muscle,32))return "Invalid native facial muscle sample";
    /* Validate all products/sums before touching the destination. */
    const LIST_WORK *c=mesh->work.list;
    for(int i=0;i<mesh->work.lnum;++i){float sum=0;int active=0;
        for(int j=0;j<mesh->work.vlist[i].mnum;++j,++c){float x=param->muscle[c->id];
            active|=x!=0;
            float rate=c->scal*fabsf(x);sum+=rate;
            if(!isfinite(sum)||!isfinite(x*rate))return "Facial muscle weight exceeds native range";}
        if(active&&(sum<=0||!isfinite(1.0f/sum)))return "Facial muscle weight underflows native range";
    }
    return NULL;
}
const char *rx_face_mesh_muscles(RxFaceMesh *mesh,const PARAM_WORK *param)
{
    const char *error=muscles_check(mesh,param);if(error)return error;
    mesh->work.param=*param;_fmCnkCalcMuscle(&mesh->work);return NULL;
}
const char *rx_face_mesh_jaw_init(RxFaceMesh *mesh)
{
    if(!mesh||njCos(0)<0.99f||g_vu0.vf[0].f[3]!=1)return "Jaw setup requires initialized original trig/VU state";
    const float *v=(const float *)mesh->work.src->vlist+16;
    const float *a=v+mesh->work.head->jawId[0]*8,*b=v+mesh->work.head->jawId[1]*8;
    float x=b[0]-a[0],y=b[1]-a[1],z=b[2]-a[2],length=x*x+y*y;
    if(!isfinite(x)||!isfinite(y)||!isfinite(z)||!isfinite(length)||length<=0)
        return "Degenerate native jaw pivot axis";
    MASK_WORK temporary=mesh->work;temporary.toothsrc=NULL;temporary.tangorg=NULL;
    _fmCnkSetJaw(&temporary,temporary.head->jawId[0],temporary.head->jawId[1]);
    if(!finite_words(&temporary.jmat1,16)||!finite_words(&temporary.jmat2,16))return "Invalid native jaw matrices";
    memcpy(&mesh->work.jmat1,&temporary.jmat1,sizeof(NJS_MATRIX));
    memcpy(&mesh->work.jmat2,&temporary.jmat2,sizeof(NJS_MATRIX));mesh->jaw_ready=1;return NULL;
}
static const char *deform_check(const RxFaceMesh *mesh,const PARAM_WORK *param)
{
    if(!mesh||!mesh->jaw_ready||!param||!isfinite(param->jawang)||!isfinite(param->jawtrans))
        return "Invalid or uninitialized native jaw sample";
    float angle=-param->jawang*182.04445f;
    if(!isfinite(angle)||fabsf(angle)>(float)(INT_MAX-1024))return "Jaw angle exceeds original integer range";
    for(int i=0;i<mesh->work.jnum;++i){float weighted=182.04445f*(-param->jawang*mesh->work.jaw[i].rate);
        if(!isfinite(weighted)||fabsf(weighted)>(float)(INT_MAX-1024))return "Weighted jaw angle exceeds original integer range";}
    return muscles_check(mesh,param);
}
const char *rx_face_mesh_deform(RxFaceMesh *mesh,const PARAM_WORK *param)
{
    const char *error=deform_check(mesh,param);if(error)return error;
    mesh->work.param=*param;_fmCnkCalcMuscle(&mesh->work);
    _fmCnkCalcJaw(&mesh->work);return NULL;
}
static unsigned node_index(const RxModel *model,const RxNode *node)
{
    uintptr_t base=(uintptr_t)model->nodes,p=(uintptr_t)node;
    return node&&p>=base&&(p-base)%sizeof(*node)==0&&(p-base)/sizeof(*node)<model->node_count?
        (unsigned)((p-base)/sizeof(*node)):UINT_MAX;
}
static unsigned mesh_node(const RxModel *model,int id)
{
    if(id<0)return UINT_MAX;
    const RxNode *node=model->nodes,*stack[4096];unsigned depth=0,ordinal=0,visited=0;
    while(node&&visited++<model->node_count){
        if(node->model&&ordinal++==(unsigned)id)return node_index(model,node);
        if(node->sibling){if(depth==4096)return UINT_MAX;stack[depth++]=node->sibling;}
        node=node->child?node->child:(depth?stack[--depth]:NULL);
    }
    return UINT_MAX;
}
const char *rx_face_mesh_aux_prepare(RxFaceMesh *m,const RxModel *src,const ML_WORK *native)
{
    if(!m||m->nodes||!src||!src->nodes||!src->node_count||src->node_count>4096||
       !native||!native->objP||native->obj_num!=src->node_count)return "Invalid facial hierarchy source";
    for(unsigned i=0;i<src->node_count;++i){
        const RxNode *n=src->nodes+i;
        if((n->child&&(node_index(src,n->child)==UINT_MAX||node_index(src,n->child)<=i))||
           (n->sibling&&(node_index(src,n->sibling)==UINT_MAX||node_index(src,n->sibling)<=i))||
           !finite_words(native->objP[i].pos,3)||!finite_words(native->objP[i].scl,3))return "Invalid native facial hierarchy";
    }
    unsigned chosen[12];int ids[12]={m->work.head->faceId,m->work.head->tangId,m->work.head->toothId};
    for(unsigned i=0;i<9;++i)ids[3+i]=m->work.head->eyeId[i];
    for(unsigned i=0;i<12;++i){
        chosen[i]=mesh_node(src,ids[i]);
        if(ids[i]!=-1&&chosen[i]==UINT_MAX)return "Facial mesh ordinal outside hierarchy";
        for(unsigned j=0;j<i;++j)if(chosen[i]!=UINT_MAX&&chosen[i]==chosen[j])return "Facial hierarchy reuses an auxiliary object";
        if(chosen[i]!=UINT_MAX&&!native->objP[chosen[i]].model)return "Missing native facial object model";
    }
    if(chosen[0]==UINT_MAX)return "Missing facial body object";
    size_t tongue_bytes=0;
    if(chosen[1]!=UINT_MAX){
        const RxChunk *chunk=src->nodes[chosen[1]].model;const unsigned char *v=chunk->vertices;
        if(!v||chunk->vertex_extent<68||half(v)!=51||half(v+4))return "Tongue requires a type-51 packet";
        tongue_bytes=64;
        if(!consume(&tongue_bytes,half(v+6),32,chunk->vertex_extent)||
           (size_t)(half(v+2)+1)*4!=tongue_bytes||chunk->vertex_extent-tongue_bytes<4||half(v+tongue_bytes)!=255)
            return "Tongue vertex packet exceeds model";
        for(int i=0;i<m->work.tnum;++i)if((unsigned)m->work.tang[i].id>=half(v+6))return "Tongue weight exceeds mesh";
        for(unsigned i=0;i<half(v+6);++i)if(!finite_words(v+64+i*32,3))return "Nonfinite tongue bind vertex";
        tongue_bytes+=4;
    }else if(m->work.tnum)return "Tongue weights lack their mesh";
    NJS_CNK_OBJECT *nodes=calloc(src->node_count,sizeof(*nodes));unsigned *parents=malloc(src->node_count*sizeof(*parents));
    unsigned char *vertices=tongue_bytes?malloc(tongue_bytes):NULL,*output=tongue_bytes?malloc(tongue_bytes):NULL;
    if(!nodes||!parents||(tongue_bytes&&(!vertices||!output))){free(nodes);free(parents);free(vertices);free(output);return "Cannot allocate native facial hierarchy";}
    for(unsigned i=0;i<src->node_count;++i)parents[i]=UINT_MAX;
    for(unsigned i=0;i<src->node_count;++i){nodes[i]=native->objP[i];const RxNode *n=src->nodes+i;
        unsigned child=node_index(src,n->child),sibling=node_index(src,n->sibling);
        nodes[i].child=child==UINT_MAX?NULL:nodes+child;nodes[i].sibling=sibling==UINT_MAX?NULL:nodes+sibling;
        if(child!=UINT_MAX)parents[child]=i;if(sibling!=UINT_MAX)parents[sibling]=parents[i];
    }
    m->nodes=nodes;m->parents=parents;m->node_count=src->node_count;
    m->tongue_vertices=vertices;m->tongue_output=output;m->tongue_bytes=tongue_bytes;
    memcpy(m->selected,chosen+1,sizeof(m->selected));
    int *body_source=m->source.vlist,*body_output=m->destination.vlist;
    m->source=*nodes[chosen[0]].model;m->destination=m->source;m->source.vlist=body_source;m->destination.vlist=body_output;
    nodes[chosen[0]].model=&m->destination;
    if(tongue_bytes){
        m->tongue_source=*nodes[chosen[1]].model;m->tongue_destination=m->tongue_source;
        memcpy(vertices,m->tongue_source.vlist,tongue_bytes);memcpy(output,vertices,tongue_bytes);
        m->tongue_source.vlist=(int *)vertices;m->tongue_destination.vlist=(int *)output;
        nodes[chosen[1]].model=&m->tongue_source;m->work.tangdst=&m->tongue_destination;
    }
    return NULL;
}
/* Called only by the original eye initializer on this owner's private copy.
   Use the original transform order and explicit matrices, avoiding its legacy
   global-stack traversal (which leaves pending sibling pushes at the target). */
void rx_native_face_eye_matrix(MASK_WORK *fm,unsigned id,NJS_CNK_OBJECT *root)
{
    RxFaceMesh *m=(RxFaceMesh *)fm;(void)root;
    unsigned path[4096],depth=0,index=(unsigned)(fm->eyesrc[id]-m->nodes);
    while(index!=UINT_MAX&&depth<m->node_count){path[depth++]=index;index=m->parents[index];}
    njUnitMatrix(&fm->eyemat[id]);
    while(depth){const NJS_CNK_OBJECT *n=m->nodes+path[--depth];
        njTranslate(&fm->eyemat[id],n->pos[0],n->pos[1],n->pos[2]);
        njRotateXYZ(&fm->eyemat[id],n->ang[0],n->ang[1],n->ang[2]);
        njScale(&fm->eyemat[id],n->scl[0],n->scl[1],n->scl[2]);
    }
}
const char *rx_face_mesh_aux_init(RxFaceMesh *m)
{
    if(!m||!m->nodes||m->aux_ready)return "Invalid native facial auxiliary initialization";
    const char *error=rx_face_mesh_jaw_init(m);if(error)return error;
    if(m->selected[1]!=UINT_MAX)_fmCnkSetToothObject(&m->work,m->nodes+m->selected[1]);
    if(m->selected[0]!=UINT_MAX)_fmCnkSetTangObject(&m->work,m->nodes+m->selected[0]);
    for(unsigned i=0;i<9;++i)if(m->selected[2+i]!=UINT_MAX)
        _fmCnkSetEyeballObject(&m->work,i,m->nodes+m->selected[2+i],m->nodes);
    _fmCnkSetJaw(&m->work,m->work.head->jawId[0],m->work.head->jawId[1]);m->aux_ready=1;return NULL;
}
const NJS_CNK_OBJECT *rx_face_mesh_root(const RxFaceMesh *m){return m?m->nodes:NULL;}
size_t rx_face_mesh_tongue_bytes(const RxFaceMesh *m){return m?m->tongue_bytes:0;}
extern void rx_native_face_eye_vectors(const MASK_WORK*,unsigned,NJS_POINT3*,NJS_POINT3*);
static const char *animate_check(const RxFaceMesh *m,const PARAM_WORK *param)
{
    if(!m||!m->aux_ready||!param||!finite_words(&param->eye,6))return "Invalid native facial auxiliary sample";
    for(int i=0;i<m->work.tnum;++i)for(unsigned axis=0;axis<2;++axis){float a=axis?param->tangy:param->tangx;
        float angle=182.04445f*(a*m->work.tang[i].rate);
        if(!isfinite(angle)||fabsf(angle)>(float)(INT_MAX-1024))return "Tongue angle exceeds original integer range";}
    MASK_WORK temporary=m->work;temporary.param=*param;
    for(unsigned i=0;i<9;++i)if(temporary.eyesrc[i]){NJS_POINT3 q,r;rx_native_face_eye_vectors(&temporary,i,&q,&r);
        float horizontal=q.x*q.x+q.z*q.z;
        if(!finite_words(&q,3)||!finite_words(&r,3)||!isfinite(horizontal)||horizontal<=0)
            return "Undefined native eye target";
    }
    return deform_check(m,param);
}
const char *rx_face_mesh_animate_check(const RxFaceMesh *m,const PARAM_WORK *param)
{
    VuState saved=g_vu0;const char *error=animate_check(m,param);g_vu0=saved;return error;
}
const char *rx_face_mesh_animate(RxFaceMesh *m,const PARAM_WORK *param)
{
    const char *error=rx_face_mesh_animate_check(m,param);if(error)return error;
    error=rx_face_mesh_deform(m,param);if(error)return error;
    _fmCnkCalcTang(&m->work);_fmCnkCalcEye(&m->work);return NULL;
}
