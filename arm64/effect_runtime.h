#pragma once
#include "types.h"
#include "room_engine.h"
#include <stdint.h>

void rx_effects_reset(void);
const char *rx_effect_validate(const O_WRK *effect);
const char *rx_effects_tick(void);

const char *rx_effects_sync_palette(void);
uint64_t rx_effects_palette_generation(void);

const char *rx_effects_load_common(RxEngineEffects *effects);

const char *rx_effect_mode_check(const O_WRK*,unsigned);
