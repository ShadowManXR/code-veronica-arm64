#include "texture_host.h"
#include "ps2_loadtim2.h"
#include "ps2_NaTextureFunction.h"
#include "gs/gs.h"
#include "camera.h"
#include "cut.h"
#include "light.h"
#include "main.h"
#include "ps2_dummy.h"
#include "ps2_NaMath.h"
#include "ps2_NaFog.h"
#include "ps2_NaMatrix.h"
#include "room_engine.h"
#include "ps2_Vu1Scissor2.h"
#include "sync.h"

#include "android_egl.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A headless GS memory instance for this test. This path has no pending
   draws or cached GS textures to flush or invalidate. */
int gs_replaying;
extern long vu_check_mismatches(void);
void gs_flush(void) {}
int gs_batch_two_pass(void) { return 0; }
void ntex_before_access(unsigned start, unsigned end) { (void)start; (void)end; }
void gs_notify_pages(const uint64_t *pages, int changed) { (void)pages; (void)changed; }
void stream_note_upload(const uint64_t *pages) { (void)pages; }
void gs_notify_mem_write(uint32_t bp, uint32_t bw, int psm, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    (void)bp; (void)bw; (void)psm; (void)x; (void)y; (void)w; (void)h;
}

static unsigned transfers, last_transfer_format;
static unsigned target_width, target_height, draws;
static GLuint program, input_texture;
static uint32_t *decoded;

