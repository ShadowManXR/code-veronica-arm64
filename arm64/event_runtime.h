#pragma once

#include <stddef.h>

typedef struct {
    const char *reason;
    size_t offset;
    unsigned opcode;
} RxEventFault;

/* The active ROM owns bytecode. Reset before replacing or destroying it. */
void rx_events_reset(void);
const char *rx_events_validate_room(RxEventFault *fault);
const char *rx_events_start_room(void);
const char *rx_events_tick(void);
const char *rx_events_run_tasks(void);
const RxEventFault *rx_events_fault(void);

/* Used by the original event.c dispatch sites in the native core. */
unsigned rx_event_dispatch(void);
