#include "effect_runtime.h"
#include "shadow_runtime.h"
#include "effsub1.h"
#include "effect.h"
#include "ps2_texture.h"
#include "main.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_dummy.h"
#include "ps2_loadtim2.h"
#include "ps2_NaMatrix.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static const unsigned char *particle_uv[512];
static unsigned short particle_id[512],particle_type[512];
static uint64_t palette_generation;

uint64_t rx_effects_palette_generation(void) { return palette_generation; }

const char *rx_effects_sync_palette(void)
{
    /* Original bhControlEOR uploads this shared bank when gm_flg bit 2 is set. */
    if (!(sys->gm_flg&4)) return NULL;
    if (Send_1024_Clut_data(palbuf,0x3ff0)<0)
        return "Could not transfer the original shared effect palette";
    sys->gm_flg&=~4u;
    ++palette_generation;
    return NULL;
}

const char *rx_effects_load_common(RxEngineEffects *effects)
{
    unsigned char *pictures=rx_engine_effect_pictures(effects);
    if (!pictures || sys->ef_ct || sys->ef_tlist.nbTexture)
        return "Original common effect textures require a fresh texture bank";
    extern const EFFECT_INFO ef_info[21];
    sys->ef_tlist.textures=sys->ef_tex;
    unsigned count=0;
    for (unsigned i=0;i<20;++i) { sys->ef_tn[i]=(short)count;count+=(unsigned)ef_info[i].nbAnim; }
    if (count!=40) return "Original common effect animation table changed";
    int loaded=bhSetMemPvpTexture(&sys->ef_tlist,pictures,0);
    if (loaded!=(int)count) return "Could not register all original common effect textures";
    sys->ef_tlist.nbTexture=sys->ef_ct=sys->ef_ctb=count;
    if (!njSetTexture(&sys->ef_tlist)) return "Could not select the original common effect texture bank";
    njSetPaletteBankNum(0,0);
    for (unsigned i=0;i<20;++i) {
        unsigned char bank;
        if ((unsigned)sys->ef_tn[i]>=count || !njRoomTextureBank(&sys->ef_tex[sys->ef_tn[i]],&bank))
            return "Original common effect atlas has no native texture work";
        sys->ef_pbkb[i]=bank;
    }
    sys->ss_flg|=0x40;
    return NULL;
}

static int pool_index(const O_WRK *op)
{
    uintptr_t p=(uintptr_t)op,start=(uintptr_t)eff;
    return p>=start && p-start<sizeof(eff) && (p-start)%sizeof(*eff)==0 ?
           (int)((p-start)/sizeof(*eff)):-1;
}

static int tracked_uv(const O_WRK *op)
{
    int slot=pool_index(op);
    return slot>=0 && particle_uv[slot] && op->exp0==particle_uv[slot] &&
           particle_id[slot]==op->id && particle_type[slot]==op->type;
}

static int texture_ready(unsigned id,unsigned frame)
{
    int index=sys->ef_tn[id]+(int)frame;
    unsigned char bank;
    return (id<20?(index>=0 && index<sys->ef_ctb):(sys->efm[id].flg&0x200)!=0) && sys->ef_tlist.textures && index>=0 &&
           (unsigned)index<sys->ef_tlist.nbTexture &&
           njRoomTextureBank(&sys->ef_tlist.textures[index],&bank);
}

static const char *effect_parent(const O_WRK *op,BH_PWORK **out)
{
    *out=NULL;
    if (!(op->flg&0x80)) return NULL;
    uintptr_t address=(uintptr_t)op->lkwkp;
    BH_PWORK *parent=NULL;
    if (op->lkwkp==(unsigned char *)plp && plp) parent=plp;
    const struct { void *base; size_t count,stride; } pools[]={
        {ene,128,sizeof(*ene)},{sys->obwp,rom&&rom->obj_n>=0&&rom->obj_n<=32?(size_t)rom->obj_n:0,sizeof(O_WRK)},
        {sys->itwp,rom&&rom->itm_n>=0&&rom->itm_n<=32?(size_t)rom->itm_n:0,sizeof(O_WRK)}};
    for (unsigned i=0;!parent && i<sizeof(pools)/sizeof(*pools);++i) {
        uintptr_t start=(uintptr_t)pools[i].base;
        if (start && address>=start && address-start<pools[i].count*pools[i].stride &&
            (address-start)%pools[i].stride==0) parent=(BH_PWORK *)op->lkwkp;
    }
    if (!parent) return "Original effect references absent native actor work";
    if (!(parent->flg&1)) { *out=parent;return NULL; }
    if (parent->flg&(0x80|0x1000)) return "Original effect requires the pending linked/frozen parent transform";
    if (!parent->mlwP || !parent->mlwP->owP || parent->mlwP->obj_num>4096 || op->lkono<0 ||
        (unsigned)op->lkono>=parent->mlwP->obj_num)
        return "Original effect references absent native joint work";
    const float values[]={parent->px,parent->py,parent->pz,op->lox,op->loy,op->loz};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);++i)
        if (!isfinite(values[i])) return "Nonfinite original effect attachment";
    for (unsigned i=0;i<16;++i)
        if (!isfinite(parent->mlwP->owP[op->lkono].mtx[i])) return "Nonfinite original effect joint matrix";
    *out=parent;return NULL;
}

