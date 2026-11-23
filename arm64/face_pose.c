#include "face_pose.h"
#include "Motion.h"
#include "MdlPut.h"
#include "ps2_NaMatrix.h"
#include "ps2/vu0.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern void rx_face_flatten_into(NJS_CNK_OBJECT*,NJS_CNK_OBJECT*);
extern int lNaMatMatrixStuckCnt,lNaMatMatrixStuckMax;
extern NJS_MATRIX *pNaMatMatrixStuckPtr;
struct RxFacePose {
    RxFaceMesh *mesh;
    ML_WORK model;
    BH_PWORK actor;
    NJS_CNK_MODEL body;
    NJS_CNK_OBJECT objects[31];
    O_WORK matrices[31];
    NJS_MATRIX partial[31], full[31];
    float origin[3];
    const NJS_CNK_MODEL *owned_models[31];
    const short *body_polygons;
    unsigned *weights;
    unsigned char *vertices;
    size_t vertex_count;
    RxDrawSpan original;
    NJS_MATRIX draw_matrices[31];
    int sampled;
};
static unsigned word(const void *p){unsigned n;memcpy(&n,p,4);return n;}
void rx_face_pose_destroy(RxFacePose *p){if(p){free(p->weights);free(p->vertices);free(p);}}
const char *rx_face_pose_create(RxFaceMesh *mesh,const RxEngineModelPart *part,RxFacePose **out)
{
    if(!mesh||!part||!part->skin||part->skin_bytes%4||!part->model.datP||!part->model_bytes||
       part->model_bytes>UINTPTR_MAX-(uintptr_t)part->model.datP||!out||*out)return "Invalid native facial pose source";
    const MASK_WORK *fm=rx_face_mesh_work(mesh);const NJS_CNK_OBJECT *root=rx_face_mesh_root(mesh),*node=root,*stack[31],*order[31];
    unsigned count=0,depth=0;
    while(node){if(count==31||depth==31)return "Unsupported native facial skeleton extent";
        for(unsigned i=0;i<count;++i)if(order[i]==node)return "Cyclic native facial skeleton";
        order[count++]=node;if(node->sibling)stack[depth++]=node->sibling;
        node=node->child?node->child:(depth?stack[--depth]:NULL);
    }
    if(count!=31||part->model.obj_num!=27||fm->toothsrc!=order[7]||fm->tangorg!=order[11]||
       fm->eyesrc[0]!=order[9]||fm->eyesrc[1]!=order[13]||order[1]->model!=fm->dst)
        return "Unsupported original facial skeleton mapping";
    unsigned records[27]={0};const unsigned char *s=(const unsigned char *)part->skin;size_t at=0;
    const unsigned char *weights=NULL;unsigned owner=UINT_MAX,vertices=0,skin_group=UINT_MAX;
    for(unsigned i=0;i<27;++i){if(part->skin_bytes-at<4)return "Truncated facial skin record";
        unsigned flags=word(s+at);at+=4;records[i]=flags;
        if(flags&~3u)return "Unsupported facial skin flags";
        if(flags){if(part->skin_bytes-at<4||word(s+at)>=128)return "Unsupported facial skin group";
            unsigned group=word(s+at);at+=4;
            if(flags&2){if(owner!=UINT_MAX||i!=1||flags!=3||part->skin_bytes-at<4)return "Invalid facial skin owner";
                owner=i;skin_group=group;vertices=word(s+at);at+=4;
                if(vertices!=fm->head->nAttr||vertices>(part->skin_bytes-at)/4)return "Facial skin weights exceed packet";
                weights=s+at;at+=(size_t)vertices*4;}
            if(group!=skin_group)return "Facial skin joint references a different group";
        }
        if(i>=6&&i<=10&&flags)return "Facial skin records conflict with inserted nodes";
    }
    if(!weights||part->skin_bytes-at!=4||word(s+at)!=UINT_MAX)return "Missing facial skin terminator";
    for(unsigned i=0;i<vertices;++i){unsigned w=word(weights+i*4),joint=w>>24;
        if(joint>=27||!(records[joint]&1)||((w>>16)&255)>=10)return "Invalid original facial skin weight";}
    RxFacePose *p=calloc(1,sizeof(*p));if(!p)return "Cannot allocate native facial pose";
    p->weights=malloc((size_t)vertices*4);p->vertices=malloc(rx_face_mesh_bytes(mesh));
    if(!p->weights||!p->vertices){rx_face_pose_destroy(p);return "Cannot allocate facial skin output";}
    p->mesh=mesh;p->vertex_count=vertices;memcpy(p->weights,weights,(size_t)vertices*4);
    memcpy(p->vertices,fm->dst->vlist,rx_face_mesh_bytes(mesh));
    rx_face_flatten_into((NJS_CNK_OBJECT *)root,p->objects);
    for(unsigned i=6;i<=14;++i)p->matrices[i].flg|=4;
    p->model=part->model;p->model.obj_num=31;p->model.objP=p->objects;p->model.owP=p->matrices;
    p->body=*fm->dst;p->body.vlist=(int *)p->vertices;p->objects[1].model=&p->body;
    p->body_polygons=p->body.plist;
    p->original=(RxDrawSpan){part->model.datP,part->model_bytes};
    for(unsigned i=0;i<31;++i)p->owned_models[i]=p->objects[i].model;
    p->actor.mlwP=&p->model;*out=p;return NULL;
}
const ML_WORK *rx_face_pose_model(const RxFacePose *p){return p?&p->model:NULL;}
const RxFaceMesh *rx_face_pose_mesh(const RxFacePose *p){return p?p->mesh:NULL;}
const BH_PWORK *rx_face_pose_actor(const RxFacePose *p){return p?&p->actor:NULL;}
const unsigned *rx_face_pose_weights(const RxFacePose *p,size_t *count)
{if(count)*count=p?p->vertex_count:0;return p?p->weights:NULL;}
static void transform(const float *matrix,const float *input,float *out,int point)
{
    vu_load_vf(4,input);for(int k=0;k<4;++k)vu_load_vf(5+k,matrix+k*4);
    vu_upper(U_MULA,14,0,5,4,BC_X);vu_upper(U_MADDA,14,0,6,4,BC_Y);
    if(point){vu_upper(U_MADDA,14,0,7,4,BC_Z);vu_upper(U_MADD,14,9,8,0,BC_W);}
    else vu_upper(U_MADD,14,9,7,4,BC_Z);
    float result[4];vu_store_vf(result,9);memcpy(out,result,12);
}
static void skin_current(RxFacePose *p,const float origin[3])
{
    const MASK_WORK *fm=rx_face_mesh_work(p->mesh);
    for(unsigned i=0;i<31;++i){memcpy(p->full[i],p->matrices[i].mtx,sizeof(NJS_MATRIX));
        /* Original npInitCalcSkinFM subtracts actor translation before skinning;
           EasyMultiDrawTreeCnk adds it back only for the skinned owner. */
        for(unsigned k=0;k<3;++k)p->full[i][12+k]=vu_clampf(p->full[i][12+k]-origin[k]);
        memcpy(p->partial[i],p->full[i],sizeof(NJS_MATRIX));
        njRotXYZ(&p->partial[i],-p->objects[i].ang[0],-p->objects[i].ang[1],-p->objects[i].ang[2]);}
    const unsigned char *rest=(const unsigned char *)fm->dst->vlist+64;
    memcpy(p->vertices,fm->dst->vlist,rx_face_mesh_bytes(p->mesh));
    static const float levels[]={0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1};
    for(size_t i=0;i<p->vertex_count;++i){unsigned encoded=p->weights[i],joint=encoded>>24;if(joint>10)joint+=4;
        float weight=levels[(encoded>>16)&255];
        for(unsigned normal=0;normal<2;++normal){float full[3],base[3];
            transform(p->full[joint],(const float *)(rest+i*32+normal*16),full,!normal);
            if(weight!=1){transform(p->partial[joint],(const float *)(rest+i*32+normal*16),base,!normal);
                for(unsigned k=0;k<3;++k)full[k]=vu_clampf(vu_clampf(vu_clampf(full[k]-base[k])*weight)+base[k]);}
            memcpy(p->vertices+64+i*32+normal*16,full,12);
        }
    }
    memcpy(p->origin,origin,sizeof(p->origin));p->sampled=1;
}
const char *rx_face_pose_skin_check(const RxFacePose *p,const BH_PWORK *actor)
{
    if(!p||!actor||(actor->mlwP!=&p->model&&actor->mlwP!=actor->mdl)||actor->mlwP->objP!=p->objects||actor->mlwP->owP!=p->matrices||actor->mlwP->obj_num!=31||
       p->body.vlist!=(int*)p->vertices||p->body.plist!=p->body_polygons||g_vu0.vf[0].f[3]!=1||!pNaMatMatrixStuckPtr)
        return "Foreign original actor skin/model binding";
    const float origin[]={actor->px,actor->py,actor->pz};
    for(unsigned k=0;k<3;++k)if(!isfinite(origin[k])||fabsf(origin[k])>1000000)return "Invalid original actor skin origin";
    for(unsigned i=0;i<31;++i){
        if(p->objects[i].model!=p->owned_models[i])return "Foreign original actor skin packet";
        for(unsigned k=0;k<3;++k)if(p->objects[i].ang[k]==INT_MIN)return "Invalid original actor skin inverse rotation";
        for(unsigned k=0;k<16;++k)if(!isfinite(p->matrices[i].mtx[k])||fabsf(p->matrices[i].mtx[k])>100000000)
            return "Invalid original actor skin world matrix";
    }
    const float *rest=(const float*)rx_face_mesh_work(p->mesh)->dst->vlist+16;
    for(size_t i=0;i<p->vertex_count;++i){unsigned w=p->weights[i];if((w>>24)>=27||((w>>16)&255)>=10)return "Invalid original actor skin weight";
        for(unsigned n=0;n<2;++n)for(unsigned k=0;k<3;++k)if(!isfinite(rest[i*8+n*4+k])||fabsf(rest[i*8+n*4+k])>1000000)
            return "Invalid original actor skin source vertex";}
    return NULL;
}
const char *rx_face_pose_skin(RxFacePose *p,const BH_PWORK *actor)
{
    const char *error=rx_face_pose_skin_check(p,actor);if(error)return error;
    float origin[]={actor->px,actor->py,actor->pz};skin_current(p,origin);return NULL;
}
const char *rx_face_pose_sample(RxFacePose *p,MN_WORK *bank,size_t slots,unsigned motion,int frame,int step,const PARAM_WORK *expression)
{
    if(!p||!bank||motion>=slots||slots>512||!expression||!pNaMatMatrixStuckPtr||
       lNaMatMatrixStuckMax-lNaMatMatrixStuckCnt<32||g_vu0.vf[0].f[3]!=1)return "Invalid native facial pose binding";
    if(p->model.objP!=p->objects||p->model.owP!=p->matrices||p->model.obj_num!=31||p->actor.mlwP!=&p->model)
        return "Foreign native facial model binding";
    for(unsigned i=0;i<31;++i)if(p->matrices[i].flg!=(i>=6&&i<=14?4u:0u))
        return "Unsupported facial motion-protection flags";
    const MN_WORK *m=bank+motion;
    if(!m->datP||!m->md2P||m->obj_num!=22||!m->frm_num||m->frm_num>=32768||frame<0||
       (unsigned)frame/65536>=m->frm_num)return "Unsupported original facial body motion";
    int64_t period=(int64_t)m->frm_num*65536,advanced=(int64_t)frame+step;
    if(step>period||step< -period||advanced>INT_MAX||advanced<INT_MIN)return "Facial motion step exceeds original wrap range";
    if(!(m->flg&0x30)&&!m->md2P[0].p[0])return "Fast facial motion lacks root translation";
    unsigned current=(unsigned)frame/65536,next=current+1<m->frm_num?current+1:0;
    for(unsigned i=0;i<22;++i)if(m->md2P[i].p[0])for(unsigned k=0;k<3;++k){
        const float *keys=m->md2P[i].p[0];
        if(!isfinite(keys[current*3+k])||((m->flg&0x20)&&!isfinite(keys[next*3+k])))return "Nonfinite facial motion key";
    }
    const char *error=rx_face_mesh_animate(p->mesh,expression);if(error)return error;
    const MASK_WORK *fm=rx_face_mesh_work(p->mesh);
    p->objects[7].ang[0]=fm->toothsrc->ang[0];p->objects[11].ang[0]=fm->tangorg->ang[0];
    for(unsigned i=0;i<2;++i){p->objects[9+i*4].ang[0]=-fm->eyesrc[i]->ang[0];p->objects[9+i*4].ang[1]=-fm->eyesrc[i]->ang[1];}
    p->actor.mnwP=bank;p->actor.mtn_no=(int)motion;p->actor.frm_no=frame;p->actor.mtn_add=step;
    bhSetMotion(&p->actor,step,0,NULL);bhCalcTree(NULL,&p->model);
    const float origin[3]={0};skin_current(p,origin);
    p->sampled=1;return NULL;
}
const char *rx_face_pose_parts(RxFacePose *p,const NJS_MATRIX *base,RxRoomDrawPart parts[31],size_t *count)
{
    if(!p||!p->sampled||!base||!parts||!count)return "Invalid native facial draw binding";
    for(unsigned k=0;k<16;++k)if(!isfinite(((const float *)base)[k]))return "Nonfinite facial view matrix";
    NJS_MATRIX input;memcpy(&input,base,sizeof(input));
    const MASK_WORK *fm=rx_face_mesh_work(p->mesh);size_t n=0;
    for(unsigned i=0;i<31;++i){const NJS_CNK_OBJECT *node=p->objects+i;
        if(!node->model||(node->evalflags&8))continue;
        njSetMatrix(&p->draw_matrices[i],&input);
        /* Original EasyMultiDrawTreeCnk draws skinned owner packets without
           applying their joint matrix a second time. */
        if(!(node->evalflags&0x80000000u))njMultiMatrix(&p->draw_matrices[i],&p->matrices[i].mtx);
        else njTranslate(&p->draw_matrices[i],p->origin[0],p->origin[1],p->origin[2]);
        RxDrawSpan vertices=p->original;
        if(node->model==&p->body)vertices=(RxDrawSpan){p->vertices,rx_face_mesh_bytes(p->mesh)};
        else if(node->model==fm->tangdst)vertices=(RxDrawSpan){fm->tangdst->vlist,rx_face_mesh_tongue_bytes(p->mesh)};
        parts[n++]=(RxRoomDrawPart){node->model,&p->draw_matrices[i],vertices,p->original};
    }
    *count=n;return NULL;
}
const char *rx_face_pose_draw(RxFacePose *p,const NJS_MATRIX *base,const NJS_CNK_VIEW *view,RxRoomSubmit submit,void *context)
{
    RxRoomDrawPart parts[31];size_t count=0;
    const char *error=rx_face_pose_parts(p,base,parts,&count);if(error)return error;
    return rx_draw_chunk_parts(parts,count,p->model.texP,1,view,submit,context);
}
