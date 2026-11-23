#include "room_effects.h"
#include "effect_runtime.h"
#include "event_effects.h"
#include "main.h"
#include "room.h"
#include "ps2_NaMatrix.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(offsetof(EF_WRK,lkflg)==sizeof(EF_WORK),"Original effect constructor prefix changed");
_Static_assert(offsetof(O_WRK,mtx)==offsetof(BH_PWORK,mtx) &&
               offsetof(O_WRK,mlwP)==offsetof(BH_PWORK,mlwP),"Original actor prefix changed");

struct RxRoomEffects {
    ROM_WORK *room;
    EF_WRK *source, *original, *records;
    O_WRK *work;
    int *slots;
    size_t count;
    int published;
};

typedef struct {
    BH_PWORK *actor;
    ML_WORK *previous_model, model;
    NJS_MATRIX *previous_matrix, world;
    O_WORK *bones;
} WorldView;

static void rebase_self(O_WRK *work)
{
    work->mtx=(NJS_MATRIX *)work->mtxbuf;
    work->pvp=work->pv;
    work->tvp=work->tv;
}

void rx_room_effects_destroy(RxRoomEffects *effects)
{
    if (!effects) return;
    rx_event_effects_retire(effects);
    free(effects->original); free(effects->records); free(effects->work); free(effects->slots);
    free(effects);
}

static BH_PWORK *link_actor(const EF_WRK *record)
{
    int index=record->lkno;
    switch (record->lkflg) {
    case 1: return plp;
    /* Preserve the original linker's fallback-to-zero and direct enemy
       work-slot semantics, rather than substituting an RDT record index. */
    case 2: return &ene[index>=rom->ene_n?0:index];
    case 3: return sys->obwp ? (BH_PWORK *)&sys->obwp[index>=rom->obj_n?0:index] : NULL;
    case 4: return sys->itwp ? (BH_PWORK *)&sys->itwp[index>=rom->itm_n?0:index] : NULL;
    default: return NULL;
    }
}

static const char *validate_record(const EF_WRK *record)
{
    if (!(record->flg&3)) return "Inactive room effect records require the pending aliasing policy";
    if (record->id>=sizeof(sys->efm)/sizeof(*sys->efm)) return "Room effect ID exceeds native model work";
    if (record->lkflg<0 || record->lkflg>4)
        return "Room effect-to-effect linking is not ported yet";
    if (record->lkno<0 || record->lkono<0) return "Negative room effect actor or joint index";
    const float values[]={record->px,record->py,record->pz,record->sx,record->sy,record->sz,
                          record->lx,record->ly,record->lz};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);++i)
        if (!isfinite(values[i])) return "Nonfinite room effect transform";
    if (!record->lkflg) return NULL;
    BH_PWORK *actor=link_actor(record);
    if (!actor) return "Room effect references absent native actor work";
    if (!(actor->flg&1)) return NULL; // Original linker disables this link.
    if (actor->flg&(0x80|0x1000))
        return "Room effect requires the pending linked/frozen actor world-matrix path";
    if (!actor->mlwP || !actor->mlwP->obj_num || !actor->mlwP->owP)
        return "Room effect requires native actor joint work";
    if (actor->mlwP->obj_num>4096) return "Room effect actor joint count exceeds native bounds";
    if (!isfinite(actor->px) || !isfinite(actor->py) || !isfinite(actor->pz))
        return "Nonfinite room effect parent position";
    for (unsigned i=0;i<actor->mlwP->obj_num;++i)
        for (unsigned j=0;j<16;++j)
            if (!isfinite(actor->mlwP->owP[i].mtx[j])) return "Nonfinite room effect parent joint matrix";
    return NULL;
}

static const char *borrow_world(WorldView *view, BH_PWORK *actor)
{
    view->actor=actor; view->previous_model=actor->mlwP; view->previous_matrix=actor->mtx;
    view->model=*actor->mlwP;
    view->bones=malloc((size_t)view->model.obj_num*sizeof(*view->bones));
    if (!view->bones) return "Cannot allocate room effect world-joint views";
    memcpy(view->bones,view->model.owP,(size_t)view->model.obj_num*sizeof(*view->bones));
    njUnitMatrix(&view->world);
    njTranslate(&view->world,actor->px,actor->py,actor->pz);
    njRotateXYZ(&view->world,actor->ax,actor->ay,actor->az);
    for (unsigned i=0;i<view->model.obj_num;++i) {
        NJS_MATRIX local;
        memcpy(local,view->bones[i].mtx,sizeof(local));
        njSetMatrix(&view->bones[i].mtx,&view->world);
        njMultiMatrix(&view->bones[i].mtx,&local);
    }
    view->model.owP=view->bones;
    actor->mlwP=&view->model; actor->mtx=&view->world;
    return NULL;
}

