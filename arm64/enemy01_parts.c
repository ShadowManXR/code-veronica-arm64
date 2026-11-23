#include "enemy01_parts.h"
#include "enemy01_work.h"
#include "en01sub.h"
#include "eneset.h"
#include "main.h"
#include <stdint.h>
#include <string.h>
_Static_assert(offsetof(NJS_CNK_OBJECT,evalflags)==0&&offsetof(O_WORK,flg)==0,"Original flag aliases changed");
static RxEnemy01PartView parts[128];
static int slot(const BH_PWORK *a)
{uintptr_t p=(uintptr_t)a,b=(uintptr_t)ene;return p>=b&&p-b<sizeof(ene)&&!((p-b)%sizeof(*ene))?(int)((p-b)/sizeof(*ene)):-1;}
static size_t original_bytes(unsigned id){return id==31?32:id>=32&&id<=34?112:0;}
static unsigned minimum_nodes(unsigned id){return id==32?3:id==33?2:1;}
const RxEnemy01PartView *rx_enemy01_part_view(const BH_PWORK *a)
{
    int i=slot(a);if(i<0)return NULL;
    RxEnemy01PartView *p=parts+i;
    return p->scalar&&p->kind==a->id&&a->exp0==p->scalar?p:NULL;
}
int rx_enemy01_part_reserved(const BH_PWORK *a)
{int i=slot(a);return i>=0&&parts[i].scalar!=NULL;}
NJS_CNK_OBJECT **rx_enemy01_part_objects_slot(BH_PWORK *a)
{return rx_enemy01_part_view(a)?&parts[slot(a)].objects:NULL;}
O_WORK **rx_enemy01_part_world_slot(BH_PWORK *a)
{return rx_enemy01_part_view(a)?&parts[slot(a)].world:NULL;}
int rx_enemy01_parts_overlap(const void *data,size_t bytes)
{
    uintptr_t p=(uintptr_t)data;if(!p||p>UINTPTR_MAX-bytes)return 1;
    for(unsigned i=0;i<128;++i)if(parts[i].scalar){uintptr_t b=(uintptr_t)parts[i].scalar;if(p<b+parts[i].bytes&&b<p+bytes)return 1;}
    return 0;
}
static const char *fresh_target(BH_PWORK *a)
{
    const char *error=rx_enemy01_link_check(a);if(error)return error;
    int i=slot(a);size_t bytes=original_bytes(a->id);
    if(!bytes||parts[i].scalar||a->exp0||rx_enemy01_work_reserved(a))return "Unsupported or occupied linked body-part work";
    void *candidate=rx_enemy_arena_preview((int)bytes);
    if(!candidate||rx_enemy01_work_overlaps(candidate,bytes)||rx_enemy01_parts_overlap(candidate,bytes))
        return "Linked body-part work exceeds its available native arena";
    return NULL;
}
unsigned char *rx_enemy01_part_allocate(BH_PWORK *a)
{
    if(fresh_target(a))return NULL;
    size_t bytes=original_bytes(a->id);unsigned char *data=bhEne_CallocWork((int)bytes,8);
    if(!data)return NULL;
    parts[slot(a)]=(RxEnemy01PartView){data,bytes,NULL,NULL,a->id};a->exp0=data;return data;
}
const char *rx_enemy01_part_check(const BH_PWORK *a)
{
    const char *error=rx_enemy01_link_check(a);if(error)return error;
    const RxEnemy01PartView *p=rx_enemy01_part_view(a);
    if(!p||p->bytes!=original_bytes(a->id))return "Linked body part requires its original owned scalar view";
    for(unsigned i=0;i<8;++i)if(p->scalar[i])return "Packed body-part pointer cells must remain unused";
    if(!p->objects&&!p->world)return NULL;
    if(!p->objects||!p->world)return "Incomplete linked body-part node view";
    const ML_WORK *model=rx_enemy01_link_model(a);
    uintptr_t obj=(uintptr_t)p->objects,base=(uintptr_t)model->objP,world=(uintptr_t)p->world,wbase=(uintptr_t)model->owP;
    if(obj<base||world<wbase||(obj-base)%sizeof(*p->objects)||(world-wbase)%sizeof(*p->world))
        return "Foreign linked body-part object or world pointer";
    size_t index=(obj-base)/sizeof(*p->objects);
    if(index!=((world-wbase)/sizeof(*p->world))||index>=model->obj_num||minimum_nodes(a->id)>model->obj_num-index)
        return "Linked body-part slice escaped its parent model";
    return NULL;
}
const char *rx_enemy01_part_attach(BH_PWORK *a,unsigned first,unsigned count)
{
    const char *error=rx_enemy01_part_check(a);if(error)return error;
    const ML_WORK *model=rx_enemy01_link_model(a);
    if(first>=model->obj_num||count<minimum_nodes(a->id)||count>model->obj_num-first)
        return "Invalid linked body-part node slice";
    RxEnemy01PartView *p=parts+slot(a);p->objects=model->objP+first;p->world=model->owP+first;return NULL;
}
const char *rx_enemy01_part_initialize(BH_PWORK *a)
{
    const char *error=rx_enemy01_link_check(a);if(error)return error;
    if(a->mode0||!original_bytes(a->id))return "Original linked body-part continuation is not ported yet";
    error=a->exp0?rx_enemy01_part_check(a):fresh_target(a);if(error)return error;
    switch(a->id){case 31:bhEne01Head_Init(a);break;case 32:bhEne01Arm_Init(a);break;case 33:bhEne01Leg_Init(a);break;case 34:bhEne01Cap_Init(a);break;}
    return rx_enemy01_part_check(a);
}
const char *rx_enemy01_part_release_check(const BH_PWORK *a)
{
    int i=slot(a);if(i<0)return "Foreign linked body-part retirement";
    if(parts[i].scalar)return parts[i].kind==a->id&&a->exp0==parts[i].scalar?NULL:"Linked body-part work was replaced";
    return a->exp0?"Unowned linked body-part work on retirement":NULL;
}
void rx_enemy01_part_forget(BH_PWORK *a)
{
    int i=slot(a);if(i<0)return;
    if(parts[i].scalar&&a->exp0==parts[i].scalar)a->exp0=NULL;
    parts[i]=(RxEnemy01PartView){0};
}
void rx_enemy01_parts_reset(void)
{for(unsigned i=0;i<128;++i)rx_enemy01_part_forget(ene+i);}
