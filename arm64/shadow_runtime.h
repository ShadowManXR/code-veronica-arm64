#pragma once
#include "types.h"

/* Narrow original effect backend: other effect IDs remain explicit errors. */
void rx_shadows_reset(void);
const char *rx_shadow_validate(const O_WRK *shadow);
const char *rx_shadow_step(O_WRK *shadow);
const char *rx_shadows_tick(void);
