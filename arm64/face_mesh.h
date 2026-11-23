#pragma once
#include "face.h"
#include "ninja_binary.h"
#include <stddef.h>

typedef struct RxFaceMesh RxFaceMesh;
/* Owns immutable MASK/bind-pose copies, a writable destination and native
   neighbor references. No model/hierarchy or actor context is published. */
const char *rx_face_mesh_create(const void *mask,size_t bytes,const RxChunk *chunk,RxFaceMesh **out);
void rx_face_mesh_destroy(RxFaceMesh *mesh);
const MASK_WORK *rx_face_mesh_work(const RxFaceMesh *mesh);
size_t rx_face_mesh_neighbors(const RxFaceMesh *mesh,const NJS_POINT3 *const **out);
size_t rx_face_mesh_bytes(const RxFaceMesh *mesh);
/* Isolated muscle stage; this entry point does not apply the jaw transform. */
const char *rx_face_mesh_muscles(RxFaceMesh *mesh,const PARAM_WORK *param);
/* Call after original trig/VU initialization. Matrices remain detached. */
const char *rx_face_mesh_jaw_init(RxFaceMesh *mesh);
/* Fresh muscle pose followed by original weighted jaw deformation. */
const char *rx_face_mesh_deform(RxFaceMesh *mesh,const PARAM_WORK *param);
/* Copy the decoded/native bind hierarchy; borrowed, immutable model metadata
   must outlive this mesh. No live actor hierarchy is replaced. */
const char *rx_face_mesh_aux_prepare(RxFaceMesh *mesh,const RxModel *source,const ML_WORK *model);
const char *rx_face_mesh_aux_init(RxFaceMesh *mesh);
const NJS_CNK_OBJECT *rx_face_mesh_root(const RxFaceMesh *mesh);
size_t rx_face_mesh_tongue_bytes(const RxFaceMesh *mesh);
/* Original muscle -> jaw -> tongue -> eye order on detached owned work. */
const char *rx_face_mesh_animate(RxFaceMesh *mesh,const PARAM_WORK *param);

/* Same parameter/range checks as animate, preserving mesh and VU state. */
const char *rx_face_mesh_animate_check(const RxFaceMesh*,const PARAM_WORK*);
