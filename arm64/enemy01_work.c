#include "enemy01_work.h"
#include "main.h"
#include "en01.h"
#include "eneset.h"
#include "enemy01_parts.h"
#include <string.h>

_Static_assert(sizeof(int)==4&&sizeof(float)==4,"Original scalar widths changed");
_Static_assert(offsetof(RxEnemy01Work,children)==RX_ENEMY01_SCALAR_BYTES,"Native pointers overlap original scalar bytes");
static RxEnemy01Work *bindings[128];
static const char *validate_storage(const RxEnemy01Work *work);
static int actor_slot(const BH_PWORK *actor)
{
    uintptr_t base=(uintptr_t)ene,p=(uintptr_t)actor;
    if(p<base||p-base>=sizeof(ene)||(p-base)%sizeof(*ene))return -1;
    return (int)((p-base)/sizeof(*ene));
}
int rx_enemy01_work_reserved(const BH_PWORK *actor)
{int i=actor_slot(actor);return i>=0&&bindings[i]!=NULL;}
int rx_enemy01_work_overlaps(const void *data,size_t bytes)
{
    uintptr_t p=(uintptr_t)data;if(!p||p>UINTPTR_MAX-bytes)return 1;
    for(unsigned i=0;i<128;++i)if(bindings[i]){
        uintptr_t b=(uintptr_t)bindings[i];if(p<b+sizeof(*bindings[i])&&b<p+bytes)return 1;
    }
    return 0;
}
RxEnemy01Work *rx_enemy01_work_get(const BH_PWORK *actor)
{
    int slot=actor_slot(actor);if(slot<0)return NULL;
    RxEnemy01Work *work=bindings[slot];
    return work&&actor->id==1&&actor->exp0==work->scalar?work:NULL;
}
static const char *binding_target(BH_PWORK *actor,RxEnemy01Work *work,size_t bytes)
{
    int slot=actor_slot(actor);
    if(slot<0||actor->id!=1||!work||bytes!=sizeof(*work)||(uintptr_t)work%_Alignof(RxEnemy01Work))
        return "Invalid native enemy01 scalar/pointer work";
    if((bindings[slot]&&bindings[slot]!=work)||(actor->exp0&&actor->exp0!=work->scalar))
        return "Enemy01 work would replace an occupied context";
    uintptr_t start=(uintptr_t)work;
    if(start>UINTPTR_MAX-sizeof(*work))return "Enemy01 work extent overflows host memory";
    for(unsigned i=0;i<128;++i)if(i!=(unsigned)slot&&bindings[i]){
        uintptr_t other=(uintptr_t)bindings[i];
        if(start<other+sizeof(*work)&&other<start+sizeof(*work))
            return "Enemy01 work cannot overlap another actor context";
    }
    if(rx_enemy01_parts_overlap(work,bytes))return "Zombie parent work overlaps a linked part context";
    return NULL;
}
const char *rx_enemy01_work_bind(BH_PWORK *actor,RxEnemy01Work *work,size_t bytes)
{
    const char *target=binding_target(actor,work,bytes);if(target)return target;
    int slot=actor_slot(actor);
    const char *error=validate_storage(work);if(error)return error;
    bindings[slot]=work;actor->exp0=work->scalar;return NULL;
}
static uintptr_t arena_base,arena_end;
const char *rx_enemy_arena_bind(void *base,size_t bytes)
{
    uintptr_t p=(uintptr_t)base;
    if(!p||p%64||!bytes||p>UINTPTR_MAX-bytes)return "Invalid native enemy arena extent";
    if(arena_base&&(arena_base!=p||arena_end!=p+bytes))return "Native enemy arena is already registered";
    arena_base=p;arena_end=p+bytes;return NULL;
}
void rx_enemy_arena_release(void *base)
{if(arena_base==(uintptr_t)base)arena_base=arena_end=0;}
unsigned char *rx_enemy_arena_preview(int bytes)
{
    uintptr_t current=(uintptr_t)sys->memp,origin=(uintptr_t)sys->mempb;
    if(bytes<0||!arena_base||(uintptr_t)sys->endp!=arena_end||origin<arena_base||origin>current||
       current<arena_base||current>arena_end||current>UINTPTR_MAX-63)return NULL;
    uintptr_t aligned=(current+63)&~(uintptr_t)63;
    if(aligned>arena_end||(size_t)bytes>arena_end-aligned)return NULL;
    return (unsigned char *)aligned;
}
unsigned char *rx_enemy01_work_allocate(BH_PWORK *actor)
{
    int slot=actor_slot(actor);
    if(slot<0||bindings[slot]||actor->exp0)return NULL;
    RxEnemy01Work *work=(void *)rx_enemy_arena_preview(sizeof(RxEnemy01Work));
    if(binding_target(actor,work,sizeof(*work)))return NULL;
    if(bhEne_CallocWork(sizeof(*work),8)!=(void *)work)return NULL;
    bindings[slot]=work;actor->exp0=work->scalar;
    return actor->exp0;
}
BH_PWORK **rx_enemy01_child_slot(BH_PWORK *actor,unsigned offset)
{
    RxEnemy01Work *work=rx_enemy01_work_get(actor);
    return work&&!(offset%4)&&offset<=0x20?work->children+offset/4:NULL;
}
ATR_WORK **rx_enemy01_attribute_slot(BH_PWORK *actor,unsigned offset)
{
    RxEnemy01Work *work=rx_enemy01_work_get(actor);
    return work&&(offset==0x70||offset==0x74)?work->attributes+(offset-0x70)/4:NULL;
}
static const char *validate_storage(const RxEnemy01Work *work)
{
    for(unsigned i=0;i<RX_ENEMY01_SCALAR_BYTES;++i)
        if((i<0x24||(i>=0x70&&i<0x78))&&work->scalar[i])
            return "Packed enemy01 pointer cells must remain unused";
    for(unsigned i=0;i<RX_ENEMY01_CHILDREN;++i)
        if(work->children[i]&&actor_slot(work->children[i])<0)return "Enemy01 child reference escaped the native enemy pool";
    return NULL;
}
const char *rx_enemy01_work_check(const BH_PWORK *actor)
{
    RxEnemy01Work *work=rx_enemy01_work_get(actor);
    return work?validate_storage(work):"Enemy01 requires its bound native work";
}
const char *rx_enemy01_work_reinitialize(BH_PWORK *actor)
{
    const char *error=rx_enemy01_work_check(actor);if(error)return error;
    bhEne01_ResetWork(actor);
    return rx_enemy01_work_check(actor);
}
void rx_enemy01_work_reset(void)
{
    rx_enemy01_parts_reset();
    rx_enemy01_links_reset();
    for(unsigned i=0;i<128;++i){
        if(bindings[i]&&ene[i].exp0==bindings[i]->scalar)ene[i].exp0=NULL;
        bindings[i]=NULL;
    }
}
