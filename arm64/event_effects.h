#pragma once
#include "types.h"
#include <stddef.h>
typedef const char *(*RxEffectModeCheck)(const O_WRK*,unsigned);
const char *rx_event_effects_bind(const void*,EF_WRK*,const EF_WRK*,const O_WRK*,const int*,size_t,RxEffectModeCheck);
void rx_event_effects_retire(const void*);
const char *rx_event_effect_mode_check(unsigned,unsigned);
/* Resolve only published, persistent room effects; never arbitrary pool work. */
const char *rx_event_effect_work(unsigned,O_WRK**);
const char *rx_event_effect_work_check(const void*);
const char *rx_event_effect_clear_check(void);