static void check(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int rx_texture_transfer_at(uint32_t block, unsigned stride, unsigned format,
                        unsigned x, unsigned y, unsigned width, unsigned height,
                        const void *source, size_t bytes)
{
    if (!source || !stride || stride >= 64 || block >= 0x4000 ||
        !width || width >= 0x1000 || !height || height >= 0x1000 ||
        x >= 0x800 || y >= 0x800 || format >= 64 || bytes > UINT32_MAX)
        return -1;
    gs_exec_reg(0x50, (uint64_t)block << 32 | (uint64_t)stride << 48 | (uint64_t)format << 56);
    gs_exec_reg(0x51, (uint64_t)x << 32 | (uint64_t)y << 48);
    gs_exec_reg(0x52, (uint64_t)height << 32 | width);
    gs_exec_reg(0x53, 0);
    gs_exec_upload(source, (uint32_t)bytes);
    ++transfers;
    last_transfer_format = format;
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
    check(width == target_width && height == target_height && draws,
          "framebuffer capture requires a completed 512x512 draw");
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(glGetError() == GL_NO_ERROR, "Quest framebuffer capture failed");
}

/* Only the textured sprite used by this offscreen check enters this GLES
   backend. The existing GS code assembles its two vertices and draw state. */
void gs_draw_primitive(int type, const GsVertex *v, int count, const GsContext *ctx, uint64_t prim)
{
    check(type == 6 && count == 2 && (prim & 0x110) == 0x110 && decoded,
          "unexpected GS primitive in texture check");
    unsigned w = 1u << ((ctx->tex0 >> 26) & 15), h = 1u << ((ctx->tex0 >> 30) & 15);
    check(w == target_width && h == target_height && ctx->tex1 && ctx->test,
          "original texture state did not reach the GS drawing context");
    gs_decode_texture(ctx->tex0, gs.texa, w, h, decoded);
    glBindTexture(GL_TEXTURE_2D, input_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, decoded);
    glUseProgram(program);
    glUniform4f(glGetUniformLocation(program, "bounds"),
                2.0f * v[0].x / w - 1, 2.0f * v[0].y / h - 1,
                2.0f * v[1].x / w - 1, 2.0f * v[1].y / h - 1);
    glUniform4f(glGetUniformLocation(program, "coords"),
                v[0].u / w, v[0].v / h, v[1].u / w, v[1].v / h);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    ++draws;
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    check(ok, "cannot compile texture check shader");
    return shader;
}

static GLuint link_program(const char *vertex_source, const char *fragment_source)
{
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLuint linked_program = glCreateProgram();
    glAttachShader(linked_program, vertex);
    glAttachShader(linked_program, fragment);
    glLinkProgram(linked_program);
    GLint linked = 0;
    glGetProgramiv(linked_program, GL_LINK_STATUS, &linked);
    check(linked, "cannot link GLES check shader");
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return linked_program;
}

static void draw_sprite(unsigned w, unsigned h)
{
    gs_exec_reg(0x00, 6 | 0x110); // Sprite, texture mapping and fixed UV.
    gs_exec_reg(0x03, 0);
    gs_exec_reg(0x05, 0);
    gs_exec_reg(0x03, (uint64_t)(h * 16) << 16 | (w * 16));
    gs_exec_reg(0x05, (uint64_t)(h * 16) << 16 | (w * 16));
}

static void check_pixels(const TIM2_PICTUREHEADER_EX *disc, const uint32_t *pixels)
{
    const unsigned char *indices = (const unsigned char *)disc + 128 + disc->HeaderSize;
    const unsigned char *palette = indices + disc->ImageSize;
    size_t count = (size_t)disc->ImageWidth * disc->ImageHeight;
    check(disc->FormatId == 1 && disc->ImageType == 5 && disc->ClutColors == 256 &&
          disc->ClutSize >= 256 * 4 && count == disc->ImageSize, "unexpected original indexed texture");
    for (size_t i = 0; i < count; ++i) {
        unsigned index = indices[i];
        index = (index & ~0x18u) | ((index & 8) << 1) | ((index & 16) >> 1);
        const unsigned char *color = palette + index * 4;
        uint32_t rgb = (uint32_t)color[0] | (uint32_t)color[1] << 8 | (uint32_t)color[2] << 16;
        if ((pixels[i] & 0xffffff) != rgb) {
            fprintf(stderr, "TIM2 %u texel %zu: %06x expected %06x\n",
                    disc->Gindex, i, pixels[i] & 0xffffff, rgb);
            check(0, "original GS transfer differs from disc RGB");
        }
    }
}

void check_texture_gpu(NJS_TEXLIST *list, unsigned index, const TIM2_PICTUREHEADER_EX *disc)
{
    unsigned w = disc->ImageWidth, h = disc->ImageHeight;
    size_t bytes = (size_t)w * h * 4;
    uint32_t *pixels = malloc(bytes), *readback = malloc(bytes);
    check(pixels && readback, "cannot allocate texture readback");

    RxEgl target = {0};
    check(rx_egl_open(&target, NULL, 1, 1), "cannot create Quest GLES pbuffer");

    GLuint output_texture, framebuffer;
    glGenTextures(1, &output_texture);
    glBindTexture(GL_TEXTURE_2D, output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output_texture, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "cannot create offscreen texture draw target");
    glViewport(0, 0, w, h);
    glGenTextures(1, &input_texture);
    glBindTexture(GL_TEXTURE_2D, input_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const char *vertex_source = "#version 300 es\n"
        "uniform vec4 bounds; uniform vec4 coords; out vec2 uv;\n"
        "void main() { vec2 corner=vec2(gl_VertexID & 1, gl_VertexID >> 1);"
        "gl_Position=vec4(mix(bounds.xy,bounds.zw,corner),0.0,1.0);"
        "uv=mix(coords.xy,coords.zw,corner); }";
    const char *fragment_source = "#version 300 es\n"
        "precision highp float; uniform highp sampler2D image; in vec2 uv; out vec4 color;\n"
        "void main() { color=texture(image,uv); }";
    program = link_program(vertex_source, fragment_source);

    target_width = w;
    target_height = h;
    draws = transfers = 0;
    decoded = pixels;
    gs_reset();
    Ps2_current_texbreak = 1;
    unsigned expected_format = w > 128 && h > 128 ? PSMCT32 : PSMT8;
    check(njSetTexture(list) && njSetTextureNum(index) == 1 && transfers == 2 &&
          last_transfer_format == expected_format, "original texture selection did not upload image and palette");
    check(njSetTextureNum(index) == 0 && transfers == 2, "original repeated texture bind uploaded again");
    const NJS_TEXMEMLIST *selected = Ps2_current_texmemlist;
    check(njSetTextureNumG(disc->Gindex) == 0 && Ps2_current_texmemlist == selected &&
          njSetTextureNumG(UINT32_MAX) == -1, "global texture lookup changed the active selection");
    NJS_TEXNAME invalid = {.texaddr = 1};
    NJS_TEXLIST unloaded = {&invalid, 1};
    check(njSetTexture(&unloaded) && njSetTextureNum(0) == -1 &&
          Ps2_current_texmemlist == selected && transfers == 2,
          "an unloaded texture slot changed the active GS binding");
    njSetTexture(list);
    draw_sprite(w, h);
    check(draws == 1, "GS did not assemble the textured sprite");
    check_pixels(disc, pixels);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    check(glGetError() == GL_NO_ERROR && !memcmp(pixels, readback, bytes),
          "Quest GLES sampled pixels differ");

    if (index == 0 && w == 512 && h == 512) {
        NJS_TEXINFO info = {0};
        NJS_TEXNAME name;
        NJS_TEXLIST captured = {&name, 1};
        njSetTextureInfo(&info, NULL, 0xb00, w, h);
        njSetTextureName(&name, &info, 200000, 0x40800000);
        check(njLoadTexture(&captured) == 1, "original framebuffer texture allocation failed");
        const NJS_TEXMEMLIST *entry = (const NJS_TEXMEMLIST *)name.texaddr;
        const TIM2_PICTUREHEADER_EX *picture = (const TIM2_PICTUREHEADER_EX *)entry->texinfo.texsurface.pSurface;
        check((uintptr_t)picture > UINT32_MAX && picture->Gindex == 200000 &&
              Ps2GetTim2Size((void *)picture) == 256 + bytes &&
              !memcmp((const unsigned char *)picture + 256, readback, bytes),
              "captured TIM2 header or framebuffer pixels differ");
        // The original RGBA32 path splits 512 rows into 255, 255 and 2.
        check(njSetTexture(&captured) && njSetTextureNum(0) == 1 && transfers == 5 &&
              last_transfer_format == PSMCT32, "original engine did not bind captured texture");
        glClearColor(1, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        draw_sprite(w, h);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, readback);
        check(draws == 2 && glGetError() == GL_NO_ERROR &&
              !memcmp((const unsigned char *)picture + 256, readback, bytes),
              "captured texture did not redraw the original frame");
        check(njReleaseTexture(&captured) == 1, "captured texture release failed");
        puts("PASS original 512x512 framebuffer capture, texture rebind and redraw");
    }
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    check(renderer && strstr(renderer, "Adreno"), "GLES check did not run on Quest GPU");
    printf("PASS original texture selection and sampled GS sprite: TIM2 %u, %ux%u on %s\n",
           disc->Gindex, w, h, renderer);

    njSetTexture(list);
    decoded = NULL;
    glDeleteProgram(program);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &output_texture);
    glDeleteTextures(1, &input_texture);
    rx_egl_close(&target);
    free(readback);
    free(pixels);
}

typedef struct {
    GLuint name;
    unsigned has_alpha, max_alpha;
    GLint wrap_s, wrap_t;
} RoomTexture;

typedef struct {
    RoomTexture *textures;
    GLuint elements;
    GLint matrix, projection, alpha, texture_alpha, fog_enabled, fog_color;
    unsigned strips, fans, vertices, textured_slots, clipped, blended, dark_material, lit;
    unsigned last_bucket;
    int actor;
} RoomDraw;

static void set_room_fog(const RoomDraw *draw)
{
    uint64_t color = gs.fogcol;
    glUniform1i(draw->fog_enabled, 1);
    glUniform3f(draw->fog_color, color & 255, (color >> 8) & 255, (color >> 16) & 255);
}

static void clear_room_frame(RoomDraw *draw)
{
    draw->strips = draw->fans = draw->vertices = draw->clipped = 0;
    draw->blended = draw->dark_material = draw->lit = draw->last_bucket = 0;
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void set_room_projection(const RoomDraw *draw, const NJS_CNK_VIEW *view)
{
    float range = view->far_z - view->near_z;
    glUniform4f(draw->projection, view->distance * view->aspect_x / view->half_width,
                view->distance * view->aspect_y / view->half_height,
                (view->far_z + view->near_z) / range, -2 * view->far_z * view->near_z / range);
}

static const NJS_CNK_VIEW test_view = {2, 20000, 1, 1, 1, 1, 1};

static void draw_room_mesh(const RxRoomStrip *strip, const RoomDraw *draw, const RxRoomVertex *vertices)
{
    glUniformMatrix4fv(draw->matrix, 1, GL_FALSE, *strip->matrix);
    glBufferData(GL_ARRAY_BUFFER, strip->count * sizeof(*vertices), vertices, GL_STREAM_DRAW);
    if (!strip->suppressed_triangles) {
        glDrawArrays(strip->fan ? GL_TRIANGLE_FAN : GL_TRIANGLE_STRIP, 0, (GLsizei)strip->count);
        return;
    }

    GLushort indices[58 * 3];
    GLsizei count = 0;
    for (GLushort i = 2; i < strip->count; ++i) {
        if (strip->suppressed_triangles & (UINT64_C(1) << i)) continue;
        indices[count++] = i - 2 + (i & 1);
        indices[count++] = i - 2 + !(i & 1);
        indices[count++] = i;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, draw->elements);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (size_t)count * sizeof(*indices), indices, GL_STREAM_DRAW);
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, NULL);
}

