#pragma once

#include <stddef.h>
#include <stdint.h>
#include "types.h"
#include "room_draw.h"

typedef struct RxEngineRoom RxEngineRoom;
typedef struct RxEnginePlayer RxEnginePlayer;
typedef struct RxEngineWeapon RxEngineWeapon;
typedef struct RxEngineEffects RxEngineEffects;
typedef struct RxFaceMesh RxFaceMesh;
typedef struct RxFacePose RxFacePose;

typedef struct {
    const float (*positions)[3];
    const unsigned short *indices;
    size_t vertex_count, triangle_count;
} RxShadowMesh;

/* Owns the common effect/font package and the native model views at original
   effect IDs 0, 1, 7 and 13. Shadow ID 1 has two bounded modifier meshes. */
const char *rx_engine_effects_load(const void *data, size_t bytes, RxEngineEffects **out);
unsigned char *rx_engine_effect_pictures(RxEngineEffects *effects);
void rx_engine_effects_destroy(RxEngineEffects *effects);
ML_WORK *rx_engine_effect_model(RxEngineEffects *effects, unsigned id);
const RxShadowMesh *rx_engine_shadow_mesh(RxEngineEffects *effects, unsigned version);

typedef enum {
    RX_PLAYER_IDLE, RX_PLAYER_TURN, RX_PLAYER_WALK, RX_PLAYER_RUN, RX_PLAYER_REVERSE,
    RX_PLAYER_CLIPS
} RxPlayerClip;

typedef struct {
    unsigned motion_slot;
    int translation_slot; // -1 for stationary clips
} RxPlayerClipSource;

typedef struct {
    ML_WORK model;
    int *skin;
    size_t skin_bytes;
    size_t model_bytes;
    NJS_TEXLIST *authored_textures;
} RxEngineModelPart;

/* Original enemy driver callback; valid only inside the preflighted room task. */
const char *bhStepNativeEnemyController(BH_PWORK *actor);

/* Initialize *out to NULL. A successful replacement releases the previous room.
   Consumers must stop using the old view before replacing or destroying it. */
const char *rx_engine_room_load(const void *rdt, size_t bytes, RxEngineRoom **out);
void rx_engine_room_destroy(RxEngineRoom *room);
ROM_WORK *rx_engine_room_rom(RxEngineRoom *room);
MN_WORK *rx_engine_room_motions(RxEngineRoom *room);
/* Slot indices refer to the RDT records. Missing packages return NULL. The
   model includes converted bind-pose vertices. Its skin words remain
   available to the original per-frame skin routines. */
RxEngineModelPart *rx_engine_enemy_model(RxEngineRoom *room, size_t enemy, size_t part);
RxEngineModelPart *rx_engine_prop_model(RxEngineRoom *room, size_t index);
RxEngineModelPart *rx_engine_item_model(RxEngineRoom *room, size_t index);
unsigned char *rx_engine_enemy_mask(RxEngineRoom *room, size_t enemy, size_t *bytes);
/* Room-owned scalar staging, not a ready deformation/lip-sync context. */
int32_t *rx_engine_enemy_face_control(RxEngineRoom *room,size_t enemy,size_t *bytes);
/* Owned, detached facial mesh staging; not a published actor exp1 context. */
RxFaceMesh *rx_engine_enemy_face_mesh(RxEngineRoom *room,size_t enemy);
RxFacePose *rx_engine_enemy_face_pose(RxEngineRoom *room,size_t enemy);
MN_WORK *rx_engine_enemy_motions(RxEngineRoom *room, size_t enemy);

typedef struct {
    NJS_TEXLIST *list;
    unsigned char *data;
    int effect; // -1 for model textures
    unsigned type, id, version, ordinal;
} RxEngineRoomTexture;

/* Stage a freshly loaded room once, before publishing its views. */
const char *rx_engine_room_stage(RxEngineRoom *room);
const RxEngineRoomTexture *rx_engine_room_textures(RxEngineRoom *room, size_t *count);
MN_WORK *rx_engine_room_empty_enemy_motion(RxEngineRoom *room, unsigned id);
void rx_engine_room_face_data(RxEngineRoom *room, unsigned char **mouth, unsigned char **lip);
void rx_engine_room_face_spans(RxEngineRoom*,const void**,size_t*,const void**,size_t*);
size_t rx_engine_room_model_bytes(const RxEngineRoom *room);
const char *bhDrawNativeRoom(const NJS_CNK_VIEW *view, RxRoomSubmit submit, void *context);

const char *rx_engine_player_load(const void *data, size_t bytes,
                                  const RxPlayerClipSource clips[RX_PLAYER_CLIPS],
                                  RxEnginePlayer **out);
