#include "opening.h"
#include "room.h"

#include "camera.h"
#include "ps2_NaView.h"
#include "ps2_dummy.h"
#include "gs/gs.h"
#include "light.h"
#include "MdlPut.h"
#include "main.h"
#include "ps2_NaMatrix.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_dummy.h"
#include "room_engine.h"
#include "texture_host.h"
#include "vu0.h"
#include "weapon.h"
#include "effect_runtime.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

int gs_replaying;
static int unsupported_gs_draw;

_Static_assert(offsetof(O_WRK, mlwP) == offsetof(BH_PWORK, mlwP) &&
               offsetof(O_WRK, mtx) == offsetof(BH_PWORK, mtx) &&
               offsetof(O_WRK, lkono) == offsetof(BH_PWORK, lkono),
               "Weapon objects must preserve the original shared actor prefix");

typedef struct {
    RxOpening *opening;
    unsigned texture_start, texture_count;
    GLuint *cache;
} OpeningDraw;

void prof_push(int category) { (void)category; }
void prof_pop(void) {}

void vu1_xgkick(const VuVec *memory, uint32_t quadword, uint32_t mask)
{
    (void)memory; (void)quadword; (void)mask;
    unsupported_gs_draw = 1;
}

// Room strips are submitted immediately through the GLES renderer below.
// The GS is used for texture memory transfers and texture register decoding.
void gs_flush(void) {}
int gs_batch_two_pass(void) { return 0; }
void ntex_before_access(unsigned start, unsigned end) { (void)start; (void)end; }
void gs_notify_pages(const uint64_t *pages, int changed) { (void)pages; (void)changed; }
void stream_note_upload(const uint64_t *pages) { (void)pages; }
void gs_notify_mem_write(uint32_t bp, uint32_t bw, int psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h;
}
void gs_draw_primitive(int type, const GsVertex *v, int count, const GsContext *ctx, uint64_t prim)
{
    (void)type; (void)v; (void)count; (void)ctx; (void)prim;
    unsupported_gs_draw = 1;
}

int rx_texture_transfer_at(uint32_t block, unsigned stride, unsigned format,
                        unsigned x, unsigned y, unsigned width, unsigned height,
                        const void *source, size_t bytes)
{
    if (!source || !stride || stride >= 64 || block >= 0x4000 ||
        !width || width >= 0x1000 || !height || height >= 0x1000 ||
        x >= 0x800 || y >= 0x800 || format >= 64 || bytes > UINT32_MAX) return -1;
    gs_exec_reg(0x50, (uint64_t)block << 32 | (uint64_t)stride << 48 | (uint64_t)format << 56);
    gs_exec_reg(0x51, (uint64_t)x << 32 | (uint64_t)y << 48);
    gs_exec_reg(0x52, (uint64_t)height << 32 | width);
    gs_exec_reg(0x53, 0);
    gs_exec_upload(source, (uint32_t)bytes);
    return gs.xfer_active ? -1 : 0;
}

int rx_texture_transfer(uint32_t block, unsigned stride, unsigned format,
                        unsigned y, unsigned width, unsigned height,
                        const void *source, size_t bytes)
{
    return rx_texture_transfer_at(block,stride,format,0,y,width,height,source,bytes);
}

void rx_texture_register(unsigned reg, uint64_t value)
{
    gs_exec_reg((int)reg, value);
}

