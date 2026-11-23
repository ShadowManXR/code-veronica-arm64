#include "room_skin.h"
#include "njplus.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    SKIN_GROUPS = 128, SKIN_VERTEX_TYPE = 51, SKIN_VERTEX_BYTES = 32,
    SKIN_AFFECTS_VERTICES = 1, SKIN_OWNS_GROUP = 2
};
#define SKIN_SKIP_NODE UINT32_C(0x20000000)
#define SKIN_OWNER_NODE UINT32_C(0x80000000)

_Static_assert(sizeof(HDR_PS) == 64 && sizeof(int) == 4, "Unexpected original skin layout");
_Static_assert(sizeof(NJS_POINT4) * 2 == SKIN_VERTEX_BYTES &&
               sizeof(((NP_WORK *)0)->cobj) / sizeof(NJS_CNK_OBJECT *) == SKIN_GROUPS,
               "Skin work layout differs from the original engine");

typedef struct {
    size_t index;
    unsigned flags, group;
    float pivot[3];
} Joint;

typedef struct {
    const int *weights;
    unsigned char *vertices;
    size_t vertex_count;
    size_t owner;
    float origin[3];
} Group;

typedef struct {
    const RxModel *source;
    NJS_CNK_OBJECT *objects;
    const int *words, *end;
    Joint *joints;
    size_t joint_count;
    Group groups[SKIN_GROUPS];
} SkinWork;

static const char *collect_group(SkinWork *work, const RxNode *node, const Joint *joint)
{
    if (work->words == work->end) return "missing skin weight count";
    unsigned count = (unsigned)*work->words++;
    if (count > (size_t)(work->end - work->words)) return "skin weights exceed payload";
    if (!node->model || !node->model->vertices || node->model->vertex_extent < sizeof(HDR_PS))
        return "missing skin vertex header";
    HDR_PS header;
    memcpy(&header, node->model->vertices, sizeof(header));
    size_t bytes = sizeof(header) + (size_t)header.usIndexMax * SKIN_VERTEX_BYTES;
    size_t chunk_bytes = ((size_t)header.usSize + 1) * 4;
    if (header.ucType != SKIN_VERTEX_TYPE || bytes > chunk_bytes ||
        chunk_bytes > node->model->vertex_extent || header.usIndexMax > count)
        return "skin vertex count exceeds its stream or weight table";

    Group *group = &work->groups[joint->group];
    group->weights = work->words;
    group->vertices = (unsigned char *)work->objects[joint->index].model->vlist + sizeof(header);
    group->vertex_count = header.usIndexMax;
    group->owner = joint->index;
    memcpy(group->origin, joint->pivot, sizeof(group->origin));
    work->words += count;
    return NULL;
}

/* njplus.c visits a child with the updated translation and a sibling with
   the parent's translation. A skipped node consumes neither a record nor a
   joint index. No rotation or scale is applied by the original conversion. */
static const char *collect_joints(SkinWork *work, const RxNode *node, const float parent[3])
{
    for (; node; node = node->sibling) {
        const float *pivot = parent;
        if (!(node->flags & SKIN_SKIP_NODE)) {
            if (work->words == work->end) return "missing skin joint record";
            Joint *joint = &work->joints[work->joint_count++];
            joint->index = (size_t)(node - work->source->nodes);
            joint->flags = (unsigned)*work->words++;
            for (size_t axis = 0; axis < 3; ++axis) {
                joint->pivot[axis] = parent[axis] + node->position[axis];
                if (!isfinite(joint->pivot[axis])) return "nonfinite skin joint pivot";
            }
            pivot = joint->pivot;
            if (joint->flags) {
                if (work->words == work->end) return "missing skin group index";
                joint->group = (unsigned)*work->words++;
                if (joint->group >= SKIN_GROUPS) return "skin group index outside engine table";
                if (joint->flags & SKIN_OWNS_GROUP) {
                    const char *error = collect_group(work, node, joint);
                    if (error) return error;
                }
            }
        }
        if (node->child) {
            const char *error = collect_joints(work, node->child, pivot);
            if (error) return error;
        }
    }
    return NULL;
}

static void convert_vertices(const Joint *joint, const Group *group, size_t joint_index)
{
    // The original right-shifts a signed 32-bit weight. Its high byte can
    // select a nonnegative joint only in the range 0..127.
    if (joint_index >= 128) return;
    for (size_t i = 0; i < group->vertex_count; ++i) {
        if (((uint32_t)group->weights[i] >> 24) != joint_index) continue;
        unsigned char *vertex = group->vertices + i * SKIN_VERTEX_BYTES;
        float position[3];
        memcpy(position, vertex, sizeof(position));
        for (size_t axis = 0; axis < 3; ++axis)
            position[axis] = (position[axis] + group->origin[axis]) - joint->pivot[axis];
        memcpy(vertex, position, sizeof(position));
    }
}

const char *rx_room_skin_convert_mesh(RxEngineModelPart *part, const RxModel *source,
                                     RxSkinMesh *mesh)
{
    if (!part->skin) return NULL;
    if (!source->node_count || part->skin_bytes % sizeof(*part->skin))
        return "skin has no model hierarchy or has a partial word";
    SkinWork work = {
        .source = source,
        .objects = part->model.objP,
        .words = part->skin,
        .end = part->skin + part->skin_bytes / sizeof(*part->skin),
        .joints = calloc(source->node_count, sizeof(*work.joints))
    };
    if (!work.joints) return "cannot allocate skin work";
    const float origin[3] = {0};
    const char *error = collect_joints(&work, source->nodes, origin);
    if (!error && mesh) {
        const Group *group = &work.groups[0];
        if (!group->vertices || source->chunk_count != 1 ||
            work.joint_count != source->node_count)
            error = "unsupported player skin layout";
        for (size_t i = 0; !error && i < group->vertex_count; ++i) {
            uint32_t weight = (uint32_t)group->weights[i];
            unsigned index = weight >> 24;
            if (index >= work.joint_count || ((weight >> 16) & 255) >= 10 ||
                work.joints[index].index != index ||
                !(work.joints[index].flags & SKIN_AFFECTS_VERTICES) ||
                work.joints[index].group != 0)
                error = "invalid player skin weight";
        }
        if (!error)
            *mesh = (RxSkinMesh){group->weights, group->vertices, group->vertex_count, group->owner};
    }
    if (!error) {
        for (size_t i = 0; i < work.joint_count; ++i) {
            const Joint *joint = &work.joints[i];
            if ((joint->flags & SKIN_AFFECTS_VERTICES) && !work.groups[joint->group].vertices) {
                error = "skin joint references a group without an owner";
                break;
            }
        }
    }
    if (!error) {
        for (size_t i = 0; i < work.joint_count; ++i) {
            const Joint *joint = &work.joints[i];
            if (joint->flags & SKIN_OWNS_GROUP) work.objects[joint->index].evalflags |= SKIN_OWNER_NODE;
            if (joint->flags & SKIN_AFFECTS_VERTICES)
                convert_vertices(joint, &work.groups[joint->group], i);
        }
    }
    free(work.joints);
    return error;
}

const char *rx_room_skin_convert(RxEngineModelPart *part, const RxModel *source)
{
    return rx_room_skin_convert_mesh(part, source, NULL);
}
