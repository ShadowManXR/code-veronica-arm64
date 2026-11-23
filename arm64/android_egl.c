#include "android_egl.h"

void rx_egl_close(RxEgl *target)
{
    if (target->display == EGL_NO_DISPLAY) return;
    if (target->context != EGL_NO_CONTEXT && eglGetCurrentContext() == target->context)
        eglMakeCurrent(target->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (target->surface != EGL_NO_SURFACE) eglDestroySurface(target->display, target->surface);
    if (target->context != EGL_NO_CONTEXT) eglDestroyContext(target->display, target->context);
    eglTerminate(target->display);
    *target = (RxEgl){0};
}

int rx_egl_open(RxEgl *target, ANativeWindow *window, int width, int height)
{
    RxEgl next = {0};
    next.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (next.display == EGL_NO_DISPLAY || !eglInitialize(next.display, NULL, NULL)) return 0;

    const EGLint attributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, window ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_NONE
    };
    const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint pbuffer_attributes[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLConfig config;
    EGLint count;
    if (!eglBindAPI(EGL_OPENGL_ES_API) ||
        !eglChooseConfig(next.display, attributes, &config, 1, &count) || count != 1)
        goto fail;

    next.context = eglCreateContext(next.display, config, EGL_NO_CONTEXT, context_attributes);
    if (next.context == EGL_NO_CONTEXT) goto fail;
    if (window) {
        EGLint format;
        if (!eglGetConfigAttrib(next.display, config, EGL_NATIVE_VISUAL_ID, &format) ||
            ANativeWindow_setBuffersGeometry(window, 0, 0, format) < 0)
            goto fail;
        next.surface = eglCreateWindowSurface(next.display, config, window, NULL);
    } else {
        next.surface = eglCreatePbufferSurface(next.display, config, pbuffer_attributes);
    }
    if (next.surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(next.display, next.surface, next.surface, next.context))
        goto fail;
    next.config = config;
    *target = next;
    return 1;

fail:
    rx_egl_close(&next);
    return 0;
}

int rx_egl_size(const RxEgl *target, int *width, int *height)
{
    return eglQuerySurface(target->display, target->surface, EGL_WIDTH, width) &&
           eglQuerySurface(target->display, target->surface, EGL_HEIGHT, height) &&
           *width > 0 && *height > 0;
}