void rx_texture_capture(void *pixels, unsigned width, unsigned height)
{
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static GLuint shader(GLenum type, const char *text)
{
    GLuint name = glCreateShader(type);
    if (!name) return 0;
    glShaderSource(name, 1, &text, NULL);
    glCompileShader(name);
    GLint status;
    glGetShaderiv(name, GL_COMPILE_STATUS, &status);
    if (status) return name;
    glDeleteShader(name);
    return 0;
}

void rx_opening_close_graphics(RxOpening *opening)
{
    rx_particle_close(&opening->particles);
    rx_shadow_close(&opening->shadows);
    if(opening->actor_textures){glDeleteTextures(opening->actor_texture_count,opening->actor_textures);free(opening->actor_textures);opening->actor_textures=NULL;opening->actor_texture_count=0;}
    if (!opening->program) return;
    glDeleteTextures(opening->texture_count, opening->textures);
    for (unsigned i = 0; i < opening->texture_count; ++i) opening->textures[i] = 0;
    glDeleteBuffers(1, &opening->elements);
    glDeleteBuffers(1, &opening->vertices);
    glDeleteVertexArrays(1, &opening->vao);
    glDeleteProgram(opening->program);
    opening->program = opening->vertices = opening->elements = opening->vao = 0;
}

static int open_graphics(RxOpening *opening)
{
    const char *vs =
        "#version 300 es\n"
        "layout(location=0) in vec3 position; layout(location=1) in vec2 coords;\n"
        "layout(location=2) in vec3 modulation; out vec3 shade; out vec2 uv;\n"
        "uniform mat4 modelView; uniform vec4 projection;\n"
        "void main() { vec4 p=modelView*vec4(position,1.0);"
        "gl_Position=vec4(projection.xy*vec2(p.x,-p.y),projection.z*p.z+projection.w,p.z);"
        "uv=coords; shade=floor(modulation*128.0)/128.0; }";
    const char *fs =
        "#version 300 es\n"
        "precision highp float; uniform highp sampler2D image; in vec2 uv; out vec4 color;\n"
        "in vec3 shade; uniform float materialAlpha; uniform bool textureAlpha;\n"
        "void main() { vec4 texel=texture(image,uv);"
        "float alpha=floor(materialAlpha*(textureAlpha ? texel.a*255.0 : 128.0)+0.0001);"
        "if (alpha <= 0.0) discard;"
        "color=vec4(floor(texel.rgb*255.0*shade+0.0001)/255.0,alpha/128.0); }";
    GLuint vertex = shader(GL_VERTEX_SHADER, vs), fragment = shader(GL_FRAGMENT_SHADER, fs);
    GLint linked = 0;
    if (vertex && fragment) {
        opening->program = glCreateProgram();
        glAttachShader(opening->program, vertex);
        glAttachShader(opening->program, fragment);
        glLinkProgram(opening->program);
        glGetProgramiv(opening->program, GL_LINK_STATUS, &linked);
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (!linked) {
        rx_opening_close_graphics(opening);
        return 0;
    }
    opening->matrix_uniform = glGetUniformLocation(opening->program, "modelView");
    opening->projection_uniform = glGetUniformLocation(opening->program, "projection");
    opening->material_alpha_uniform = glGetUniformLocation(opening->program, "materialAlpha");
    opening->texture_alpha_uniform = glGetUniformLocation(opening->program, "textureAlpha");
    glGenVertexArrays(1, &opening->vao);
    glBindVertexArray(opening->vao);
    glGenBuffers(1, &opening->vertices);
    glBindBuffer(GL_ARRAY_BUFFER, opening->vertices);
    glGenBuffers(1, &opening->elements);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, opening->elements);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RxRoomVertex), NULL);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RxRoomVertex), (void*)offsetof(RxRoomVertex, uv));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(RxRoomVertex), (void*)offsetof(RxRoomVertex, modulation));
    for (unsigned i = 0; i < 3; ++i) glEnableVertexAttribArray(i);
    glBindVertexArray(0);
    gs_reset();
    Ps2_current_texbreak = 1;
    return glGetError() == GL_NO_ERROR;
}

