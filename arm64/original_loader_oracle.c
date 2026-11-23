/* Only compiled by the 32-bit desktop reference test. Include the unmodified
   loader with its SDK records and the two work structs it directly accesses. */
#include "ninja_binary.h"
#include <stdlib.h>
#include <string.h>

#define _TYPEDEF_Uint32
#define _TYPEDEF_Sint32
typedef uint32_t Uint32;
typedef int32_t Sint32;
#include <sg_xpt.h>
#include <ninjadef.h>
#include <ninjastr.h>
#include <ninjacnk.h>

typedef struct {
    unsigned flg, obj_num;
    void *datP;
    NJS_CNK_OBJECT *objP;
    NJS_TEXLIST *texP;
    void *owP;
} ML_WORK;
typedef struct { void *p[2]; } NJS_MDATA2_MOD;
typedef struct {
    unsigned flg, obj_num, frm_num;
    void *datP;
    NJS_MDATA2_MOD *md2P;
    unsigned short *atrP;
} MN_WORK;
_Static_assert(sizeof(void *) == 4 && sizeof(NJS_CNK_OBJECT) == 52 &&
               sizeof(NJS_CNK_MODEL) == 24 && sizeof(NJS_MDATA2_MOD) == 8 &&
               sizeof(ML_WORK) == 24 && sizeof(MN_WORK) == 24,
               "Original loader oracle requires its original record sizes");

int bhBscBinRealize(NJS_MODEL *, unsigned);
int bhCnkBinRealize(NJS_CNK_MODEL *, unsigned);
#define _BINFUNC_H_
#include "../port/decomp/src/ps2/veronica/prog/binfunc.c"

static int same_ref(const void *a, const uint8_t *a_base,
                    const void *b, const uint8_t *b_base, size_t size)
{
    if (!a || !b) return !a && !b;
    uintptr_t x = (uintptr_t)a - (uintptr_t)a_base;
    uintptr_t y = (uintptr_t)b - (uintptr_t)b_base;
    return x == y && x < size;
}

static int same_node(const NJS_CNK_OBJECT *a, const NJS_CNK_OBJECT *a_base,
                     const RxNode *b, const RxNode *b_base)
{
    if (!a || !b) return !a && !b;
    return a - a_base == b - b_base;
}

int rx_compare_original(const uint8_t *mdl, size_t model_size, const RxModel *model,
                        const uint8_t *mtn, size_t motion_size, const RxMotion *motion)
{
    uint8_t *a = malloc(model_size), *b = malloc(motion_size);
    if (!a || !b) { free(a); free(b); return 0; }
    memcpy(a, mdl, model_size);
    memcpy(b, mtn, motion_size);
    ML_WORK md;
    MN_WORK mt;
    int ok = bhMlbBinRealize(a, &md) && bhMnbBinRealize(b, &mt) &&
             md.flg == model->flags && md.obj_num == model->node_count &&
             mt.flg == motion->flags && mt.obj_num == motion->node_count &&
             mt.frm_num == motion->frame_count &&
             md.texP && md.texP->nbTexture == model->texture_count &&
             same_ref(mt.atrP, b, motion->attributes, mtn, motion_size);
    for (unsigned i = 0; ok && i < md.obj_num; ++i) {
        const NJS_CNK_OBJECT *n = &md.objP[i];
        const RxNode *r = &model->nodes[i];
        ok = n->evalflags == r->flags && !memcmp(n->pos, r->position, sizeof(n->pos)) &&
             !memcmp(n->ang, r->angle, sizeof(n->ang)) && !memcmp(n->scl, r->scale, sizeof(n->scl)) &&
             same_node(n->child, md.objP, r->child, model->nodes) &&
             same_node(n->sibling, md.objP, r->sibling, model->nodes) &&
             (n->model != NULL) == (r->model != NULL);
        if (ok && n->model) {
            const NJS_CNK_MODEL *c = n->model;
            const RxChunk *s = r->model;
            ok = c->r == s->radius && c->center.x == s->center[0] &&
                 c->center.y == s->center[1] && c->center.z == s->center[2] &&
                 same_ref(c->vlist == (void *)-1 ? NULL : c->vlist, a, s->vertices, mdl, model_size) &&
                 same_ref(c->plist == (void *)-1 ? NULL : c->plist, a, s->polygons, mdl, model_size);
            for (unsigned j = 0; ok && j < i; ++j)
                ok = (md.objP[j].model == c) == (model->nodes[j].model == s);
        }
    }
    for (unsigned i = 0; ok && i < model->texture_count; ++i) {
        const NJS_TEXNAME *t = &md.texP->textures[i];
        const RxTexture *r = &model->textures[i];
        ok = t->attr == r->attributes && t->texaddr == r->texture &&
             same_ref(t->filename, a, r->filename, mdl, model_size);
    }
    for (unsigned i = 0; ok && i < mt.obj_num; ++i) {
        ok = same_ref(mt.md2P[i].p[0], b, motion->tracks[i].position, mtn, motion_size) &&
             same_ref(mt.md2P[i].p[1], b, motion->tracks[i].angle, mtn, motion_size);
    }
    free(a);
    free(b);
    return ok;
}