static void effect_attach(O_WRK *op,BH_PWORK *parent)
{
    if (!parent || !(parent->flg&1)) return;
    /* Original bhControlEffect consumes world joints. Native actors currently
       retain local skin joints, so compose their actor transform explicitly. */
    NJS_MATRIX world;NJS_POINT3 local;
    njUnitMatrix(&world);njTranslate(&world,parent->px,parent->py,parent->pz);
    njRotateXYZ(&world,parent->ax,parent->ay,parent->az);
    njCalcPoint(&parent->mlwP->owP[op->lkono].mtx,(NJS_POINT3 *)&op->lox,&local);
    njCalcPoint(&world,&local,(NJS_POINT3 *)&op->px);
}

void rx_effects_reset(void)
{
    rx_shadows_reset();
    memset(particle_uv,0,sizeof(particle_uv));
    memset(particle_id,0,sizeof(particle_id));memset(particle_type,0,sizeof(particle_type));
    sys->ef_fncn=0;memset(sys->ef_fnc,0,sizeof(sys->ef_fnc));
    sys->ef_trs2dn=0;memset(sys->ef_trs2d,0,sizeof(sys->ef_trs2d));
    sys->ef_trsn=0;
    memset(sys->ef_trs,0,sizeof(sys->ef_trs));
}

static const char *overlay_validate(const O_WRK *op)
{
    if (op->type!=45 || op->mode0>1 || (op->flg&0x80) || (op->stflg&0x20))
        return "Original screen-overlay variant is not ported yet";
    int page=op->mode0?op->ani_ct:op->lkono;
    if (page<0 || page>2 || !texture_ready(45,(unsigned)page))
        return "Original screen overlay references an absent texture page";
    if (op->mode0 && (op->tex_id!=45 || op->gidx!=bhGetGidx((O_WRK *)op)))
        return "Original screen-overlay cached texture identity changed";
    const float values[]={op->px,op->py,op->pz,op->sx,op->sy,op->sz,
                          op->tv[0].u,op->tv[0].v,op->tv[1].u,op->tv[1].v,
                          op->tv[2].u,op->tv[2].v,op->tv[3].u,op->tv[3].v};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);++i)
        if (!isfinite(values[i])) return "Nonfinite original screen-overlay state";
    if (op->mode0 && op->mode1 && (!op->sz || !isfinite(1.0f/op->sz)))
        return "Original screen overlay has an invalid reciprocal depth";
    return NULL;
}

static const char *filter_validate(const O_WRK *op)
{
    if (op->type || op->mode0>2 || op->ani_ct || (op->flg&0x80) || (op->stflg&0x20))
        return "Original camera-filter variant is not ported yet";
    if (!texture_ready(92,0)) return "Original camera-filter texture is not registered";
    if (op->mode0 && (op->tex_id!=92 || op->gidx!=bhGetGidx((O_WRK *)op)))
        return "Original camera-filter cached texture identity changed";
    for (unsigned i=0;i<4;++i)
        if (!isfinite(op->tv[i].u) || !isfinite(op->tv[i].v)) return "Nonfinite original camera-filter UV";
    return NULL;
}

