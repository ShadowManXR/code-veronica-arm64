#pragma once
/* Original request/transition components only. A live caller must also supply
   the original status task and its owned resources before switching the host. */
const char *rx_inventory_requested(int *requested);
const char *rx_inventory_begin_transition(int *entered);
