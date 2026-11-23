#pragma once
#include "face.h"
#include <stddef.h>

/* Views borrow immutable, four-byte-aligned room bytes. These are samplers,
   not geometry contexts: never publish them as an actor's exp1/exp2. */
typedef struct { const PARAM_WORK *keys; unsigned count, actor; } RxFaceClip;
typedef struct { const unsigned char *data; size_t bytes; } RxLipClip;
const char *rx_face_bank_validate(const void *data,size_t bytes,int lip,unsigned *count);
const char *rx_face_clip(const void *data,size_t bytes,unsigned index,RxFaceClip *out);
const char *rx_lip_clip(const void *data,size_t bytes,unsigned index,RxLipClip *out);
const char *rx_face_sample(const RxFaceClip *clip,unsigned actor,float frame,PARAM_WORK *out);
const char *rx_lip_begin(const RxLipClip *clip,LIP_WORK *out);
const char *rx_lip_sample(const RxLipClip *clip,const PARAM_WORK base[6],LIP_WORK *state,PARAM_WORK *inout);
