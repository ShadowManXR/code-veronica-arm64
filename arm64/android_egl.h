#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>

typedef struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    EGLConfig config;
} RxEgl;

/* A NULL window creates a pbuffer. The caller owns the native window. */
int rx_egl_open(RxEgl *target, ANativeWindow *window, int width, int height);
void rx_egl_close(RxEgl *target);
int rx_egl_size(const RxEgl *target, int *width, int *height);