const char *rx_effect_validate(const O_WRK *op)
{
    if (!op) return "Absent native effect work";
    if (op->id==21) {
        if (op->type || (op->func && op->func!=bhDraw021))
            return "Original cinema callback variant is not ported yet";
        return NULL;
    }
    if (op->id==92) return filter_validate(op);
    if (op->id==20) return overlay_validate(op);
    if (op->id==1) return rx_shadow_validate(op);
    /* These texture-only records use bhEffDmy in the original jump table. */
    if (op->id==33 || op->id==35 || op->id==45) return NULL;
    if (op->id!=9 && op->id!=115 && op->id!=124 && op->id!=125 && op->id!=126)
        return "Original effect update and drawing are not ported for this effect family";
    if ((op->id==9 && op->type!=1) || (op->id==115 && op->type!=2) || (op->id==124 && op->type!=0) ||
        (op->id==126 && (op->type>6 || op->mdlver>3)))
        return "Original particle-manager variant is not ported yet";
    if (op->id==9 || op->id==125) {
        BH_PWORK *parent;const char *error=effect_parent(op,&parent);
        if (error) return error;
    } else if (op->flg&0x80) return "Original particle attachment path is not ported yet";
    const float values[]={op->px,op->py,op->pz,op->sx,op->sy,op->sz,op->yn,op->spd,op->gpy};
    for (unsigned i=0;i<sizeof(values)/sizeof(*values);++i)
        if (!isfinite(values[i])) return "Nonfinite original particle state";
    unsigned atlas=op->id==9?8:op->id==125||op->id==126?35:33;
    for (unsigned frame=0;frame<((op->id==125||op->id==126)?3u:1u);++frame)
        if (!texture_ready(atlas,frame)) return "Original effect atlas is not registered";
    if (op->id==125 && op->mode1>7) return "Original emitter cue exceeds its child's supported types";
    if (op->id==126) {
        int last=op->type<4?18:op->type==4?19:15;
        if (op->mode0>1 || op->ct0<0 || op->ct0>last)
            return "Original falling particle animation exceeds its authored UV table";
        if (op->mode0) {
            if (!tracked_uv(op))
                return "Original falling particle has an untracked native animation pointer";
        }
    }
    if (op->id==9 && op->mode0) {
        if (!tracked_uv(op))
            return "Original flame has an untracked native animation pointer";
        if (op->mode0!=1 || op->ct0<0 || op->ct0>8)
            return "Original flame animation exceeds its authored UV table";
    }
    if (op->id==124 && op->mode0) {
        if (!tracked_uv(op))
            return "Original particle has an untracked native animation pointer";
        if (op->mode0>3 || op->ct0<0 || op->ct0>14 ||
            (op->mode0==1 && op->ct0>4) || (op->mode0==2 && (op->ct0<5 || op->ct0>8)) ||
            (op->mode0==3 && op->ct0<9))
            return "Original particle animation exceeds its authored UV table";
    }
    return NULL;
}

const char *rx_effects_tick(void)
{
    const char *palette_error=rx_effects_sync_palette();
    if (palette_error) return palette_error;
    if (!(sys->sp_flg&8) && !(sys->ef_flg&1)) return NULL;
    sys->ef_mdfn=sys->ef_trsn=sys->ef_trs2dn=sys->ef_fncn=0;
    for (unsigned i=0;i<512;++i) {
        O_WRK *op=&eff[i];
        if (!(op->flg&1) || (op->stflg&0x1000000)) continue;
        const char *error=rx_effect_validate(op);
        if (error) return error;
        op->idx=(unsigned short)i;
        if (op->id==1) {
            error=rx_shadow_step(op);
            if (error) return error;
            continue;
        }
        op->pxb=op->px;op->pyb=op->py;op->pzb=op->pz;
        op->axb=op->ax;op->ayb=op->ay;op->azb=op->az;
        switch (op->id) {
        case 33: case 35: case 45: bhEffDmy(op); break;
        case 92:
            if (op->mode0 && (cam.flg&8) && sys->fil_no==2 && sys->ef_trs2dn>=128)
                return "Original screen-overlay queue is full";
            bhEffFil(op);break;
        case 21:
            if (op->mode1 && sys->ef_fncn>=512) return "Original callback queue is full";
            bhEff021(op);break;
        case 20:
            if (op->mode0==1 && op->mode1 && sys->ef_trs2dn>=(int)(sizeof(sys->ef_trs2d)/sizeof(*sys->ef_trs2d)))
                return "Original screen-overlay queue is full";
            bhEff2D(op);break;
        case 115: bhEff115(op); break;
        case 125: {
            BH_PWORK *parent;error=effect_parent(op,&parent);if (error) return error;
            effect_attach(op,parent);bhEff125(op);break;
        }
        case 9:
        case 124:
        case 126:
            if (sys->ef_trsn>=(int)(sizeof(sys->ef_trs)/sizeof(*sys->ef_trs)))
                return "Original translucent effect queue is full";
            if (!op->mode0) particle_uv[i]=NULL;
            if (op->id==9) {
                BH_PWORK *parent;error=effect_parent(op,&parent);if (error) return error;
                effect_attach(op,parent);bhEff009(op);
            } else if (op->id==126) bhEff126(op);
            else bhEff124(op);
            if (!particle_uv[i] && op->mode0==1) {
                particle_uv[i]=op->exp0;particle_id[i]=op->id;particle_type[i]=op->type;
            }
            break;
        }
    }
    return NULL;
}

const char *rx_effect_mode_check(const O_WRK *op,unsigned mode)
{
    const char *error=rx_effect_validate(op);if(error)return error;
    if(mode>255)return "Original effect mode exceeds byte operand";
    /* These room controllers remain allocated while their owner is active.
       Expiring particles and shadows are not event-mode targets yet. */
    switch(op->id){
    case 9:case 33:case 35:case 45:case 92:case 21:break;
    case 115:if(op->type!=2)return "Original event smoke variant is not ported yet";break;
    case 125:if(mode>7)return "Original event emitter mode exceeds native child types";break;
    case 20:
        if(mode&&(!op->sz||!isfinite(1.0f/op->sz)))return "Original event overlay has invalid reciprocal depth";
        break;
    default:return "Original event effect mode family is not ported yet";
    }
    return NULL;
}
