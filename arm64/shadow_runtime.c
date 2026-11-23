#include "shadow_runtime.h"
#include "effsub1.h"
#include "main.h"
#include "ps2_NaMatrix.h"
#include <stdint.h>
#include <string.h>

void rx_shadows_reset(void)
{
    memset(eff,0,sizeof(eff));
    sys->ef_mdfn=0;
    memset(sys->ef_mdf,0,sizeof(sys->ef_mdf));
    for (unsigned i=0;i<sizeof(sys->efid)/sizeof(*sys->efid);++i) sys->efid[i]=-1;
}

const char *rx_shadow_validate(const O_WRK *op)
{
    if (!op || op->id!=1 || op->type!=1 || op->mdlver>=2 || op->mlwP!=&sys->efm[1] ||
        (op->flg&0x200080)!=0x200080)
        return "Original effect update and drawing are not ported for this effect family";
    BH_PWORK *parent=(BH_PWORK *)op->lkwkp;
    uintptr_t address=(uintptr_t)parent,start=(uintptr_t)ene;
    int known=parent && (parent==plp || (address>=start && address-start<sizeof(ene) &&
                                       (address-start)%sizeof(*ene)==0));
    if (!known || !parent->mlwP || !parent->mlwP->owP ||
        op->lkono<0 || (unsigned)op->lkono>=parent->mlwP->obj_num)
        return "Original shadow references an absent native actor joint";
    if (parent->stflg&0x2000) return "Original shadow requires the pending elevated-ground actor path";
    if (parent->flg&(0x80|0x1000))
        return "Original shadow requires the pending linked/frozen actor world-matrix path";
    return NULL;
}

const char *rx_shadow_step(O_WRK *op)
{
        const char *error=rx_shadow_validate(op);
        if (error) return error;
        BH_PWORK *parent=(BH_PWORK *)op->lkwkp;
        if (sys->ef_mdfn>=(int)(sizeof(sys->ef_mdf)/sizeof(*sys->ef_mdf)))
            return "Original shadow draw queue is full";
        op->pxb=op->px; op->pyb=op->py; op->pzb=op->pz;
        op->axb=op->ax; op->ayb=op->ay; op->azb=op->az;
        if (parent->flg&1) {
            /* Player skin matrices currently live in actor-local space. Build
               the world-space attachment used by bhControlEffect without
               altering those matrices or the hand attachments. */
            NJS_MATRIX world;
            njUnitMatrix(&world);
            njTranslate(&world,parent->px,parent->py,parent->pz);
            njRotateXYZ(&world,parent->ax,parent->ay,parent->az);
            NJS_POINT3 local;
            njCalcPoint(&parent->mlwP->owP[op->lkono].mtx,(NJS_POINT3 *)&op->lox,&local);
            njCalcPoint(&world,&local,(NJS_POINT3 *)&op->px);
        }
        bhEff001(op);
    return NULL;
}

const char *rx_shadows_tick(void)
{
    if (!(sys->sp_flg&8) && !(sys->ef_flg&1)) return NULL;
    sys->ef_mdfn=0;
    for (unsigned i=0;i<512;++i) {
        O_WRK *op=&eff[i];
        if (!(op->flg&1) || (op->stflg&0x1000000)) continue;
        op->idx=(unsigned short)i;
        const char *error=rx_shadow_step(op);
        if (error) return error;
    }
    return NULL;
}
