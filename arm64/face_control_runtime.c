#include "face_control_runtime.h"
#include "main.h"
#include "face_mesh.h"
#include "face_pose.h"
#include "face_sampling.h"
#include "ps2/vu0.h"
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
typedef struct {
    int32_t *words;unsigned id;
    RxFaceMesh *mesh;RxFacePose *pose;
    const void *mouth,*lip_bank;size_t mouth_bytes,lip_bytes;
    unsigned char *mask_source;
    MASK_WORK work;HEAD_WORK head;LIP_WORK lip;
    const PARAM_WORK *base,*lip_base;unsigned base_count;
    RxFaceClip expression,active;RxLipClip selected,playing;
    const char *fault;int ready,preview;
} FaceBinding;
static FaceBinding bindings[128];
static int slot(const BH_PWORK *actor)
{
    uintptr_t p=(uintptr_t)actor,start=(uintptr_t)ene;
    return p>=start&&p-start<sizeof(ene)&&(p-start)%sizeof(*ene)==0?(int)((p-start)/sizeof(*ene)):-1;
}
const char *rx_face_control_bind(BH_PWORK *actor,int32_t *words,size_t bytes)
{
    int i=slot(actor);
    if(i<0 || actor->id<=90 || !words || (uintptr_t)words%_Alignof(int32_t) || bytes!=RX_FACE_CONTROL_BYTES)
        return "Invalid native facial control allocation";
    if(bindings[i].ready||(actor->exp0&&actor->exp0!=(unsigned char *)words)||(bindings[i].words&&bindings[i].words!=words))
        return "Native facial control would replace occupied actor work";
    bindings[i].words=words;bindings[i].id=actor->id;actor->exp0=(unsigned char *)words;
    return NULL;
}
const char *rx_face_control_check(const BH_PWORK *actor)
{
    int i=slot(actor);
    if(i<0 || !bindings[i].words || actor->id!=bindings[i].id || actor->exp0!=(unsigned char *)bindings[i].words)
        return "Facial command requires its owned native control work";
    return NULL;
}
void rx_face_control_reset(void)
{
    for(unsigned i=0;i<128;++i){FaceBinding *b=bindings+i;
        if(b->words&&ene[i].exp0==(unsigned char *)b->words)ene[i].exp0=NULL;
        if(b->ready&&ene[i].exp1==(unsigned char *)&b->work)ene[i].exp1=NULL;
        if(b->ready&&ene[i].exp2==(unsigned char *)&b->lip)ene[i].exp2=NULL;
    }
    memset(bindings,0,sizeof(bindings));
}
static FaceBinding *binding(const BH_PWORK *actor)
{int i=slot(actor);return i<0?NULL:bindings+i;}
const char *rx_face_control_ready(const BH_PWORK *actor)
{
    const char *error=rx_face_control_check(actor);if(error)return error;
    FaceBinding *b=binding(actor);
    if(!b->ready||!b->mesh||!b->pose||actor->mskp!=b->mask_source||
       actor->exp1!=(unsigned char *)&b->work||actor->exp2!=(unsigned char *)&b->lip)
        return "Facial command requires its native expression/lip context";
    if(sys->mspp!=b->mouth||sys->lspp!=b->lip_bank)return "Facial context references a retired room bank";
    const MASK_WORK *owned=rx_face_mesh_work(b->mesh);
    if(b->work.src!=owned->src||b->work.dst!=owned->dst||b->work.toothsrc!=owned->toothsrc||b->work.tangorg!=owned->tangorg)
        return "Foreign native facial geometry binding";
    for(unsigned i=0;i<9;++i)if(b->work.eyesrc[i]!=owned->eyesrc[i])return "Foreign native eye binding";
    if(b->work.head!=&b->head||b->work.mode!=1||b->head.nFace!=(unsigned)b->work.fnum||
       !((b->work.face==b->base&&b->work.fnum==(int)b->base_count)||
         (b->active.keys&&b->work.face==b->active.keys&&b->work.fnum==(int)b->active.count)))
        return "Foreign active facial expression binding";
    return NULL;
}
const char *rx_face_control_prepare(BH_PWORK *actor,RxFaceMesh *mesh,RxFacePose *pose,
                                  const void *mouth,size_t mouth_bytes,const void *lip,size_t lip_bytes)
{
    const char *error=rx_face_control_check(actor);if(error)return error;
    FaceBinding *b=binding(actor);RxLipClip initial;
    if(b->ready||!mesh||!pose||!actor->mskp||actor->exp1||actor->exp2)return "Occupied or incomplete native facial context";
    const MASK_WORK *work=rx_face_mesh_work(mesh);const ML_WORK *model=rx_face_pose_model(pose);
    if(!work||!work->head||!work->face||work->fnum<=0||rx_face_pose_mesh(pose)!=mesh||model->obj_num!=31||!model->objP||!model->owP)
        return "Invalid native facial model work";
    if((error=rx_face_bank_validate(mouth,mouth_bytes,0,NULL))||
       (error=rx_face_bank_validate(lip,lip_bytes,1,NULL))||(error=rx_lip_clip(lip,lip_bytes,0,&initial)))return error;
    for(int i=0;i<work->fnum;++i){if(work->face[i].frame<0||(i&&work->face[i].frame<=work->face[i-1].frame))return "Invalid native facial base frame order";}
    for(unsigned i=0;i<RX_FACE_CONTROL_WORDS;++i)if(b->words[i])return "Native face setup requires cleared scalar work";
    b->mesh=mesh;b->pose=pose;b->mouth=mouth;b->mouth_bytes=mouth_bytes;b->lip_bank=lip;b->lip_bytes=lip_bytes;
    b->mask_source=actor->mskp;b->work=*work;b->head=*work->head;b->work.head=&b->head;
    b->base=b->lip_base=work->face;b->base_count=(unsigned)work->fnum;b->selected=initial;b->ready=1;
    actor->exp1=(unsigned char *)&b->work;actor->exp2=(unsigned char *)&b->lip;
    bhInitMask(actor);return NULL;
}
void rx_face_control_initialize(BH_PWORK *actor)
{
    FaceBinding *b=binding(actor);if(!b||!b->ready)return;
    /* Original bhInitMask scalar initialization; pointer/cache words stay
       separate from exp0 and the pre-staged model/matrix allocations. */
    memset(b->words,0,RX_FACE_CONTROL_BYTES);b->words[8]=(int32_t)b->base_count;b->words[1]=1;
    b->work.face=(PARAM_WORK *)b->base;b->work.fnum=(int)b->base_count;b->head.nFace=b->base_count;
    memset(&b->work.param,0,sizeof(b->work.param));b->lip_base=b->base;
    b->expression=b->active=(RxFaceClip){0};b->playing=(RxLipClip){0};
    memset(&b->lip,0,sizeof(b->lip));actor->ko_num=0;
}
const unsigned char *rx_face_control_expression(BH_PWORK *a)
{FaceBinding *b=binding(a);return b&&b->expression.keys?(const unsigned char *)b->expression.keys-8:NULL;}
const PARAM_WORK *rx_face_control_base(BH_PWORK *a){FaceBinding *b=binding(a);return b?b->base:NULL;}
const PARAM_WORK *rx_face_control_lip_base(BH_PWORK *a){FaceBinding *b=binding(a);return b?b->lip_base:NULL;}
const unsigned char *rx_face_control_lip_data(BH_PWORK *a){FaceBinding *b=binding(a);return b?b->selected.data:NULL;}
void rx_face_control_restore_base(BH_PWORK *a){FaceBinding *b=binding(a);if(b)b->lip_base=b->base;}
const char *rx_face_control_request(const BH_PWORK *actor,int expression,unsigned index,unsigned group)
{
    const char *error=rx_face_control_ready(actor);if(error)return error;FaceBinding *b=binding(actor);
    if(expression){RxFaceClip clip;return rx_face_clip(b->mouth,b->mouth_bytes,index,&clip);}
    if(group>=b->base_count/6)return "Lip command references an absent six-phoneme parameter group";
    RxLipClip clip;return rx_lip_clip(b->lip_bank,b->lip_bytes,index,&clip);
}
int rx_face_control_select_expression(BH_PWORK *actor,int index)
{
    if(index<0||rx_face_control_request(actor,1,(unsigned)index,0))return 0;
    FaceBinding *b=binding(actor);return rx_face_clip(b->mouth,b->mouth_bytes,(unsigned)index,&b->expression)==NULL;
}
int rx_face_control_select_lip(BH_PWORK *actor,int index)
{
    if(index<0||rx_face_control_request(actor,0,(unsigned)index,0))return 0;
    FaceBinding *b=binding(actor);return rx_lip_clip(b->lip_bank,b->lip_bytes,(unsigned)index,&b->selected)==NULL;
}
void rx_face_control_deform(BH_PWORK *actor,MASK_WORK *work)
{
    FaceBinding *b=binding(actor);
    if(!b||work!=&b->work)return;
    b->fault=b->preview?rx_face_mesh_animate_check(b->mesh,&work->param):rx_face_mesh_animate(b->mesh,&work->param);
}
const ML_WORK *rx_face_control_model(const BH_PWORK *actor)
{FaceBinding *b=binding(actor);return b&&b->ready?rx_face_pose_model(b->pose):NULL;}
static const char *face_tick(BH_PWORK *actor,int preview)
{
    const char *error=rx_face_control_ready(actor);if(error)return error;FaceBinding *b=binding(actor);
    if(b->words[1]<0||b->words[1]>1||(!(actor->flg2&0x80)&&(b->words[272]<0||b->words[272]==INT_MAX)))
        return "Invalid original facial initialization/freeze counter";
    if(b->words[265]||b->words[266]||b->words[267]||b->words[268])return "Packed facial pointer words must remain unused";
    if(b->work.head!=&b->head||b->work.mode!=1||b->words[8]!=(int32_t)b->base_count||b->head.nFace!=(unsigned)b->work.fnum||
       !((b->work.face==b->base&&b->work.fnum==(int)b->base_count)||
         (b->active.keys&&b->work.face==b->active.keys&&b->work.fnum==(int)b->active.count)))return "Foreign active facial expression";
    if(b->words[6]<0||b->words[6]>32767||b->words[7]<0||b->words[7]>32767||
       ((b->words[0]&8)&&(!b->expression.keys||b->words[5]<0)))return "Invalid native facial frame state";
    if((b->words[0]&4)&&(b->words[4]<0||(unsigned)b->words[4]>=b->base_count/6))return "Lip parameter group exceeds native base bank";
    if(b->lip.flag>1)return "Invalid native lip state flag";
    if((b->words[0]&4)&&b->lip.flag){uintptr_t top=(uintptr_t)b->playing.data,ptr=(uintptr_t)b->lip.ptr;
        if(!top||b->lip.top!=b->playing.data||ptr<top||ptr-top>=b->playing.bytes||(ptr-top)%2||
           b->lip.cur>=6||b->lip.next>=6||b->lip.cnt<0||b->lip.cnt>32765||b->lip.time<0||b->lip.time>255)
            return "Foreign or invalid active lip cursor";
        RxLipClip tail={b->lip.ptr,b->playing.bytes-(ptr-top)};LIP_WORK checked;
        if(rx_lip_begin(&tail,&checked))return "Active lip cursor escaped its terminated stream";
    }
    int32_t words[RX_FACE_CONTROL_WORDS];memcpy(words,b->words,sizeof(words));MASK_WORK work=b->work;HEAD_WORK head=b->head;
    LIP_WORK lip=b->lip;RxLipClip playing=b->playing;const PARAM_WORK *lip_base=b->lip_base;unsigned flags=actor->flg2;
    ML_WORK *previous=actor->mlwP;ML_WORK *model=(ML_WORK *)rx_face_pose_model(b->pose);int angles[31][3];
    for(unsigned i=0;i<31;++i)memcpy(angles[i],model->objP[i].ang,12);
    if((b->words[0]&4)&&!b->lip.flag)b->playing=b->selected;
    b->preview=preview;b->fault=NULL;actor->mlwP=model;bhControlMask(actor);actor->mlwP=previous;
    b->preview=0;
    if(b->fault||preview){const char *fault=b->fault;memcpy(b->words,words,sizeof(words));b->work=work;b->head=head;b->lip=lip;b->playing=playing;b->lip_base=lip_base;actor->flg2=flags;
        for(unsigned i=0;i<31;++i)memcpy(model->objP[i].ang,angles[i],12);b->fault=NULL;return fault;}
    if(b->work.face==b->base)b->active=(RxFaceClip){0};
    else if(b->work.face==b->expression.keys)b->active=b->expression;
    return NULL;
}

/* Original scalar controller runs in validation mode, then every touched scalar
   and flat angle is restored. Geometry validation performs no deformation. */
const char *rx_face_control_tick_check(BH_PWORK *actor)
{VuState saved=g_vu0;const char *error=face_tick(actor,1);g_vu0=saved;return error;}
const char *rx_face_control_tick(BH_PWORK *actor){return face_tick(actor,0);}
