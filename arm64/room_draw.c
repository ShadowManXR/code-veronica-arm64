#include "room_draw.h"
#include "ps2_NaDraw2D.h"
#include "ps2_NaFog.h"
#include "ps2_NaMatrix.h"
#include "ps2_Vu1Scissor2.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { CNK_END = 255, CNK_VERTEX_PS2 = 51, CNK_TEXTURE = 8, CNK_DIFFUSE_AMBIENT = 19, CNK_UVH = 66 };

typedef struct {
    RxRoomStrip strip;
    size_t sequence;
    NJS_MATRIX matrix;
    RxRoomVertex vertices[];
} DeferredStrip;

typedef struct {
    uintptr_t start, end;
    uintptr_t vertex_start,vertex_end;
    int validating;
    unsigned texture_count;
    RxRoomSubmit submit;
    void *context;
    RxRoomVertex *vertices;
    size_t vertex_capacity;
    VU1_STRIP_BUF *lit_vertices;
    size_t lit_capacity;
    DeferredStrip **deferred;
    size_t deferred_count, deferred_capacity;
    const NJS_CNK_VIEW *view;
    float depth_a, depth_b;
    const char *error;
} DrawWork;

static _Thread_local DrawWork *active_draw;

static unsigned word(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static int signed_word(const unsigned char *p)
{
    unsigned bits = word(p);
    return bits < 0x8000 ? (int)bits : (int)bits - 0x10000;
}

static int available(const DrawWork *draw, const void *pointer, size_t bytes)
{
    uintptr_t p = (uintptr_t)pointer;
    return p >= draw->start && p <= draw->end && bytes <= draw->end - p;
}
static int vertex_available(const DrawWork *draw,const void *pointer,size_t bytes)
{
    uintptr_t p=(uintptr_t)pointer;
    return p>=draw->vertex_start&&p<=draw->vertex_end&&bytes<=draw->vertex_end-p;
}

static const char *defer_strip(DrawWork *draw, const RxRoomStrip *strip)
{
    float sum = 0.0f;
    const float *matrix = *strip->matrix;
    for (size_t i = 0; i < strip->count; ++i) {
        const float *position = strip->vertices[i].position;
        float view_z = matrix[2] * position[0] + matrix[6] * position[1] +
                       matrix[10] * position[2] + matrix[14];
        if (!isfinite(view_z)) return "invalid Ninja translucent vertex depth";
        float depth = -draw->depth_b * (1.0f / view_z) - draw->depth_a;
        sum += fminf(fmaxf(depth, 0.0f), 65534.0f);
    }
    // Ps2AddPrim3DEx averages clamped GS depth before Ps2AddOT quantizes it.
    const unsigned bucket = (unsigned)(sum * (1.0f / (float)strip->count)) >> 4;
    if (draw->deferred_count == draw->deferred_capacity) {
        if (draw->deferred_capacity > SIZE_MAX / sizeof(*draw->deferred) / 2)
            return "Ninja translucent queue exceeds native capacity";
        size_t capacity = draw->deferred_capacity ? draw->deferred_capacity * 2 : 16;
        DeferredStrip **entries = realloc(draw->deferred, capacity * sizeof(*entries));
        if (!entries) return "cannot allocate Ninja translucent queue";
        draw->deferred = entries;
        draw->deferred_capacity = capacity;
    }
    size_t bytes = strip->count * sizeof(*strip->vertices);
    DeferredStrip *entry = malloc(sizeof(*entry) + bytes);
    if (!entry) return "cannot copy Ninja translucent strip";
    entry->strip = *strip;
    memcpy(entry->matrix, *strip->matrix, sizeof(entry->matrix));
    memcpy(entry->vertices, strip->vertices, bytes);
    entry->strip.matrix = &entry->matrix;
    entry->strip.vertices = entry->vertices;
    entry->strip.ot_bucket = bucket;
    entry->sequence = draw->deferred_count;
    draw->deferred[draw->deferred_count++] = entry;
    return NULL;
}

static int compare_deferred(const void *a, const void *b)
{
    const DeferredStrip *left = *(DeferredStrip *const *)a;
    const DeferredStrip *right = *(DeferredStrip *const *)b;
    if (left->strip.ot_bucket != right->strip.ot_bucket)
        return left->strip.ot_bucket < right->strip.ot_bucket ? -1 : 1;
    // Ps2AddOT appends each new packet to the tail of its bucket.
    return (left->sequence > right->sequence) - (left->sequence < right->sequence);
}

static unsigned vertex_clip_planes(const NJS_CNK_VIEW *view, const VU1_STRIP_BUF *vert)
{
    // ClipMatrix2 is symmetric and uses the original 4:3 half extents.
    float x = vert->fVx * view->distance;
    float y = vert->fVy * view->distance;
    float depth = fabsf(vert->fVz);
    float width = depth * view->half_width, height = depth * view->half_height;
    return (x > width ? RX_CLIP_RIGHT : 0) | (x < -width ? RX_CLIP_LEFT : 0) |
           (y > height ? RX_CLIP_BOTTOM : 0) | (y < -height ? RX_CLIP_TOP : 0) |
           (vert->fVz > view->far_z ? RX_CLIP_FAR : 0) | (vert->fVz < view->near_z ? RX_CLIP_NEAR : 0);
}

static const char *emit_packet(DrawWork *draw, const RxRoomStrip *packet)
{
    if (packet->attributes & 8) return defer_strip(draw, packet);
    return draw->submit(packet, draw->context) ? NULL : "native Ninja draw submission failed";
}

static const char *clip_triangle(DrawWork *draw, const RxRoomStrip *packet, size_t end)
{
    const size_t start = end - 2;
    const size_t indices[] = {start + (end & 1), start + !(end & 1), end};
    NJS_SCISSOR_VERTEX triangle[3], polygon[NJS_SCISSOR_MAX_VERTICES];
    for (size_t i = 0; i < 3; ++i) {
        const RxRoomVertex *vertex = &packet->vertices[indices[i]];
        NJS_POINT3 point = {vertex->position[0], vertex->position[1], vertex->position[2]};
        njCalcPoint(NULL, &point, &point);
        triangle[i] = (NJS_SCISSOR_VERTEX){
            .view = {point.x, point.y, point.z, vertex->fog},
            .uv = {vertex->uv[0], vertex->uv[1], 1, 0},
            .color = {vertex->modulation[0], vertex->modulation[1], vertex->modulation[2],
                      packet->material.alpha}
        };
    }
    const int count = njCnkScissorTriangle(draw->view, triangle, polygon);
    if (count < 0) return "invalid Ninja scissor polygon";
    if (count < 3) return NULL;

    static const NJS_MATRIX identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    RxRoomVertex vertices[NJS_SCISSOR_MAX_VERTICES];
    for (int i = 0; i < count; ++i) {
        const NJS_SCISSOR_VERTEX *vertex = &polygon[i];
        vertices[i] = (RxRoomVertex){
            .position = {vertex->view.x, vertex->view.y, vertex->view.z},
            .uv = {vertex->uv.x, vertex->uv.y},
            .modulation = {vertex->color.x, vertex->color.y, vertex->color.z},
            .fog = vertex->view.w
        };
    }
    RxRoomStrip fan = *packet;
    fan.fan = 1;
    fan.suppressed_triangles = 0;
    fan.matrix = &identity;
    fan.vertices = vertices;
    fan.count = (size_t)count;
    return emit_packet(draw, &fan);
}

static const char *submit_packet(DrawWork *draw, RxRoomStrip *packet)
{
    unsigned common = 0x3f;
    for (size_t i = 0; i < packet->count; ++i) common &= packet->vertices[i].clip_planes;
    if (common) return NULL;
    packet->suppressed_triangles = 0;
    for (size_t i = 2; i < packet->count; ++i) {
        const RxRoomVertex *v = packet->vertices + i - 2;
        unsigned a = v[0].clip_planes, b = v[1].clip_planes, c = v[2].clip_planes;
        if (!(a | b | c)) continue;
        packet->suppressed_triangles |= UINT64_C(1) << i;
        if (!(a & b & c)) {
            const char *error = clip_triangle(draw, packet, i);
            if (error) return error;
        }
    }
    // Keep the original packet's vertex depths for its ordering-table key.
    const uint64_t triangles = (UINT64_C(1) << packet->count) - 4;
    if (packet->suppressed_triangles == triangles) return NULL;
    return emit_packet(draw, packet);
}

static const char *submit_polygons(DrawWork *draw, const NJS_CNK_MODEL *model, unsigned mode)
{
    const unsigned char *vlist = (const unsigned char *)model->vlist;
    const unsigned char *p = (const unsigned char *)model->plist;
    if (!vertex_available(draw, vlist, 64) || vlist[0] != CNK_VERTEX_PS2)
        return "unsupported or truncated Ninja PS2 vertex chunk";
    unsigned first = word(vlist + 4), count = word(vlist + 6);
    size_t length = 4 + (size_t)word(vlist + 2) * 4;
    if (length < 64 + (size_t)count * 32 || !vertex_available(draw, vlist, length + 1))
        return "Ninja vertex payload exceeds its model";
    if (vlist[length] != CNK_END) return "unsupported additional Ninja vertex chunk";
    if ((uintptr_t)(vlist + 64) % _Alignof(float)) return "unaligned Ninja vertex payload";
    if(draw->validating)for(unsigned i=0;i<count;++i)for(unsigned k=0;k<3;++k){
        float p,n;memcpy(&p,vlist+64+i*32+k*4,4);memcpy(&n,vlist+80+i*32+k*4,4);
        if(!isfinite(p)||!isfinite(n))return "Nonfinite Ninja vertex data";
    }
    if (!draw->validating && count > draw->lit_capacity) {
        VU1_STRIP_BUF *lit = calloc(count, sizeof(*lit));
        if (!lit) return "cannot allocate Ninja lighting vertices";
        free(draw->lit_vertices);
        draw->lit_vertices = lit;
        draw->lit_capacity = count;
    }
    if (!draw->validating && count) {
        rx_cnk_light_vertices((const float *)(vlist + 64), draw->lit_vertices, count);
        for (unsigned i = 0; i < count; ++i) {
            VU1_STRIP_BUF *vertex = &draw->lit_vertices[i];
            vertex->fFog = njCalcFogPowerEx(1.0f / vertex->fVz);
        }
    }

    unsigned texture = 0, blend = 0;
    int have_texture = 0, have_material = 0;
    int texture_filter = -1;
    while (available(draw, p, 2)) {
        if (p[0] == CNK_END) return NULL;
        if (!available(draw, p, 4)) break;
        if (p[0] == CNK_TEXTURE) {
            unsigned selector = word(p + 2); // njCnkCtTid
            texture = selector & 0xfff;
            if (texture >= draw->texture_count) return "Ninja texture ID outside room list";
            if (!(selector & 0x2000)) texture_filter = selector >> 14;
            have_texture = 1;
            p += 4;
            continue;
        }
        size_t bytes = 4 + (size_t)word(p + 2) * 2;
        if (!available(draw, p, bytes)) return "Ninja polygon chunk exceeds its model";
        const unsigned char *end = p + bytes;
        if (p[0] == CNK_DIFFUSE_AMBIENT) {
            if (bytes != 12) return "invalid Ninja diffuse/ambient payload";
            if(!draw->validating)rx_cnk_diffuse_ambient(p + 4);
            blend = p[1];
            have_material = 1;
            p = end;
            continue;
        }
        if (p[0] != CNK_UVH || bytes < 6 || !have_texture || !have_material)
            return "unsupported Ninja polygon command";
        unsigned attributes = p[1];
        RxRoomMaterial material = rx_cnk_room_material(attributes, blend);
        if (material.color_mode != 4) return "unsupported Ninja room color mode";
        unsigned header = word(p + 4), strips = header & 0x3fff, user_words = header >> 14;
        p += 6;
        for (unsigned s = 0; s < strips; ++s) {
            if (end - p < 2) return "missing Ninja strip length";
            int signed_count = signed_word(p);
            p += 2;
            unsigned n = (unsigned)abs(signed_count);
            if (n < 3 || n * 6 + (n - 2) * 2 * user_words > (size_t)(end - p))
                return "Ninja strip exceeds its polygon chunk";
            if (!draw->validating && n > draw->vertex_capacity) {
                RxRoomVertex *vertices = realloc(draw->vertices, n * sizeof(*vertices));
                if (!vertices) return "cannot allocate Ninja strip vertices";
                draw->vertices = vertices;
                draw->vertex_capacity = n;
            }
            for (unsigned i = 0; i < n; ++i) {
                unsigned index = word(p);
                if (index < first || index - first >= count) return "Ninja strip references a missing vertex";
                if(draw->validating){p+=6+(i>=2?user_words*2:0);continue;}
                const unsigned char *vertex = vlist + 64 + (size_t)(index - first) * 32;
                RxRoomVertex *out = &draw->vertices[i];
                memcpy(out->position, vertex, sizeof(out->position));
                memcpy(out->normal, vertex + 16, sizeof(out->normal));
                const VU1_STRIP_BUF *light = &draw->lit_vertices[index - first];
                out->incident[0] = light->fIr;
                out->incident[1] = light->fIg;
                out->incident[2] = light->fIb;
                out->fog = light->fFog;
                for (size_t channel = 0; channel < 3; ++channel) {
                    float lit = out->incident[channel] + material.ambient[channel];
                    out->modulation[channel] = fminf(fmaxf(lit, 0), 1) * material.diffuse[channel];
                }
                out->clip_planes = vertex_clip_planes(draw->view, light);
                // Match the original VU's zero extension and UVH scale.
                out->uv[0] = word(p + 2) * 0.0009775171f;
                out->uv[1] = word(p + 4) * 0.0009775171f;
                p += 6 + (i >= 2 ? user_words * 2 : 0);
            }
            if(draw->validating)continue;
            RxRoomStrip packet = {
                .model = model, .matrix = pNaMatMatrixStuckPtr, .vertices = draw->vertices,
                .count = n, .texture = texture, .attributes = attributes, .mode = mode,
                .texture_filter = texture_filter, .clip_flag = signed_count < 0,
                .material = material
            };
            // Ninja cuts long strips into 60-vertex packets with two shared vertices.
            for (unsigned offset = 0; ; offset += 58) {
                unsigned remaining = n - offset;
                packet.vertices = draw->vertices + offset;
                packet.count = remaining > 60 ? 60 : remaining;
                const char *error = submit_packet(draw, &packet);
                if (error) return error;
                if (remaining <= 60) break;
            }
        }
        // Odd word counts can be padded to a 32-bit boundary.
        if (end - p == 2 && bytes % 4 == 0 && !word(p)) p += 2;
        if (p != end) return "unexpected data after Ninja strips";
    }
    return "unterminated Ninja polygon list";
}

int rx_cnk_push_matrix(void)
{
    DrawWork *draw = active_draw;
    if (!draw || draw->error) return 0;
    if (njPushMatrixEx()) return 1;
    draw->error = "native Ninja room matrix stack exhausted";
    return 0;
}

int rx_cnk_draw_model(const NJS_CNK_MODEL *model, unsigned mode)
{
    DrawWork *draw = active_draw;
    if (!draw || !model || draw->error) return 0;
    if (!njCnkModelInView(model, draw->view)) return 0;
    draw->error = submit_polygons(draw, model, mode);
    return !draw->error;
}

static const char *finish_draw(DrawWork *draw)
{
    if (!draw->error && draw->deferred_count) {
        qsort(draw->deferred, draw->deferred_count, sizeof(*draw->deferred), compare_deferred);
        for (size_t i = 0; i < draw->deferred_count; ++i) {
            if (!draw->submit(&draw->deferred[i]->strip, draw->context)) {
                draw->error = "native Ninja translucent draw submission failed";
                break;
            }
        }
    }
    active_draw = NULL;
    for (size_t i = 0; i < draw->deferred_count; ++i) free(draw->deferred[i]);
    free(draw->deferred);
    free(draw->lit_vertices);
    free(draw->vertices);
    return draw->error;
}

const char *rx_draw_room_chunks(ML_WORK *model, size_t bytes, int multi,
                               const NJS_CNK_VIEW *view,
                               RxRoomSubmit submit, void *context)
{
    if (active_draw || !model || !model->objP || !model->texP || !model->datP || !submit ||
        !bytes || bytes > UINTPTR_MAX - (uintptr_t)model->datP || !view ||
        !isfinite(view->near_z) || !isfinite(view->far_z) || view->near_z <= 0 || view->far_z <= view->near_z ||
        !isfinite(view->distance) || view->distance <= 0 ||
        !isfinite(view->half_width) || view->half_width <= 0 ||
        !isfinite(view->half_height) || view->half_height <= 0 ||
        !isfinite(view->aspect_x) || view->aspect_x <= 0 ||
        !isfinite(view->aspect_y) || view->aspect_y <= 0)
        return "invalid native Ninja room draw";
    DrawWork draw = {
        .start = (uintptr_t)model->datP, .end = (uintptr_t)model->datP + bytes,
        .vertex_start=(uintptr_t)model->datP,.vertex_end=(uintptr_t)model->datP+bytes,
        .texture_count = model->texP->nbTexture, .submit = submit, .context = context, .view = view
    };
    Ps2DepthCoefficients(-view->near_z, -view->far_z, &draw.depth_a, &draw.depth_b);
    if (!isfinite(draw.depth_a) || !isfinite(draw.depth_b)) return "invalid Ninja room depth coefficients";
    active_draw = &draw;
    if (multi) njCnkEasyMultiDrawObject(model->objP);
    else njCnkEasyDrawObject(model->objP);
    return finish_draw(&draw);
}
static int span_valid(RxDrawSpan span)
{return span.data&&span.bytes&&span.bytes<=UINTPTR_MAX-(uintptr_t)span.data;}
static void part_bounds(DrawWork *draw,const RxRoomDrawPart *part)
{
    draw->start=(uintptr_t)part->polygons.data;draw->end=draw->start+part->polygons.bytes;
    draw->vertex_start=(uintptr_t)part->vertices.data;draw->vertex_end=draw->vertex_start+part->vertices.bytes;
}
const char *rx_draw_chunk_parts(const RxRoomDrawPart *parts,size_t count,const NJS_TEXLIST *textures,int multi,
                                const NJS_CNK_VIEW *view,RxRoomSubmit submit,void *context)
{
    if(active_draw||(!parts&&count)||count>4096||!textures||!submit||!view||
       !isfinite(view->near_z)||!isfinite(view->far_z)||view->near_z<=0||view->far_z<=view->near_z||
       !isfinite(view->distance)||view->distance<=0||!isfinite(view->half_width)||view->half_width<=0||
       !isfinite(view->half_height)||view->half_height<=0||!isfinite(view->aspect_x)||view->aspect_x<=0||
       !isfinite(view->aspect_y)||view->aspect_y<=0||!pNaMatMatrixStuckPtr)return "Invalid native Ninja split draw";
    DrawWork draw={.texture_count=(unsigned)textures->nbTexture,.submit=submit,.context=context,.view=view,.validating=1};
    Ps2DepthCoefficients(-view->near_z,-view->far_z,&draw.depth_a,&draw.depth_b);
    if(!isfinite(draw.depth_a)||!isfinite(draw.depth_b))return "Invalid Ninja split depth coefficients";
    for(size_t i=0;i<count;++i){const RxRoomDrawPart *part=parts+i;
        if(!part->model||!part->matrix||!span_valid(part->vertices)||!span_valid(part->polygons))return "Invalid Ninja packet span";
        for(unsigned k=0;k<16;++k)if(!isfinite(((const float *)part->matrix)[k]))return "Nonfinite Ninja part matrix";
        if(!isfinite(part->model->center.x)||!isfinite(part->model->center.y)||!isfinite(part->model->center.z)||!isfinite(part->model->r))
            return "Nonfinite Ninja model bounds";
        part_bounds(&draw,part);const char *error=submit_polygons(&draw,part->model,multi?1:0);if(error)return error;
    }
    NJS_MATRIX before;njGetMatrix(&before);draw.validating=0;active_draw=&draw;
    for(size_t i=0;i<count&&!draw.error;++i){part_bounds(&draw,parts+i);njSetMatrix(NULL,(NJS_MATRIX *)parts[i].matrix);
        if(multi)njCnkEasyMultiDrawModel((NJS_CNK_MODEL *)parts[i].model);
        else njCnkEasyDrawModel((NJS_CNK_MODEL *)parts[i].model);
    }
    njSetMatrix(NULL,&before);return finish_draw(&draw);
}