static int draw_strip(const RxRoomStrip *strip, void *context)
{
    const OpeningDraw *draw = context;
    RxOpening *opening = draw->opening;
    if (strip->texture >= draw->texture_count || strip->count > 64) return 0;
    if (njSetTextureNum(strip->texture) < 0) return 0;
    const GsContext *state = &gs.ctx[0];
    uint64_t tex0 = state->tex0;
    GLuint *texture = &(draw->cache?draw->cache:opening->textures)[draw->texture_start + strip->texture];
    if (!*texture) {
        unsigned width = 1u << ((tex0 >> 26) & 15), height = 1u << ((tex0 >> 30) & 15);
        if (width > 1024 || height > 1024) return 0;
        uint32_t *pixels = malloc((size_t)width * height * sizeof(*pixels));
        if (!pixels) return 0;
        gs_decode_texture(tex0, gs.texa, width, height, pixels);
        glGenTextures(1, texture);
        glBindTexture(GL_TEXTURE_2D, *texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        free(pixels);
    } else {
        glBindTexture(GL_TEXTURE_2D, *texture);
    }
    int translucent = (strip->attributes & 8) != 0;
    if (translucent && strip->material.gs_alpha != 0x44) return 0;
    if (translucent) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glDepthMask(!translucent);
    GLint filter = strip->texture_filter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state->clamp & 3 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (state->clamp >> 2) & 3 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glUniform1f(opening->material_alpha_uniform, strip->material.alpha);
    glUniform1i(opening->texture_alpha_uniform, (tex0 >> 34) & 1);
    glUniformMatrix4fv(opening->matrix_uniform, 1, GL_FALSE, *strip->matrix);
    glBufferData(GL_ARRAY_BUFFER, strip->count * sizeof(*strip->vertices), strip->vertices, GL_STREAM_DRAW);
    if (!strip->suppressed_triangles) {
        glDrawArrays(strip->fan ? GL_TRIANGLE_FAN : GL_TRIANGLE_STRIP, 0, strip->count);
    } else {
        GLushort indices[62 * 3];
        GLsizei count = 0;
        for (GLushort i = 2; i < strip->count; ++i) {
            if (strip->suppressed_triangles & (UINT64_C(1) << i)) continue;
            indices[count++] = i - 2 + (i & 1);
            indices[count++] = i - 2 + !(i & 1);
            indices[count++] = i;
        }
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(*indices), indices, GL_STREAM_DRAW);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, NULL);
    }
    return !unsupported_gs_draw && glGetError() == GL_NO_ERROR;
}

