#pragma once
#include "types.h"
#include "player.h"
#include <stddef.h>
/* Borrow the published player's exact scalar work. Reset before either owner
   expires; this registry never encodes a host address into game scalar words. */
const char *rx_player_control_bind(BH_PWORK*,EXP_WORK*,size_t);
const char *rx_player_control_check(void);
void rx_player_control_reset(void);

#include "room_engine.h"
const char *rx_player_control_bind_models(RxEnginePlayer*,RxEngineWeapon*);
const char *rx_player_control_motion_check(const unsigned char*,int runtime);

const char *rx_player_control_model_check(void);

const char *rx_player_control_stand_check(void);

const char *rx_player_control_handoff_check(void);

const char *rx_player_control_bind_script_flags(unsigned*);
/* Selected original scripted states only. Combat/head/patch controllers and
   the normal-control handoff remain separate readiness requirements. */
const char *rx_player_control_script_check(void);
const char *rx_player_control_script_tick(void);
void rx_player_script_footstep(int,int,int);

const char *rx_player_control_place_room(void);
