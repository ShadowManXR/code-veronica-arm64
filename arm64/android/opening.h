#pragma once

#include "room_engine.h"
#include "player.h"
#include "event_runtime.h"
#include "shadow_draw.h"
#include "room_effects.h"
#include "particle_draw.h"

#include <GLES3/gl3.h>

typedef struct {
    unsigned char *arena;
    void *texture_heap, *pixel_work;
    RxEnginePlayer *player;
    RxEngineWeapon *weapon;
    unsigned weapon_number;
    RxEngineEffects *effects;
    RxShadowDraw *shadows;
    RxParticleDraw *particles;
    RxRoomEffects *room_effects;
    EXP_WORK player_work;
    unsigned pose_flags;
    int events_ready;
    int commands_ready;
    RxPlayerClip pose_clip;
    unsigned texture_count, room_texture_count, pose_frame;
    GLuint program, vao, vertices, elements, *textures, *actor_textures;
    unsigned actor_texture_count;
    GLint matrix_uniform, projection_uniform, material_alpha_uniform, texture_alpha_uniform;
} RxOpening;

const char *rx_opening_start(RxOpening *opening, unsigned difficulty);
void rx_opening_end(RxOpening *opening);
void rx_opening_close_graphics(RxOpening *opening);
const char *rx_opening_present(RxOpening *opening);
void rx_opening_read_controls(void);
void rx_opening_read_runtime_controls(void);
const char *rx_opening_move_player(RxOpening *opening);
const char *rx_opening_sync_weapon(RxOpening *opening);
void rx_opening_clear_controls(void);
/* A non-byte opcode identifies missing live runtime rather than bytecode. */
enum { RX_OPENING_LIVE_RUNTIME = 256 };
const char *rx_opening_check_events(RxEventFault *fault);
