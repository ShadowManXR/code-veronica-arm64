#pragma once
#include "types.h"
#include <stddef.h>
#include <stdint.h>
#include "face.h"
typedef struct RxFaceMesh RxFaceMesh;
typedef struct RxFacePose RxFacePose;
enum { RX_FACE_CONTROL_WORDS=273, RX_FACE_CONTROL_BYTES=1092 };
/* Original scalar layout; pointer words 265..268 remain zero. Native expression
   and lip pointers live in a separate typed context. Bind borrows valid owned
   storage; reset bindings before freeing it. */
const char *rx_face_control_bind(BH_PWORK *actor,int32_t *words,size_t bytes);
const char *rx_face_control_check(const BH_PWORK *actor);
void rx_face_control_reset(void);
const char *rx_face_control_prepare(BH_PWORK*,RxFaceMesh*,RxFacePose*,const void*,size_t,const void*,size_t);
const char *rx_face_control_ready(const BH_PWORK*);
const char *rx_face_control_request(const BH_PWORK*,int expression,unsigned index,unsigned group);
const char *rx_face_control_tick(BH_PWORK*);
const char *rx_face_control_tick_check(BH_PWORK*);
const ML_WORK *rx_face_control_model(const BH_PWORK*);
/* Typed replacements for the original pointer words; never store addresses in
   scalar exp0. Used by the original native face.c control bodies. */
void rx_face_control_initialize(BH_PWORK*);
const unsigned char *rx_face_control_expression(BH_PWORK*);
const PARAM_WORK *rx_face_control_base(BH_PWORK*);
const PARAM_WORK *rx_face_control_lip_base(BH_PWORK*);
const unsigned char *rx_face_control_lip_data(BH_PWORK*);
void rx_face_control_restore_base(BH_PWORK*);
int rx_face_control_select_expression(BH_PWORK*,int);
int rx_face_control_select_lip(BH_PWORK*,int);
void rx_face_control_deform(BH_PWORK*,MASK_WORK*);
