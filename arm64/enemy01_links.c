#include "enemy01_work.h"
#include "enemy01_parts.h"
#include "main.h"
#include <math.h>
#include <string.h>

typedef struct { int valid; ML_WORK model; MN_WORK *motions; } ParentView;
typedef struct { BH_PWORK *parent; int joint; short id; } LinkView;
static ParentView parents[128];
static LinkView links[128];
extern EGG_WORK zob_lkmtab;
static const EGG_WORK original_template={0x81,1,0,0,0,0,0,0.0f,0.0f,0.0f,0,0,0,0,{0,0,0,0}};
static int slot(const BH_PWORK *a)
{
    uintptr_t p=(uintptr_t)a,b=(uintptr_t)ene;
    return p>=b&&p-b<sizeof(ene)&&!((p-b)%sizeof(*ene))?(int)((p-b)/sizeof(*ene)):-1;
}
static int same_model(const ML_WORK *a,const ML_WORK *b)
{return a->objP==b->objP&&a->owP==b->owP&&a->obj_num==b->obj_num&&a->datP==b->datP&&a->texP==b->texP;}
const char *rx_enemy01_model_bind(BH_PWORK *parent)
{
    int i=slot(parent);
    if(i<0||parent->id!=1||!(parent->flg&1)||(parent->flg&0x80)||parent->mlwP!=parent->mdl||!parent->mnwP)
        return "Zombie link parent requires an unlinked native model view";
    ML_WORK *model=parent->mdl;
    if(!model->obj_num||model->obj_num>64||!model->objP||!model->owP||
       (uintptr_t)model->objP%_Alignof(NJS_CNK_OBJECT)||(uintptr_t)model->owP%_Alignof(O_WORK)||
       (uintptr_t)parent->mnwP%_Alignof(MN_WORK))return "Invalid native zombie parent model extent/alignment";
    if(parents[i].valid&&(!same_model(model,&parents[i].model)||parent->mnwP!=parents[i].motions))
        return "Zombie parent model binding is already occupied";
    parents[i]=(ParentView){1,*model,parent->mnwP};return NULL;
}
static const char *parent_check(const BH_PWORK *parent)
{
    int i=slot(parent);
    if(i<0||!parents[i].valid)return "Zombie link requires its registered parent model";
    const char *error=rx_enemy01_work_check(parent);if(error)return error;
    if(!(parent->flg&1)||(parent->flg&0x80)||parent->mlwP!=parent->mdl||
       !same_model(parent->mdl,&parents[i].model)||parent->mnwP!=parents[i].motions)
        return "Zombie link parent model/motion identity changed";
    return NULL;
}
const char *rx_enemy01_link_preflight(BH_PWORK *parent,int joint,short id)
{
    const char *error=parent_check(parent);if(error)return error;
    if(joint<0||(unsigned)joint>=parent->mdl[0].obj_num||(id!=1&&(id<31||id>38)))
        return "Unported zombie link joint or body-part controller";
    if(memcmp(&zob_lkmtab,&original_template,sizeof(original_template)))return "Modified original zombie link spawn template";
    for(unsigned i=0;i<32;++i)if(!isfinite(rom->grand[i]))return "Invalid original zombie spawn floor table";
    if((unsigned)sys->ewk_n>128)return "Invalid original enemy pool extent";
    for(unsigned i=0;i<128;++i)if(!(ene[i].flg&1)){
        if(ene[i].exp0||rx_enemy01_work_reserved(ene+i)||rx_enemy01_part_reserved(ene+i)||parents[i].valid||links[i].parent)
            return "Zombie link free slot still owns a native context";
        return NULL;
    }
    return "Original zombie link enemy pool is full";
}
/* The source constructor calls this only after successful whole-spawn preflight
   and the unchanged first-free bhSetEnemy allocation on the engine thread. */
void rx_enemy01_link_record(BH_PWORK *parent,BH_PWORK *child,int joint,short id)
{links[slot(child)]=(LinkView){parent,joint,id};}
static const char *link_identity(const BH_PWORK *child,int inactive)
{
    int i=slot(child);if(i<0||!links[i].parent)return "Unregistered native zombie linked actor";
    LinkView *view=links+i;const char *error=parent_check(view->parent);if(error)return error;
    if((!inactive&&!(child->flg&1))||!(child->flg&0x80)||child->id!=view->id||child->lkwkp!=(void*)view->parent||
       child->lkono!=view->joint||child->mlwP!=view->parent->mdl||child->mnwP!=view->parent->mnwP)
        return "Zombie linked actor lost its original parent/joint/model/motion identity";
    return NULL;
}
const char *rx_enemy01_link_check(const BH_PWORK *child)
{return link_identity(child,0);}
const ML_WORK *rx_enemy01_link_model(const BH_PWORK *child)
{return rx_enemy01_link_check(child)?NULL:links[slot(child)].parent->mdl;}
const char *rx_enemy01_link_retire(BH_PWORK *child)
{
    int i=slot(child);if(i<0||child->flg&1)return "Zombie link retirement requires an inactive native child";
    const char *error=link_identity(child,1);if(error)return error;
    if(parents[i].valid||rx_enemy01_work_reserved(child))return "Zombie child still owns a parent context";
    BH_PWORK *parent=links[i].parent;
    RxEnemy01Work *owner=rx_enemy01_work_get(parent);
    if(parent->exp1==(void*)child)return "Zombie secondary body is still referenced";
    for(unsigned n=0;n<9;++n)if(owner->children[n]==child)return "Zombie child is still referenced by its parent work";
    for(unsigned n=0;n<128;++n)if(links[n].parent==child)return "Zombie child still owns dependent links";
    error=rx_enemy01_part_release_check(child);if(error)return error;
    rx_enemy01_part_forget(child);links[i]=(LinkView){0};return NULL;
}
void rx_enemy01_links_reset(void)
{memset(parents,0,sizeof(parents));memset(links,0,sizeof(links));}