const char *rx_opening_present(RxOpening *opening)
{
    const char *equipment_error=rx_opening_sync_weapon(opening);
    if(equipment_error)return equipment_error;
    if (!opening->program && !open_graphics(opening)) return "Could not start the GLES room renderer";
    unsupported_gs_draw = 0;
    if (!njSetTexture(rom->mdl.texP)) return "Could not select starting-room textures";
    bhControlCamera();
    bhControlLight();
    const char *shadow_error=rx_effects_tick();
    if (shadow_error) return shadow_error;
    njSetMatrix(NULL, cam.mtx);
    float distance = 320.0f / tanf((cam.pers / 2) * 0.0000958738f);
    const NJS_CNK_VIEW view = {_fNaViwClipNear, _fNaViwClipFar, distance, 320, 240, BHD_ASPECT_X, BHD_ASPECT_Y};
    if(!isfinite(view.near_z)||!isfinite(view.far_z)||view.near_z<=0||view.far_z<=view.near_z)
        return "Invalid original camera clip range";
    uint64_t background=Db.clear0.rgbaq;
    glClearColor((background&255)/255.0f,((background>>8)&255)/255.0f,((background>>16)&255)/255.0f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    float range = view.far_z - view.near_z;
    glUseProgram(opening->program);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(opening->vao);
    glBindBuffer(GL_ARRAY_BUFFER, opening->vertices);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(opening->projection_uniform, distance * view.aspect_x / view.half_width,
                distance * view.aspect_y / view.half_height,
                (view.far_z + view.near_z) / range, -2 * view.far_z * view.near_z / range);
    OpeningDraw room = {opening, 0, opening->room_texture_count, NULL};
    const char *error = bhDrawNativeRoom(&view, draw_strip, &room);
    if (!error && !(plp->stflg & 0x1000000) && !(plp->mdflg & 1)) {
        NJS_TEXLIST *textures = rx_engine_player_textures(opening->player, NULL);
        if (!njSetTexture(textures)) {
            error = "Could not select Claire's original textures";
        } else {
            unsigned ambient = rom->amb_chr;
            njCnkSetEasyMultiAmbient(rom->amb_r[ambient], rom->amb_g[ambient], rom->amb_b[ambient]);
            njSetMatrix(NULL, cam.mtx);
            njTranslate(NULL, plp->px, plp->py, plp->pz);
            if(!rx_engine_player_world_pose(opening->player))njRotateY(NULL, plp->ay);
            OpeningDraw player = {opening, opening->room_texture_count, textures->nbTexture, NULL};
            error = rx_engine_player_draw(opening->player, &view, draw_strip, &player);
            unsigned texture_start = opening->room_texture_count + textures->nbTexture;
            for (unsigned side = 0; !error && side < 2; ++side) {
                O_WRK *object = &sys->obwp[side];
                textures = rx_engine_weapon_textures(opening->weapon, side, NULL);
                if (!textures) continue;
                if ((object->flg & 1) && !(object->stflg & 0x1000000) &&
                    !(object->mdflg & 1) && object->mlwP->objP) {
                    bhObjWpn((BH_PWORK *)object);
                    bhCalcModel((BH_PWORK *)object);
                    if (!njSetTexture(textures)) {
                        error = "Could not select original equipped-hand textures";
                        break;
                    }
                    njSetMatrix(NULL, cam.mtx);
                    if(!rx_engine_player_world_pose(opening->player)){
                        njTranslate(NULL, plp->px, plp->py, plp->pz);njRotateY(NULL, plp->ay);
                    }
                    njMultiMatrix(NULL, object->mtx);
                    OpeningDraw hand = {opening, texture_start, textures->nbTexture, NULL};
                    error = rx_engine_weapon_draw(opening->weapon, side, &view, draw_strip, &hand);
                }
                texture_start += textures->nbTexture;
            }
        }
    }
    if(!error&&opening->events_ready){
        error=bhPublishNativeRoomActors();
        unsigned total=0;
        for(size_t i=0;i<bhNativeRoomActorCount();++i){NJS_TEXLIST *list=bhNativeRoomActorTextures(i);if(list)total+=list->nbTexture;}
        if(!error&&!opening->actor_textures&&total){opening->actor_textures=calloc(total,sizeof(GLuint));
            if(!opening->actor_textures)error="Could not allocate original room actor texture cache";
            else opening->actor_texture_count=total;}
        if(!error&&total!=opening->actor_texture_count)error="Original room actor texture cache changed owner";
        unsigned start=0;
        for(size_t i=0;!error&&i<bhNativeRoomActorCount();++i){NJS_TEXLIST *list=bhNativeRoomActorTextures(i);if(!list)continue;
            if(!njSetTexture(list)){error="Could not select original room actor textures";break;}
            unsigned ambient=rom->amb_chr;
            njCnkSetEasyMultiAmbient(rom->amb_r[ambient],rom->amb_g[ambient],rom->amb_b[ambient]);
            OpeningDraw actor={opening,start,list->nbTexture,opening->actor_textures};
            error=bhDrawNativeRoomActor(i,cam.mtx,&view,draw_strip,&actor);start+=list->nbTexture;
        }
    }
    if (!error) error=rx_shadow_present(opening->effects,&opening->shadows,&view);
    if (!error) error=rx_particle_present(&opening->particles,&view);
    if (!error) error=rx_overlay_present(&opening->particles,&view);
    if (!error) error=rx_callback_present(&opening->particles,&view);
    if (!error) error=rx_message_present(&opening->particles);
    if (!error) error=rx_fade_present(&opening->particles);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    return error;
}
