#include "movie_port.h"

#include <GLES3/gl3.h>

const uint8_t *rx_movie_pixels(const RxMovie *movie, unsigned *serial, unsigned *width,
                              unsigned *height, float rectangle[4], float *brightness);

static GLuint compile(GLenum kind, const char *code)
{
    GLuint shader = glCreateShader(kind);
    if (!shader) return 0;
    glShaderSource(shader, 1, &code, NULL);
    glCompileShader(shader);
    GLint ready = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ready);
    if (ready) return shader;
    glDeleteShader(shader);
    return 0;
}

void rx_game_movie_close_graphics(RxMovieDraw *draw)
{
    if (draw->program) glDeleteProgram(draw->program);
    glDeleteTextures(3, draw->planes);
    *draw = (RxMovieDraw){0};
}

static int open_graphics(RxMovieDraw *draw)
{
    const char *vertex =
        "#version 300 es\n"
        "uniform vec4 rect; out vec2 uv;\n"
        "void main() { uv=vec2(gl_VertexID&1,gl_VertexID>>1);"
        "gl_Position=vec4(mix(rect.xy,rect.zw,uv),0.0,1.0); }";
    const char *fragment =
        "#version 300 es\n"
        "precision mediump float; in vec2 uv; out vec4 color;\n"
        "uniform sampler2D luma, blue, red; uniform float brightness;\n"
        "void main() { float y=texture(luma,uv).r;"
        "float cb=texture(blue,uv).r-128.0/255.0, cr=texture(red,uv).r-128.0/255.0;"
        "vec3 rgb=vec3(y+1.402*cr,y-0.344136*cb-0.714136*cr,y+1.772*cb);"
        "color=vec4(clamp(rgb,0.0,1.0)*brightness,1.0); }";
    GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    GLint ready = GL_FALSE;
    if (vs && fs) {
        draw->program = glCreateProgram();
        glAttachShader(draw->program, vs);
        glAttachShader(draw->program, fs);
        glLinkProgram(draw->program);
        glGetProgramiv(draw->program, GL_LINK_STATUS, &ready);
    }
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (!ready) {
        rx_game_movie_close_graphics(draw);
        return 0;
    }
    glUseProgram(draw->program);
    draw->bounds = glGetUniformLocation(draw->program, "rect");
    draw->brightness = glGetUniformLocation(draw->program, "brightness");
    static const char *const names[] = {"luma", "blue", "red"};
    glGenTextures(3, draw->planes);
    for (unsigned i = 0; i < 3; ++i) {
        glUniform1i(glGetUniformLocation(draw->program, names[i]), i);
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, draw->planes[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glActiveTexture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

int rx_game_movie_draw(const RxMovie *movie, RxMovieDraw *draw)
{
    unsigned serial, width, height;
    float rect[4], brightness;
    const uint8_t *pixels = rx_movie_pixels(movie, &serial, &width, &height, rect, &brightness);
    if (!pixels) return 1;
    if (!draw->program && !open_graphics(draw)) return 0;
    glUseProgram(draw->program);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (unsigned i = 0; i < 3; ++i) {
        unsigned w = i ? width / 2 : width, h = i ? height / 2 : height;
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, draw->planes[i]);
        if (draw->width != width || draw->height != height)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
        else if (draw->uploaded != serial)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, pixels);
        pixels += (size_t)w * h;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glActiveTexture(GL_TEXTURE0);
    draw->width = width;
    draw->height = height;
    draw->uploaded = serial;
    glUniform4fv(draw->bounds, 1, rect);
    glUniform1f(draw->brightness, brightness);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    return glGetError() == GL_NO_ERROR;
}
