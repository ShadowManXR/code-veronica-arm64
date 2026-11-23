#pragma once

#include "android_egl.h"
#include "pad_input.h"
#include <android/native_activity.h>

typedef struct RxTheater RxTheater;

/* The theater uses the caller's current GLES context. Destroy before EGL. */
const char *rx_theater_open(RxTheater **out, ANativeActivity *activity, const RxEgl *egl);
void rx_theater_close(RxTheater *theater);
void rx_theater_target(RxTheater *theater);
/* Pump lifecycle events, wait for the next XR frame, and collect Touch input.
   Returns -1 on failure, 0 while idle, and 1 for a begun frame. */
int rx_theater_begin(RxTheater *theater, RxPadInput *pad, int *focused, double *seconds);
int rx_theater_end(RxTheater *theater);
int rx_theater_exiting(const RxTheater *theater);
void rx_theater_suppress_held_buttons(RxTheater *theater);