static int draw_room_strip(const RxRoomStrip *strip, void *context)
{
    RoomDraw *draw = context;
    const int translucent = (strip->attributes & 8) != 0;
    if (translucent) {
        check(strip->ot_bucket >= draw->last_bucket, "opening translucent depth order differs");
        draw->last_bucket = strip->ot_bucket;
    }
    if (translucent && !draw->blended) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    if (!translucent && draw->blended) return 0;
    const RxRoomMaterial *material = &strip->material;
    // The opening uses color calculation 4 (diffuse plus ambient) and
    // GS ALPHA 0x44, (source - destination) * sourceAlpha + destination.
    if (material->color_mode != 4 || (translucent && material->gs_alpha != 0x44) ||
        (strip->texture_filter != 0 && strip->texture_filter != 1))
        return 0;
    RoomTexture *texture = &draw->textures[strip->texture];
    if (!texture->name) {
        if (njSetTextureNum(strip->texture) < 0) return 0;
        const GsContext *state = &gs.ctx[0];
        uint64_t tex0 = state->tex0;
        unsigned wrap_s = state->clamp & 3, wrap_t = (state->clamp >> 2) & 3;
        unsigned width = 1u << ((tex0 >> 26) & 15);
        unsigned height = 1u << ((tex0 >> 30) & 15);
        if (width > 1024 || height > 1024 || ((tex0 >> 35) & 3) || wrap_s > 1 || wrap_t > 1 ||
            (state->test & 0x3fff) != 13)
            return 0;
        uint32_t *pixels = malloc((size_t)width * height * sizeof(*pixels));
        if (!pixels) return 0;
        gs_decode_texture(tex0, gs.texa, width, height, pixels);
        texture->has_alpha = (tex0 >> 34) & 1;
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            unsigned alpha = pixels[i] >> 24;
            if (alpha > texture->max_alpha) texture->max_alpha = alpha;
        }
        texture->wrap_s = wrap_s ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        texture->wrap_t = wrap_t ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        glGenTextures(1, &texture->name);
        glBindTexture(GL_TEXTURE_2D, texture->name);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        free(pixels);
        ++draw->textured_slots;
    } else {
        glBindTexture(GL_TEXTURE_2D, texture->name);
    }

    // RGBA8 blending can represent the GS factor only in the 0..128 range.
    if (translucent && texture->has_alpha && texture->max_alpha * material->alpha > 128)
        return 0;
    GLint filter = strip->texture_filter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture->wrap_s);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture->wrap_t);
    glUniform1f(draw->alpha, material->alpha);
    glUniform1i(draw->texture_alpha, texture->has_alpha);
    draw_room_mesh(strip, draw, strip->vertices);
    ++draw->strips;
    draw->fans += strip->fan != 0;
    draw->vertices += (unsigned)strip->count;
    draw->clipped += strip->clip_flag;
    draw->blended += translucent;
    for (size_t i = 0; i < strip->count; ++i) {
        const RxRoomVertex *vertex = &strip->vertices[i];
        const float *modulation = vertex->modulation;
        if (!(vertex->fog >= 0 && vertex->fog <= 255)) return 0;
        int direct = 0;
        for (size_t channel = 0; channel < 3; ++channel) {
            if (!isfinite(modulation[channel])) return 0;
            direct |= modulation[channel] > material->ambient[channel] * material->diffuse[channel] + 0.000001f;
        }
        draw->lit += direct;
    }
    float ambient = material->ambient[0];
    if (ambient < 0.18f) ++draw->dark_material;
    if(draw->actor)return glGetError()==GL_NO_ERROR;
    float authored_ambient = 0.3f * (ambient < 0.18f ? 127.0f : 178.0f) / 255.0f;
    for (unsigned i = 0; i < 3; ++i)
        if (material->diffuse[i] != 1.0f || fabsf(material->ambient[i] - authored_ambient) > 0.000001f)
            return 0;
    if (material->alpha != 1.0f) return 0;
    return glGetError() == GL_NO_ERROR;
}

