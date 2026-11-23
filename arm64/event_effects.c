#include "event_effects.h"
#include "main.h"
#include <stdint.h>
#include <string.h>
typedef struct {int slot;unsigned record_id,record_type,work_id,work_type;} Entry;
static struct {const void *owner;ROM_WORK *room;EF_WRK *source;size_t count;RxEffectModeCheck check;Entry entries[256];} context;
const char *rx_event_effects_bind(const void *owner,EF_WRK *source,const EF_WRK *records,const O_WRK *work,const int *slots,size_t count,RxEffectModeCheck check)
{
    if(!owner||!rom||source!=rom->effp||count>256||rom->eff_n!=(int)count||!check||(count&&(!records||!work||!slots)))return "Invalid native event effect publication";
    if(context.owner&&context.owner!=owner)return "Native event effect owner is still occupied";
    for(size_t i=0;i<count;++i){if(slots[i]<0||slots[i]>=512||!(work[i].flg&1))return "Invalid native event effect slot";
        for(size_t j=0;j<i;++j)if(slots[j]==slots[i])return "Duplicate native event effect slot";
    }
    context.owner=owner;context.room=rom;context.source=source;context.count=count;context.check=check;
    for(size_t i=0;i<count;++i)context.entries[i]=(Entry){slots[i],records[i].id,records[i].type,work[i].id,work[i].type};
    return NULL;
}
void rx_event_effects_retire(const void *owner){if(context.owner==owner)memset(&context,0,sizeof(context));}
static const char *record_work(unsigned record,O_WRK **out)
{
    if(!context.owner||rom!=context.room||rom->effp!=context.source||rom->eff_n!=(int)context.count||record>=context.count)return "EffModeSet requires its published native room effect record";
    const Entry *entry=context.entries+record;int slot=sys->efid[record];
    if(slot!=entry->slot||slot<0||slot>=512||rom->effp[record].id!=entry->record_id||rom->effp[record].type!=entry->record_type)return "EffModeSet references a foreign room effect mapping";
    O_WRK *op=eff+slot;
    if(!(op->flg&1)||op->id!=entry->work_id||op->type!=entry->work_type||op->mtx!=(void*)op->mtxbuf||op->pvp!=op->pv||op->tvp!=op->tv)return "EffModeSet references retired or foreign native effect work";
    *out=op;return NULL;
}
const char *rx_event_effect_mode_check(unsigned record,unsigned mode)
{
    if(mode>255)return "Original effect mode exceeds byte operand";
    O_WRK *op;const char *error=record_work(record,&op);return error?error:context.check(op,mode);
}
const char *rx_event_effect_work(unsigned record,O_WRK **out)
{
    if(!out)return "Missing native effect work output";
    O_WRK *op;const char *error=record_work(record,&op);if(error)return error;
    error=context.check(op,op->mode1);if(error)return error;
    *out=op;return NULL;
}
const char *rx_event_effect_work_check(const void *work)
{
    uintptr_t address=(uintptr_t)work,base=(uintptr_t)eff;
    if(!context.owner||address<base||address-base>=sizeof(eff)||(address-base)%sizeof(*eff))return "Foreign native effect task target";
    unsigned slot=(unsigned)((address-base)/sizeof(*eff));
    for(unsigned record=0;record<context.count;++record)if(context.entries[record].slot==(int)slot){
        O_WRK *op;const char *error=rx_event_effect_work(record,&op);return error?error:op==work?NULL:"Remapped native effect task target";
    }
    return "Native effect task target is not a published room effect";
}
const char *rx_event_effect_clear_check(void)
{
    /* The original passes inspect the fixed pool and only clear flags for
       other families. These cases also dereference expansion allocations;
       keep them gated until their constructors provide native ownership. */
    for(unsigned i=0;i<512;++i)if(eff[i].flg&1){
        switch(eff[i].id){
        case 22:case 24:case 25:case 27:
            if(eff[i].exp0)return "Event effect cleanup requires an owned legacy expansion payload";
            break;
        case 254:case 260:case 302:
            return "Event effect cleanup requires owned legacy expansion payloads";
        }
    }
    return NULL;
}
