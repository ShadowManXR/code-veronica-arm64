#pragma once

#include <GLES3/gl3.h>
#include <stddef.h>
#include "boot_disc.h"
#include "title_menu.h"

typedef struct {
    GLuint program;
    GLuint textures[5];
    unsigned width[5], height[5];
    GLint positions, uv, color, textured;
} RxTitleDraw;

int rx_title_open(RxTitleDraw *draw, const RxBootDisc *boot);
int rx_title_present(const RxTitleDraw *draw, RxTitleScreen *screen, int interactive);
void rx_title_close(RxTitleDraw *draw);

/* The original ADV layout calls these during title submission. */
struct QUAD;
void rx_adv_texture_size(unsigned list, unsigned texture, float *width, float *height);
void rx_adv_draw_quad(int texture, unsigned color, const struct QUAD *quad, float depth, int translucent);
void rx_adv_draw_polygon(const void *vertices, int count, int translucent);
