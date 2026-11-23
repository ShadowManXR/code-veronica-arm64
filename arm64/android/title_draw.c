#include "title_draw.h"
#include "boot_disc.h"
#include "adv.h"
#include "main.h"

#include <stdint.h>
#include <stdlib.h>

// The Ninja 2D origin is (2048 - 320, 2048 - 240).
enum { GAME_WIDTH = 640, GAME_HEIGHT = 480 };
typedef struct {
    float vertices[4][2];
    float uv[4];
    unsigned colors[4];
    float depth;
    int texture, translucent;
} AdvPrimitive;

typedef struct {
    const RxTitleDraw *draw;
    int first_texture, texture_count;
    unsigned count;
    int overflow;
    AdvPrimitive primitives[96];
} AdvFrame;

static AdvFrame *active_frame;

static uint32_t read16(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static uint32_t read32(const unsigned char *p)
{
    return read16(p) | read16(p + 2) << 16;
}

static int upload_tim2(GLuint texture, const unsigned char *data, size_t size,
                       unsigned *width, unsigned *height)
{
    enum { FILE_HEADER = 128, PICTURE_HEADER_MIN = 48 };
    if (size < FILE_HEADER + PICTURE_HEADER_MIN ||
        data[4] != 4 || data[5] != 1 || read16(data + 6) != 1) return 0;

    const unsigned char *picture = data + FILE_HEADER;
    uint32_t total = read32(picture), clut_bytes = read32(picture + 4), image_bytes = read32(picture + 8);
    uint32_t header_bytes = read16(picture + 12), colors = read16(picture + 14);
    unsigned type = picture[19], w = read16(picture + 20), h = read16(picture + 22);
    if (total > size - FILE_HEADER || header_bytes < PICTURE_HEADER_MIN || header_bytes > total ||
        image_bytes > total - header_bytes || clut_bytes > total - header_bytes - image_bytes ||
        picture[17] != 1 || picture[18] != 3 || !w || !h || w > 1024 || h > 512 ||
        !((type == 4 && colors == 16) || (type == 5 && colors == 256)) || colors * 4 > clut_bytes)
        return 0;
    size_t count = (size_t)w * h;
    if (image_bytes != (type == 5 ? count : (count + 1) / 2)) return 0;
    const unsigned char *indices = picture + header_bytes;
    const unsigned char *palette = indices + image_bytes;
    unsigned char *pixels = malloc(count * 4);
    if (!pixels) return 0;
    for (size_t i = 0; i < count; ++i) {
        unsigned index;
        if (type == 5) {
            index = indices[i];
            // CSM1 swaps the two middle eight-color groups in each block.
            index = (index & ~0x18u) | ((index & 8) << 1) | ((index & 16) >> 1);
        } else {
            index = (indices[i / 2] >> ((i & 1) * 4)) & 15;
        }
        for (unsigned channel = 0; channel < 4; ++channel)
            pixels[i * 4 + channel] = palette[index * 4 + channel];
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
    *width = w;
    *height = h;
    return glGetError() == GL_NO_ERROR;
}

static GLuint compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled) return shader;
    glDeleteShader(shader);
    return 0;
}

void rx_title_close(RxTitleDraw *draw)
{
    glDeleteTextures((GLsizei)(sizeof(draw->textures) / sizeof(*draw->textures)), draw->textures);
    if (draw->program) glDeleteProgram(draw->program);
    *draw = (RxTitleDraw){0};
}

int rx_title_open(RxTitleDraw *draw, const RxBootDisc *boot)
{
    static const char vertex_source[] =
        "#version 300 es\n"
        "uniform vec2 positions[4]; uniform vec4 uv, tint[4];\n"
        "out vec2 st; out vec4 shade;\n"
        "void main() {\n"
        "  vec2 corner = vec2(gl_VertexID >> 1, gl_VertexID & 1);\n"
        "  gl_Position = vec4(positions[gl_VertexID], 0, 1);\n"
        "  st = mix(uv.xy, uv.zw, corner); shade = tint[gl_VertexID];\n"
        "}\n";
    static const char fragment_source[] =
        "#version 300 es\n"
        "precision mediump float; uniform sampler2D atlas; uniform bool textured;\n"
        "in vec2 st; in vec4 shade; out vec4 pixel;\n"
        "void main() { pixel = textured ? texture(atlas, st) * shade : shade; }\n";
    GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
    GLint linked = 0;
    if (vertex && fragment) {
        draw->program = glCreateProgram();
        glAttachShader(draw->program, vertex);
        glAttachShader(draw->program, fragment);
        glLinkProgram(draw->program);
        glGetProgramiv(draw->program, GL_LINK_STATUS, &linked);
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!linked) goto fail;
    draw->positions = glGetUniformLocation(draw->program, "positions[0]");
    draw->uv = glGetUniformLocation(draw->program, "uv");
    draw->color = glGetUniformLocation(draw->program, "tint[0]");
    draw->textured = glGetUniformLocation(draw->program, "textured");

    glGenTextures((GLsizei)(sizeof(draw->textures) / sizeof(*draw->textures)), draw->textures);
    static const unsigned resources[] = {0, 2, 2, 3, 4};
    for (unsigned i = 0; i < sizeof(resources) / sizeof(*resources); ++i) {
        size_t available;
        const unsigned char *package = i < 2 ? boot->title_package : boot->options_package;
        size_t size = i < 2 ? boot->title_size : boot->options_size;
        if (!package) goto fail;
        const unsigned char *tim2 = rx_boot_tim2(package, size, resources[i], &available);
        if (!tim2 || !upload_tim2(draw->textures[i], tim2, available, &draw->width[i], &draw->height[i]))
            goto fail;
    }
    return 1;
fail:
    rx_title_close(draw);
    return 0;
}