const char *rx_room_effects_prepare(RxRoomEffects **out)
{
    if (!out || !rom || !sys || rom->eff_n<0 ||
        (size_t)rom->eff_n>sizeof(sys->efid)/sizeof(*sys->efid) ||
        (rom->eff_n && !rom->effp) || rom->ene_n<0 || rom->ene_n>128 ||
        rom->obj_n<0 || rom->obj_n>32 || rom->itm_n<0 || rom->itm_n>32)
        return "Invalid native room effect table request";
    size_t count=(size_t)rom->eff_n;
    unsigned free_slots=0;
    for (unsigned i=0;i<512;++i) free_slots+=!(eff[i].flg&3);
    if (count>free_slots) return "Original room effect pool is exhausted";
    for (size_t i=0;i<count;++i) {
        const char *error=validate_record(&rom->effp[i]);
        if (error) return error;
    }
    RxRoomEffects *next=calloc(1,sizeof(*next));
    O_WRK *saved_pool=NULL;
    WorldView *views=NULL;
    const char *error=NULL;
    if (!next) return "Cannot allocate native room effect table";
    next->count=count; next->room=rom; next->source=rom->effp;
    if (count) {
        next->original=malloc(count*sizeof(*next->original));
        next->records=malloc(count*sizeof(*next->records));
        next->work=malloc(count*sizeof(*next->work));
        next->slots=malloc(count*sizeof(*next->slots));
        saved_pool=malloc(sizeof(eff)); views=calloc(count,sizeof(*views));
        if (!next->original || !next->records || !next->work || !next->slots || !saved_pool || !views)
            error="Cannot allocate original room effect staging work";
    }
    if (!error && count) {
        memcpy(next->original,rom->effp,count*sizeof(*next->original));
        memcpy(next->records,rom->effp,count*sizeof(*next->records));
        size_t borrowed=0;
        for (size_t i=0;!error && i<count;++i) {
            const EF_WRK *record=&next->records[i];
            if (!record->lkflg) continue;
            BH_PWORK *actor=link_actor(record);
            if (!(actor->flg&1)) continue;
            size_t j=0;
            while (j<borrowed && views[j].actor!=actor) ++j;
            if (j==borrowed) {
                error=borrow_world(&views[borrowed],actor);
                if (!error) ++borrowed;
                else free(views[borrowed].bones);
            }
        }
        if (!error) {
            short saved_mapping[sizeof(sys->efid)/sizeof(*sys->efid)];
            memcpy(saved_mapping,sys->efid,sizeof(saved_mapping));
            memcpy(saved_pool,eff,sizeof(eff));
            rom->effp=next->records;
            bhSetEffectTable();
            for (size_t i=0;i<count;++i) {
                int slot=sys->efid[i];
                if (slot<0 || slot>=512 || (saved_pool[slot].flg&3)) {
                    error="Original effect constructor returned an invalid native slot";
                    break;
                }
                next->slots[i]=slot; next->work[i]=eff[slot]; rebase_self(&next->work[i]);
            }
            rom->effp=next->source;
            memcpy(eff,saved_pool,sizeof(eff));
            memcpy(sys->efid,saved_mapping,sizeof(saved_mapping));
        }
        while (borrowed) {
            WorldView *view=&views[--borrowed];
            view->actor->mlwP=view->previous_model; view->actor->mtx=view->previous_matrix;
            free(view->bones);
        }
    }
    free(saved_pool); free(views);
    if (error) { rx_room_effects_destroy(next); return error; }
    rx_room_effects_destroy(*out); *out=next;
    return NULL;
}

size_t rx_room_effects_count(const RxRoomEffects *effects) { return effects?effects->count:0; }
const O_WRK *rx_room_effects_work(const RxRoomEffects *effects,size_t record)
{
    return effects && record<effects->count?&effects->work[record]:NULL;
}
int rx_room_effects_slot(const RxRoomEffects *effects,size_t record)
{
    return effects && record<effects->count?effects->slots[record]:-1;
}

const EF_WRK *rx_room_effects_record(const RxRoomEffects *effects,size_t record)
{
    return effects && record<effects->count?&effects->records[record]:NULL;
}

const char *rx_room_effects_check_live(const RxRoomEffects *effects,RxRoomEffectFault *fault)
{
    if (fault) *fault=(RxRoomEffectFault){0};
    if (!effects) return "Native room effect table is not prepared";
    for (size_t i=0;i<effects->count;++i) {
        const char *error=rx_effect_validate(&effects->work[i]);
        if (error) {
            if (fault) *fault=(RxRoomEffectFault){i,effects->work[i].id,error};
            return error;
        }
    }
    return NULL;
}

const char *rx_room_effects_publish(RxRoomEffects *effects)
{
    if (!effects || effects->published || rom!=effects->room || rom->effp!=effects->source ||
        (size_t)rom->eff_n!=effects->count)
        return "Prepared room effect table no longer belongs to the active room";
    if (effects->count && memcmp(rom->effp,effects->original,effects->count*sizeof(*rom->effp)))
        return "Room effect records changed after preparation";
    for (size_t i=0;i<effects->count;++i)
        if (eff[effects->slots[i]].flg&3) return "Prepared room effect slot is no longer free";
    const char *mode_error=rx_event_effects_bind(effects,effects->source,effects->records,effects->work,effects->slots,effects->count,rx_effect_mode_check);
    if(mode_error)return mode_error;
    for (size_t i=0;i<effects->count;++i) {
        int slot=effects->slots[i];
        eff[slot]=effects->work[i]; rebase_self(&eff[slot]);
        sys->efid[i]=slot; rom->effp[i]=effects->records[i];
    }
    effects->published=1;
    return NULL;
}