static void check_room_alpha_pixels(const RoomDraw *draw)
{
    // Half of this quad has GS texture alpha 64, and half has alpha zero.
    const uint8_t texels[] = {200, 0, 0, 64, 200, 0, 0, 0};
    RxRoomVertex quad[] = {
        {.position = {-4,  4, 4}, .uv = {0, 0}, .modulation = {0.5f, 1, 1}},
        {.position = { 4,  4, 4}, .uv = {1, 0}, .modulation = {0.5f, 1, 1}},
        {.position = {-4, -4, 4}, .uv = {0, 1}, .modulation = {0.5f, 1, 1}},
        {.position = { 4, -4, 4}, .uv = {1, 1}, .modulation = {0.5f, 1, 1}}
    };
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glViewport(0, 0, 4, 2);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    glClearColor(0, 0, 128.0f / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    NJS_MATRIX identity;
    njUnitMatrix(&identity);
    glUniformMatrix4fv(draw->matrix, 1, GL_FALSE, identity);
    set_room_projection(draw, &test_view);
    glUniform1f(draw->alpha, 0.5f);
    glUniform1i(draw->texture_alpha, 1);
    glUniform1i(draw->fog_enabled, 0);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    uint8_t pixels[4][4];
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(glGetError() == GL_NO_ERROR &&
          abs(pixels[0][0] - 25) <= 1 && pixels[0][1] == 0 && abs(pixels[0][2] - 96) <= 1,
          "GLES GS modulation or source alpha blend differs");
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(glGetError() == GL_NO_ERROR && pixels[0][0] == 100 &&
          pixels[3][0] == 0 && pixels[3][1] == 0 && pixels[3][2] == 128 && pixels[3][3] == 255,
          "GLES GS zero-alpha rejection differs");

    njSetFogColor(0x14508c);
    check((gs.fogcol & 0xffffff) == 0x8c5014, "original FOGCOL register lost its RGB byte order");
    set_room_fog(draw);
    for (size_t i = 0; i < sizeof(quad) / sizeof(*quad); ++i) quad[i].fog = 128;
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    check(glGetError() == GL_NO_ERROR && pixels[0][0] == 59 && pixels[0][1] == 39 &&
          pixels[0][2] == 69 && pixels[0][3] == 64 &&
          pixels[3][0] == 0 && pixels[3][1] == 0 && pixels[3][2] == 128 && pixels[3][3] == 255,
          "GS fog mix changed alpha or produced incorrect RGB");
    glUniform1i(draw->fog_enabled, 0);
    glDeleteTextures(1, &texture);
    puts("PASS GS material modulation, alpha blending and zero-alpha rejection on Quest GLES");
    puts("PASS native FOGCOL register and exact Quest GLES fog pixel");
}

static int draw_overlapping_strip(const RxRoomStrip *strip, void *context)
{
    const RoomDraw *draw = context;
    RxRoomVertex colored[60];
    check(strip->count <= sizeof(colored) / sizeof(*colored), "synthetic clip packet too large");
    for (size_t i = 0; i < strip->count; ++i) {
        colored[i] = strip->vertices[i];
        colored[i].modulation[0] = strip->texture == 0;
        colored[i].modulation[1] = 0;
        colored[i].modulation[2] = strip->texture == 1;
    }
    draw_room_mesh(strip, draw, colored);
    return glGetError() == GL_NO_ERROR;
}

static int count_room_packets(const RxRoomStrip *strip, void *context)
{
    (void)strip;
    ++*(unsigned *)context;
    return 1;
}

typedef struct {
    RoomDraw *draw;
    unsigned fans, strips;
    uint64_t suppressed;
    unsigned last_bucket;
} ScissorDraw;

static int draw_scissored_packet(const RxRoomStrip *strip, void *context)
{
    ScissorDraw *test = context;
    if (strip->attributes & 8) {
        check(strip->ot_bucket >= test->last_bucket, "translucent scissor fan escaped depth ordering");
        test->last_bucket = strip->ot_bucket;
    }
    if (strip->fan) ++test->fans;
    else {
        ++test->strips;
        test->suppressed |= strip->suppressed_triangles;
    }
    return draw_overlapping_strip(strip, test->draw);
}

static void check_scissor_interpolation(void)
{
    const NJS_CNK_VIEW view = {2, 6, 1, 1, 1, 1, 1};
    // Vertex 0 is outside one plane. W holds the original per-vertex fog.
    const NJS_POINT4 positions[][3] = {
        {{-0.5f, 0, 1, 1}, {0, 1, 3, 1}, {1, -1, 3, 1}},
        {{-0.5f, 0, 8, 1}, {0, 1, 4, 1}, {1, -1, 4, 1}},
        {{6, 0, 4, 1}, {2, 1, 4, 1}, {2, -1, 4, 1}},
        {{-6, 0, 4, 1}, {-2, 1, 4, 1}, {-2, -1, 4, 1}},
        {{0, 6, 4, 1}, {1, 2, 4, 1}, {-1, 2, 4, 1}},
        {{0, -6, 4, 1}, {1, -2, 4, 1}, {-1, -2, 4, 1}},
    };
    for (size_t plane = 0; plane < sizeof(positions) / sizeof(*positions); ++plane) {
        const NJS_SCISSOR_VERTEX triangle[3] = {
            {.view = {positions[plane][0].x, positions[plane][0].y, positions[plane][0].z, 20},
             .uv = {0, 0, 1, 0}, .color = {1, 0, 0, 1}},
            {.view = {positions[plane][1].x, positions[plane][1].y, positions[plane][1].z, 100},
             .uv = {1, 0, 1, 0}, .color = {0, 1, 0, 1}},
            {.view = {positions[plane][2].x, positions[plane][2].y, positions[plane][2].z, 180},
             .uv = {0, 1, 1, 0}, .color = {0, 0, 1, 1}}
        };
        NJS_SCISSOR_VERTEX polygon[NJS_SCISSOR_MAX_VERTICES];
        int count = njCnkScissorTriangle(&view, triangle, polygon);
        check(count == 4, "one vertex outside a clip plane did not produce a four-vertex fan");
        unsigned intersections = 0;
        for (int i = 0; i < count; ++i) {
            const NJS_SCISSOR_VERTEX *v = &polygon[i];
            if (fabsf(v->color.x - 0.5f) > 0.0001f) continue;
            int edge = v->color.y > 0 ? 1 : 2;
            NJS_POINT4 a = positions[plane][0], b = positions[plane][edge];
            check(fabsf(v->view.x - (a.x + b.x) * 0.5f) < 0.0001f &&
                  fabsf(v->view.y - (a.y + b.y) * 0.5f) < 0.0001f &&
                  fabsf(v->view.z - (a.z + b.z) * 0.5f) < 0.0001f &&
                  fabsf(v->view.w - (triangle[0].view.w + triangle[edge].view.w) * 0.5f) < 0.0001f &&
                  fabsf(v->uv.x - triangle[edge].uv.x * 0.5f) < 0.0001f &&
                  fabsf(v->uv.y - triangle[edge].uv.y * 0.5f) < 0.0001f &&
                  fabsf(v->color.y - triangle[edge].color.y * 0.5f) < 0.0001f &&
                  fabsf(v->color.z - triangle[edge].color.z * 0.5f) < 0.0001f,
                  "original scissor intersection did not interpolate position, UV, lighting and fog");
            intersections |= 1u << edge;
        }
        check(intersections == 6, "scissor did not produce both edge intersections");
    }

    // The near pass creates an edge that the right plane cuts a second time.
    const NJS_SCISSOR_VERTEX corner[3] = {
        {.view = {0, 0, 1, 20}, .uv = {0, 0, 1, 0}, .color = {1, 0, 0, 1}},
        {.view = {8, 0, 4, 100}, .uv = {1, 0, 1, 0}, .color = {0, 1, 0, 1}},
        {.view = {0, 2, 4, 180}, .uv = {0, 1, 1, 0}, .color = {0, 0, 1, 1}}
    };
    NJS_SCISSOR_VERTEX polygon[NJS_SCISSOR_MAX_VERTICES];
    int count = njCnkScissorTriangle(&view, corner, polygon);
    check(count == 4, "near-and-right plane scissor did not produce a four-vertex fan");
    int corner_found = 0;
    for (int i = 0; i < count; ++i) {
        const NJS_SCISSOR_VERTEX *v = &polygon[i];
        if (fabsf(v->view.x - 2) > 0.0001f || fabsf(v->view.z - 2) > 0.0001f) continue;
        check(fabsf(v->view.y - 1.0f / 6) < 0.0001f &&
              fabsf(v->view.w - 160.0f / 3) < 0.0001f &&
              fabsf(v->uv.x - 0.25f) < 0.0001f && fabsf(v->uv.y - 1.0f / 12) < 0.0001f &&
              fabsf(v->color.x - 2.0f / 3) < 0.0001f &&
              fabsf(v->color.y - 0.25f) < 0.0001f && fabsf(v->color.z - 1.0f / 12) < 0.0001f,
              "second clip plane lost UV, lighting or fog interpolation");
        corner_found = 1;
    }
    check(corner_found, "scissor did not retain the near-and-right plane corner");
}

static void check_room_geometry(RoomDraw *draw)
{
    struct {
        uint32_t vertex_header[16];
        float positions[4][8];
        uint32_t vertex_end;
        Sint16 polygons[2][25];
    } fixture = {
        // Type 51, 47 payload words and four XYZ/normal records.
        .vertex_header = {0x002f0033, 0x00040000},
        .positions = {{-1, 1, 0, 1}, {1, 1, 0, 1}, {-1, -1, 0, 1}, {1, -1, 0, 1}},
        .vertex_end = 255,
        .polygons = {
            {0x3408, 0x4000, 0x2513, 4, -1, -1, -1, -1, 0x0a42, 14, 1, 4,
             0, 0, 0, 1, 1023, 0, 2, 0, 1023, 3, 1023, 1023, 255},
            {0x3408, 0x4001, 0x2513, 4, -1, -1, -1, -1, 0x0a42, 14, 1, 4,
             0, 0, 0, 1, 1023, 0, 2, 0, 1023, 3, 1023, 1023, 255}
        }
    };
    NJS_CNK_MODEL chunks[] = {
        {.vlist = (Sint32 *)fixture.vertex_header, .plist = fixture.polygons[0]},
        {.vlist = (Sint32 *)fixture.vertex_header, .plist = fixture.polygons[1]}
    };
    NJS_CNK_OBJECT objects[2] = {
        {.evalflags = 2, .model = &chunks[0], .pos = {0, 0, 4}, .scl = {4, 4, 1}, .sibling = &objects[1]},
        {.evalflags = 2, .model = &chunks[1], .pos = {0, 0, 8}, .scl = {8, 8, 1}}
    };
    NJS_TEXLIST textures = {.nbTexture = 2};
    ML_WORK model = {.datP = &fixture, .objP = objects, .obj_num = 2, .texP = &textures};
    GLuint texture;
    const uint8_t white[] = {255, 255, 255, 64};
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUniform1f(draw->alpha, 1);
    njUnitMatrix(NULL);
    const char *error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view,
                                            draw_overlapping_strip, draw);
    check(!error, error);
    uint8_t pixel[4];
    glReadPixels(1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    check(glGetError() == GL_NO_ERROR && abs(pixel[0] - 128) <= 1 &&
          pixel[1] == 0 && abs(pixel[2] - 64) <= 1,
          "overlapping strips did not blend far blue before near red");

    NJS_MATRIX split_matrices[2];RxRoomDrawPart split_parts[2];
    for(unsigned i=0;i<2;++i){njUnitMatrix(&split_matrices[i]);njTranslate(&split_matrices[i],0,0,objects[i].pos[2]);
        njScale(&split_matrices[i],objects[i].scl[0],objects[i].scl[1],1);
        split_parts[i]=(RxRoomDrawPart){&chunks[i],&split_matrices[i],
            {fixture.vertex_header,sizeof(fixture.vertex_header)+sizeof(fixture.positions)+sizeof(fixture.vertex_end)},
            {fixture.polygons[i],sizeof(fixture.polygons[i])}};
    }
    for(unsigned equal=0;equal<2;++equal){
        if(equal){njUnitMatrix(&split_matrices[1]);njTranslate(&split_matrices[1],0,0,4);njScale(&split_matrices[1],4,4,1);}
        glClear(GL_COLOR_BUFFER_BIT);
        error=rx_draw_chunk_parts(split_parts,2,&textures,1,&test_view,draw_overlapping_strip,draw);check(!error,error);
        glReadPixels(1,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        check(glGetError()==GL_NO_ERROR&&abs(pixel[0]-(equal?64:128))<=1&&pixel[1]==0&&abs(pixel[2]-(equal?128:64))<=1,
              "split model batch lost cross-model depth order or equal-bucket stability");
    }
    puts("PASS shared split-model translucent ordering and equal-depth stable composition on Quest GLES");

    objects[1].pos[2] = objects[1].scl[0] = objects[1].scl[1] = 4;
    glClear(GL_COLOR_BUFFER_BIT);
    error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view, draw_overlapping_strip, draw);
    check(!error, error);
    glReadPixels(1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    check(glGetError() == GL_NO_ERROR && abs(pixel[0] - 64) <= 1 && abs(pixel[2] - 128) <= 1,
          "translucent strips in the same depth bucket changed order");
    puts("PASS far-to-near and equal-depth Ninja strip composition on Quest GLES");

    // With radius disabled, only the packet's shared outcode can reject it.
    objects[0].sibling = NULL;
    model.obj_num = 1;
    const float outside[][3] = {
        {0, 0, 1}, {0, 0, 21000}, {-12, 0, 4}, {12, 0, 4}, {0, 12, 4}, {0, -12, 4}
    };
    for (size_t i = 0; i < sizeof(outside) / sizeof(*outside); ++i) {
        unsigned packets = 0;
        memcpy(objects[0].pos, outside[i], sizeof(objects[0].pos));
        error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view, count_room_packets, &packets);
        check(!error, error);
        check(!packets, "packet completely outside a Ninja clip plane reached GLES");
    }

    // A culled model with EVAL_CLIP stops traversal of its child branch.
    chunks[0].r = 1;
    objects[0].evalflags |= 0x100;
    objects[0].pos[1] = 12;
    objects[0].scl[0] = objects[0].scl[1] = 1;
    objects[0].child = &objects[1];
    objects[1].pos[1] = -12;
    model.obj_num = 2;
    unsigned packets = 0;
    error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view, count_room_packets, &packets);
    check(!error && !packets, "a culled Ninja parent still traversed its clipped child");
    objects[0].evalflags &= ~0x100;
    error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view, count_room_packets, &packets);
    check(!error && packets == 1, "a culled Ninja parent hid a child with independent visibility");

    // The left edge is before near Z. The right edge is inside the view.
    chunks[0].r = 0;
    objects[0].child = NULL;
    model.obj_num = 1;
    memset(objects[0].pos, 0, sizeof(objects[0].pos));
    objects[0].scl[0] = 1;
    fixture.positions[0][2] = fixture.positions[2][2] = 1;
    fixture.positions[1][0] = fixture.positions[3][0] = 4;
    fixture.positions[1][2] = fixture.positions[3][2] = 4;
    fixture.positions[1][1] = 4;
    fixture.positions[3][1] = -4;
    fixture.polygons[0][8] = 0x0242;
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);
    ScissorDraw clip = {.draw = draw};
    error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view, draw_scissored_packet, &clip);
    check(!error, error);
    check(clip.fans == 2 && !clip.strips, "near-plane scissor did not replace both crossing triangles");
    uint8_t row[4][4];
    glReadPixels(0, 0, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, row);
    check(glGetError() == GL_NO_ERROR && row[0][0] == 0 && row[3][0] == 255,
          "near-plane crossing did not preserve only the visible portion of the strip");

    fixture.positions[2][0] = fixture.positions[2][1] = -4;
    fixture.positions[2][2] = 4;
    clip = (ScissorDraw){.draw = draw};
    error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view, draw_scissored_packet, &clip);
    check(!error && clip.fans == 1 && clip.strips == 1 && clip.suppressed == (UINT64_C(1) << 2),
          "partial scissor did not preserve the untouched triangle in the same strip");
    fixture.polygons[0][8] = 0x0a42;
    clip = (ScissorDraw){.draw = draw};
    glEnable(GL_BLEND);
    error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &test_view, draw_scissored_packet, &clip);
    check(!error && clip.fans == 1 && clip.strips == 1 && clip.last_bucket > 0,
          "translucent scissor did not defer its fan and retained strip");

    // Mirror or rotate a quad across each side plane, then cross far Z.
    const NJS_CNK_VIEW short_view = {2, 6, 1, 1, 1, 1, 1};
    const struct {
        float positions[4][3];
        int inside_x, inside_y, outside_x, outside_y;
    } crossings[] = {
        {{{-2, 2, 4}, {6, 2, 4}, {-2, -2, 4}, {6, -2, 4}}, 5, 4, 0, 4},
        {{{2, 2, 4}, {-6, 2, 4}, {2, -2, 4}, {-6, -2, 4}}, 2, 4, 7, 4},
        {{{2, -2, 4}, {2, 6, 4}, {-2, -2, 4}, {-2, 6, 4}}, 4, 2, 4, 7},
        {{{2, 2, 4}, {2, -6, 4}, {-2, 2, 4}, {-2, -6, 4}}, 4, 5, 4, 0},
        {{{-4, 4, 4}, {8, 8, 8}, {-4, -4, 4}, {8, -8, 8}}, 1, 4, 7, 4}
    };
    set_room_projection(draw, &short_view);
    glViewport(0, 0, 8, 8);
    glDisable(GL_BLEND);
    fixture.polygons[0][8] = 0x0242;
    for (size_t plane = 0; plane < sizeof(crossings) / sizeof(*crossings); ++plane) {
        for (size_t i = 0; i < 4; ++i)
            memcpy(fixture.positions[i], crossings[plane].positions[i], sizeof(crossings[plane].positions[i]));
        clip = (ScissorDraw){.draw = draw};
        glClear(GL_COLOR_BUFFER_BIT);
        error = rx_draw_room_chunks(&model, sizeof(fixture), 1, &short_view, draw_scissored_packet, &clip);
        check(!error && clip.fans == 2 && !clip.strips, "side or far plane bypassed the VU scissor");
        uint8_t inside[4], outside_pixel[4];
        glReadPixels(crossings[plane].inside_x, crossings[plane].inside_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inside);
        glReadPixels(crossings[plane].outside_x, crossings[plane].outside_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outside_pixel);
        check(glGetError() == GL_NO_ERROR && inside[0] == 255 && outside_pixel[0] == 0,
              "side or far plane scissor readback differs");
    }
    check_scissor_interpolation();
    check(!vu_check_mismatches(), "VU scissor microprogram disagreed with the interpreter");
    glDeleteTextures(1, &texture);
    puts("PASS original VU scissor intersection attributes and GLES readback on all six planes");
}