void rx_adv_texture_size(unsigned list, unsigned texture, float *width, float *height)
{
    *width = *height = 1.0f;
    if (!active_frame || list || texture >= (unsigned)active_frame->texture_count) return;
    unsigned index = active_frame->first_texture + texture;
    *width = (float)active_frame->draw->width[index];
    *height = (float)active_frame->draw->height[index];
}

static void submit(AdvPrimitive primitive)
{
    if (!active_frame) return;
    if (active_frame->count == sizeof(active_frame->primitives) / sizeof(*active_frame->primitives)) {
        active_frame->overflow = 1;
        return;
    }
    // The PS2 uses depth for layers even when the background is submitted last.
    // Preserve submission order within a layer and compose from far to near.
    unsigned at = active_frame->count++;
    while (at > 0 && active_frame->primitives[at - 1].depth > primitive.depth) {
        active_frame->primitives[at] = active_frame->primitives[at - 1];
        --at;
    }
    active_frame->primitives[at] = primitive;
}

void rx_adv_draw_quad(int texture, unsigned color, const QUAD *quad, float depth, int translucent)
{
    if (!active_frame || (unsigned)texture >= (unsigned)active_frame->texture_count) return;
    AdvPrimitive primitive = {
        .vertices = {{quad->x1, quad->y1}, {quad->x1, quad->y2}, {quad->x2, quad->y1}, {quad->x2, quad->y2}},
        .uv = {quad->u1, quad->v1, quad->u2, quad->v2},
        .colors = {color, color, color, color},
        .depth = depth,
        .texture = active_frame->first_texture + texture,
        .translucent = translucent
    };
    submit(primitive);
}

void rx_adv_draw_polygon(const void *vertices, int count, int translucent)
{
    const NJS_POLYGON_VTX *polygon = vertices;
    if (count != 4) return;
    AdvPrimitive primitive = {.depth = polygon[0].z, .texture = -1, .translucent = translucent};
    for (unsigned i = 0; i < 4; ++i) {
        primitive.vertices[i][0] = polygon[i].x;
        primitive.vertices[i][1] = polygon[i].y;
        primitive.colors[i] = polygon[i].col;
    }
    submit(primitive);
}

static void draw_primitive(const RxTitleDraw *draw, const AdvPrimitive *primitive)
{
    if (primitive->translucent) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glUniform1i(draw->textured, primitive->texture >= 0);
    if (primitive->texture >= 0) glBindTexture(GL_TEXTURE_2D, draw->textures[primitive->texture]);

    float positions[4][2], tint[4][4];
    for (unsigned i = 0; i < 4; ++i) {
        positions[i][0] = 2.0f * (primitive->vertices[i][0] + sys->adjust_x) / GAME_WIDTH - 1;
        positions[i][1] = 1 - 2.0f * (primitive->vertices[i][1] + sys->adjust_y) / GAME_HEIGHT;
        unsigned color = primitive->colors[i];
        tint[i][0] = ((color >> 16) & 255) / 255.0f;
        tint[i][1] = ((color >> 8) & 255) / 255.0f;
        tint[i][2] = (color & 255) / 255.0f;
        tint[i][3] = (color >> 24) / 255.0f;
    }
    glUniform2fv(draw->positions, 4, &positions[0][0]);
    glUniform4fv(draw->uv, 1, primitive->uv);
    glUniform4fv(draw->color, 4, &tint[0][0]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int rx_title_present(const RxTitleDraw *draw, RxTitleScreen *screen, int interactive)
{
    if (!draw->program) return 0;
    AdvFrame frame = {.draw = draw, .texture_count = 2};
    active_frame = &frame;
    switch (*screen) {
    case RX_TITLE_PROMPT: DisplayPressStartPlate(AdvWork.FlushCount); break;
    case RX_TITLE_MENU: DisplayGameModePlate(AdvWork.Cursor[0], AdvWork.CursorMax[0], 255.0f, 0); break;
    case RX_TITLE_DIFFICULTY: DisplayNewGamePlate(AdvWork.Cursor[1], 255.0f, 0); break;
    case RX_TITLE_OPTIONS:
        frame.first_texture = 2;
        frame.texture_count = 3;
        if (rx_title_options_draw(interactive)) *screen = RX_TITLE_MENU;
        break;
    }
    active_frame = NULL;
    glUseProgram(draw->program);
    glActiveTexture(GL_TEXTURE0);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (unsigned i = 0; i < frame.count; ++i) draw_primitive(draw, &frame.primitives[i]);
    glDisable(GL_BLEND);
    return !frame.overflow && glGetError() == GL_NO_ERROR;
}