void rx_engine_player_destroy(RxEnginePlayer *player);
NJS_TEXLIST *rx_engine_player_textures(RxEnginePlayer *player, unsigned char **data);
unsigned rx_engine_player_frames(const RxEnginePlayer *player, RxPlayerClip clip);
float rx_engine_player_speed(const RxEnginePlayer *player, RxPlayerClip clip, unsigned frame);
void rx_engine_player_bind(RxEnginePlayer *player, BH_PWORK *actor);
/* Evaluate the original MN_WORK through bhSetMotion, then skin the body.
   The bound actor and all its borrowed views expire when player is destroyed. */
const char *rx_engine_player_pose(RxEnginePlayer *player, BH_PWORK *actor,
                                  RxPlayerClip clip, int fixed_frame, int step);
/* Evaluate the actor's current native motion, including equipped-weapon
   slots, and update skinning without replacing the original state fields. */
const char *rx_engine_player_motion(RxEnginePlayer *player, BH_PWORK *actor, int mode);
const char *rx_engine_player_clips(RxEnginePlayer *player,
                                  const RxPlayerClipSource clips[RX_PLAYER_CLIPS], int publish);
const char *rx_engine_player_draw(RxEnginePlayer *player, const NJS_CNK_VIEW *view,
                                  RxRoomSubmit submit, void *context);

/* Stage before publishing. All disk-backed references are owned by weapon.
   Bind only after its textures are registered and bhSetWeapon initializes
   the two reserved object records. Clear those views before destruction. */
const char *rx_engine_weapon_load(const void *data, size_t bytes, unsigned player_nodes,
                                  RxEngineWeapon **out);
void rx_engine_weapon_destroy(RxEngineWeapon *weapon);
NJS_TEXLIST *rx_engine_weapon_textures(RxEngineWeapon *weapon, unsigned side, unsigned char **data);
void rx_engine_weapon_bind(RxEngineWeapon *weapon, BH_PWORK *actor, O_WRK objects[2]);
const char *rx_engine_weapon_draw(RxEngineWeapon *weapon, unsigned side,
                                  const NJS_CNK_VIEW *view, RxRoomSubmit submit, void *context);


/* Original scripted player motion setup; validates native provenance before
   original event/motion/model kernels run. Does not advance per-frame skinning. */
MN_WORK *rx_engine_player_motion_bank(RxEnginePlayer*);
unsigned char *rx_engine_player_translation_bank(RxEnginePlayer*);
const char *rx_engine_player_walk_translation_check(RxEnginePlayer*,unsigned,unsigned);
const char *rx_engine_player_locomotion_translation_check(RxEnginePlayer*,unsigned,unsigned);
const char *rx_engine_player_script_motion_check(RxEnginePlayer*,RxEngineWeapon*,BH_PWORK*,unsigned,unsigned,int);

const char *rx_engine_room_camera_init_check(RxEngineRoom*);

const char *rx_engine_room_script_motion_check(RxEngineRoom*,const unsigned char*,BH_PWORK*);
const char *rx_engine_room_actor_model_check(RxEngineRoom*,BH_PWORK*);
const char *rx_engine_room_interaction_light_check(RxEngineRoom*);

const char *rx_engine_room_retain_actor_work(RxEngineRoom*);
const char *rx_engine_room_item_speed_check(RxEngineRoom*,unsigned);
/* Validates the whole selected room batch before invoking any actor callback. */
const char *rx_engine_room_actors_tick(RxEngineRoom*);

const char *rx_engine_room_sub_control_check(RxEngineRoom*,unsigned,BH_PWORK*);

const char *rx_engine_room_event_sound_check(RxEngineRoom*,unsigned,unsigned,unsigned);

const char *rx_engine_room_message_data(RxEngineRoom*,const unsigned char**,size_t*,unsigned*);

size_t rx_engine_room_actor_count(const RxEngineRoom*);
NJS_TEXLIST *rx_engine_room_actor_textures(RxEngineRoom*,size_t);
/* Draw-stage publication from current world joints; never advances motion. */
const char *rx_engine_room_actors_publish(RxEngineRoom*);
const char *rx_engine_room_actor_draw(RxEngineRoom*,size_t,const NJS_MATRIX*,const NJS_CNK_VIEW*,RxRoomSubmit,void*);

const char *rx_engine_player_tick_check(RxEnginePlayer*,RxEngineWeapon*,BH_PWORK*);
const char *rx_engine_player_publish(RxEnginePlayer*,BH_PWORK*);
int rx_engine_player_world_pose(const RxEnginePlayer*);

const char *rx_engine_room_player_floor_check(RxEngineRoom*);
const char *rx_engine_room_player_walk_walls_check(RxEngineRoom*);

const char *rx_engine_room_player_spawn_check(RxEngineRoom*);

/* Bounded original indexed PVP stream and shared-palette destinations. */
int rx_engine_validate_palette_pictures(const unsigned char*,size_t,unsigned);
