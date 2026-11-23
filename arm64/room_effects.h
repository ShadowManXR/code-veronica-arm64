#pragma once
#include "types.h"
#include <stddef.h>

typedef struct RxRoomEffects RxRoomEffects;
typedef struct {
    size_t record;
    unsigned id;
    const char *reason;
} RxRoomEffectFault;

/* Prepare through the original constructors/linker without changing the live
   pool, mapping, RDT records or actors. Initialize *out to NULL. */
const char *rx_room_effects_prepare(RxRoomEffects **out);
void rx_room_effects_destroy(RxRoomEffects *effects);
size_t rx_room_effects_count(const RxRoomEffects *effects);
const O_WRK *rx_room_effects_work(const RxRoomEffects *effects, size_t record);
const EF_WRK *rx_room_effects_record(const RxRoomEffects *effects, size_t record);
int rx_room_effects_slot(const RxRoomEffects *effects, size_t record);
/* Publication only transfers work. Check runtime support before enabling the
   frame loop; unsupported effects must not be silently skipped. */
const char *rx_room_effects_check_live(const RxRoomEffects *effects, RxRoomEffectFault *fault);
const char *rx_room_effects_publish(RxRoomEffects *effects);
