#pragma once
#include "face_mesh.h"
#include "room_engine.h"
typedef struct RxFacePose RxFacePose;
/* Stage after auxiliary initialization. Owns flat object/matrix/skin buffers;
   mesh, texture metadata and validated motion banks must outlive this view. */
const char *rx_face_pose_create(RxFaceMesh *mesh,const RxEngineModelPart *part,RxFacePose **out);
void rx_face_pose_destroy(RxFacePose *pose);
const ML_WORK *rx_face_pose_model(const RxFacePose *pose);
const RxFaceMesh *rx_face_pose_mesh(const RxFacePose *pose);
const BH_PWORK *rx_face_pose_actor(const RxFacePose *pose);
const unsigned *rx_face_pose_weights(const RxFacePose *pose,size_t *count);
/* Original 22-track body motion with protected facial nodes; actor-local skin
   output is separate from facial bind output. Hokan/mirror/remap stay gated. */
const char *rx_face_pose_sample(RxFacePose *pose,MN_WORK *bank,size_t slots,unsigned motion,
                               int frame,int step,const PARAM_WORK *expression);
/* Prepares visible original flat-model entries with separate packet bounds.
   Returned matrices belong to pose and remain valid until its next draw call. */
const char *rx_face_pose_parts(RxFacePose *pose,const NJS_MATRIX *model_to_view,
                               RxRoomDrawPart parts[31],size_t *count);
const char *rx_face_pose_draw(RxFacePose *pose,const NJS_MATRIX *model_to_view,const NJS_CNK_VIEW *view,
                              RxRoomSubmit submit,void *context);

/* Publish skin from already-evaluated world joints without evaluating motion,
   changing actor clocks, deforming face sources, or replacing joint matrices.
   Original translation subtraction is paired with owner translation at draw. */
const char *rx_face_pose_skin_check(const RxFacePose*,const BH_PWORK*);
const char *rx_face_pose_skin(RxFacePose*,const BH_PWORK*);
