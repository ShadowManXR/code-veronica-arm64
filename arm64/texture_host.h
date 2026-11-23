#pragma once

#include <stddef.h>
#include <stdint.h>

/* Native host transport for the original TIM2 image, CLUT and draw state. */
int rx_texture_transfer(uint32_t block, unsigned stride, unsigned format,
                        unsigned y, unsigned width, unsigned height,
                        const void *source, size_t bytes);
int rx_texture_transfer_at(uint32_t block, unsigned stride, unsigned format,
                           unsigned x, unsigned y, unsigned width, unsigned height,
                           const void *source, size_t bytes);
void rx_texture_register(unsigned reg, uint64_t value);
/* Writes tightly packed RGBA8 pixels into the original TIM2 image area. */
void rx_texture_capture(void *pixels, unsigned width, unsigned height);