#include "test_face_draw.inc"

void check_room_gpu(const void *rdt,size_t rdt_bytes)
{
    enum { WIDTH = 320, HEIGHT = 240 };
    RxEgl target = {0};
    check(rx_egl_open(&target, NULL, WIDTH, HEIGHT), "cannot create Quest GLES pbuffer");
    const char *vertex_source = "#version 300 es\n"
        "layout(location=0) in vec3 position; layout(location=1) in vec2 coords;\n"
        "layout(location=2) in vec3 modulation; out vec3 shade;\n"
        "layout(location=3) in float vertexFog; out float fog;\n"
        "uniform mat4 modelView; uniform vec4 projection; out vec2 uv;\n"
        "void main() { vec4 p=modelView*vec4(position,1.0);"
        "gl_Position=vec4(projection.xy*vec2(p.x,-p.y),projection.z*p.z+projection.w,p.z); uv=coords;"
        "shade=floor(modulation*128.0)/128.0; fog=floor(clamp(vertexFog,0.0,255.0)); }";
    const char *fragment_source = "#version 300 es\n"
        "precision highp float; uniform highp sampler2D image; in vec2 uv; out vec4 color;\n"
        "in vec3 shade; uniform float materialAlpha; uniform bool textureAlpha;\n"
        "in float fog; uniform vec3 fogColor; uniform bool applyFog;\n"
        "void main() { vec4 texel=texture(image,uv);"
        "float alpha=floor(materialAlpha*(textureAlpha ? texel.a*255.0 : 128.0)+0.0001);"
        "if (alpha <= 0.0) discard;"
        "vec3 rgb=texel.rgb*255.0*shade;"
        "if (applyFog) { float f=clamp(fog,0.0,255.0); rgb=(f*rgb+(255.0-f)*fogColor)/256.0; }"
        "color=vec4(floor(rgb+0.0001)/255.0,alpha/128.0); }";
    GLuint room_program = link_program(vertex_source, fragment_source), buffer, elements;
    glUseProgram(room_program);
    glGenBuffers(1, &buffer);
    glGenBuffers(1, &elements);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RxRoomVertex), NULL);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RxRoomVertex),
                          (void *)offsetof(RxRoomVertex, uv));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(RxRoomVertex),
                          (void *)offsetof(RxRoomVertex, modulation));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(RxRoomVertex),
                          (void *)offsetof(RxRoomVertex, fog));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glViewport(0, 0, WIDTH, HEIGHT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    static NJS_MATRIX stack[80];
    NJS_MATRIX view_before, view_after;
    _Make_SinTable();
    njInitMatrix(stack, 80, 0);
    bhInitCamera();
    bhInitLight();
    bhSetRenderCut(2);
    // Use the fixed cut, Ninja's +Z view basis and the original transform order.
    njUnitMatrix(NULL);
    njScale(NULL, 1, -1, -1);
    njTranslate(NULL, 0, 0, -cam.ln);
    njRotateZ(NULL, cam.az);
    njRotateX(NULL, cam.ax);
    njRotateY(NULL, cam.ay);
    njTranslate(NULL, -(cam.px + cam.ofx), -(cam.py + cam.ofy), -(cam.pz + cam.ofz));
    njGetMatrix(&view_before);
    float scale = 1.0f / tanf((cam.pers / 2) * 0.0000958738f);
    _nj_screen_.dist = 320.0f * scale;
    const NJS_CNK_VIEW view = {2, 20000, _nj_screen_.dist, 320, 240, 1.174f, 1};
    njGetMatrix(cam.mtx);
    unsigned sp_flags = sys->sp_flg;
    sys->sp_flg |= 0x80;
    bhControlLight();
    check((sys->st_flg & 0x10000) && sys->lg_pnt == 2 &&
          sys->lg_r == rom->lgtp[6].r && sys->lg_ptb[0].cr == rom->lgtp[8].r &&
          sys->lg_ptb[1].cr == rom->lgtp[11].r * 0.75f && rom->lgtp[11].ct0 == 2816,
          "original light controller did not select the authored directional and point lights");
    float first_pulse = sys->lg_ptb[1].cr;

    NJS_TEXLIST *list = rom->mdl.texP;
    RoomDraw draw = {.textures = calloc(list->nbTexture, sizeof(*draw.textures)),
                     .elements = elements,
                     .matrix = glGetUniformLocation(room_program, "modelView"),
                     .projection = glGetUniformLocation(room_program, "projection"),
                     .alpha = glGetUniformLocation(room_program, "materialAlpha"),
                     .texture_alpha = glGetUniformLocation(room_program, "textureAlpha"),
                     .fog_enabled = glGetUniformLocation(room_program, "applyFog"),
                     .fog_color = glGetUniformLocation(room_program, "fogColor")};
    check(draw.textures && draw.matrix >= 0 && njSetTexture(list), "cannot bind the opening room texture list");
    gs_reset();
    Ps2_current_texbreak = 1;
    njFogDisable();
    set_room_fog(&draw);
    set_room_projection(&draw, &view);
    const char *error = bhDrawNativeRoom(&view, draw_room_strip, &draw);
    check(!error, error);
    njGetMatrix(&view_after);
    check(pNaMatMatrixStuckPtr == stack && !memcmp(&view_before, &view_after, sizeof(view_before)),
          "room drawing changed the caller's Ninja matrix");

    uint32_t *pixels = malloc(WIDTH * HEIGHT * sizeof(*pixels));
    check(pixels != NULL, "cannot allocate opening room readback");
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    unsigned covered = 0, brightest = 0;
    for (size_t i = 0; i < WIDTH * HEIGHT; ++i) {
        covered += (pixels[i] & 0xffffff) != 0;
        for (unsigned channel = 0; channel < 3; ++channel) {
            unsigned value = (pixels[i] >> (channel * 8)) & 255;
            if (value > brightest) brightest = value;
        }
    }
    printf("Opening room cut 2: %u packets (%u fans, %u blended), %u vertices (%u receive direct light), "
           "%u textures, %u lit pixels (max %u), %u dark material strips\n",
           draw.strips, draw.fans, draw.blended, draw.vertices, draw.lit, draw.textured_slots, covered, brightest,
           draw.dark_material);
    check(draw.fans == 169 && draw.strips == 193 && draw.vertices == 743 && draw.textured_slots == 7,
          "opening cut scissor submission changed");
    check(draw.blended == 3 && draw.dark_material == 0, "opening cut material counts differ");
    check(glGetError() == GL_NO_ERROR && abs((int)covered - 74642) <= 16 && brightest > 53 && draw.lit > 100,
          "opening room did not rasterize with direct light");

    for (unsigned i = 0; i < 7; ++i) bhControlLight();
    check(sys->lg_ptb[1].cr > first_pulse + 0.1f && rom->lgtp[11].ct0 == 8 * 2816,
          "original point light did not advance its authored pulse");
    sys->sp_flg = sp_flags;
    clear_room_frame(&draw);
    error = bhDrawNativeRoom(&view, draw_room_strip, &draw);
    check(!error, error);
    uint32_t *later = malloc(WIDTH * HEIGHT * sizeof(*later));
    check(later != NULL, "cannot allocate pulsing light readback");
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, later);
    unsigned changed = 0;
    for (size_t i = 0; i < WIDTH * HEIGHT; ++i)
        changed += ((pixels[i] ^ later[i]) & 0xffffff) != 0;
    check(glGetError() == GL_NO_ERROR && changed >= 50 && draw.blended == 3 && !vu_check_mismatches(),
          "opening point light pulse did not change the frame or VU0 reference disagreed");
    printf("PASS original room lighting pulse changed %u pixels\n", changed);

    // Keep the geometry and light frame fixed. Cut 0 supplies the authored
    // fog values because the fixed render cut used above has fog disabled.
    const CAM_WRK *fog_camera = &rom->cutp[0].cam[0];
    check((fog_camera->flg & 8) && !(rom->cutp[2].cam[0].flg & 8),
          "opening fixture did not contain the expected fog settings");
    cam.fog_col = fog_camera->fog_col;
    cam.fog_nr = fog_camera->fog_nr;
    cam.fog_fr = fog_camera->fog_fr;
    unsigned state = sys->st_flg;
    bhApplyCameraFog();
    check((sys->st_flg & 2) && gs.fogcol == 0x0f0500 &&
          fabsf(njCalcFogPowerEx(1.0f / 70) - 191.25f) < 0.0001f &&
          fabsf(njCalcFogPowerEx(1.0f / 140) - 127.5f) < 0.0001f &&
          fabsf(njCalcFogPowerEx(1.0f / 280)) < 0.0001f,
          "original opening fog range or color differs");
    set_room_fog(&draw);
    clear_room_frame(&draw);
    error = bhDrawNativeRoom(&view, draw_room_strip, &draw);
    check(!error, error);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    unsigned fogged = 0;
    for (size_t i = 0; i < WIDTH * HEIGHT; ++i)
        fogged += ((pixels[i] ^ later[i]) & 0xffffff) != 0;
    check(glGetError() == GL_NO_ERROR && fogged >= 1000 && draw.fans == 169,
          "authored fog did not affect the opening-room frame");
    printf("PASS authored opening fog changed %u pixels with geometry and lighting held fixed\n", fogged);
    njFogDisable();
    sys->st_flg = state;
    free(later);
    check_room_alpha_pixels(&draw);
    check_room_geometry(&draw);
    check_face_draw_gpu(&draw,&view,rdt,rdt_bytes);

    free(pixels);
    for (unsigned i = 0; i < list->nbTexture; ++i)
        glDeleteTextures(1, &draw.textures[i].name);
    free(draw.textures);
    glDeleteBuffers(1, &elements);
    glDeleteBuffers(1, &buffer);
    glDeleteProgram(room_program);
    rx_egl_close(&target);
}
