#include "inventory_runtime.h"
#include "player_control_runtime.h"
#include "fade_runtime.h"
#include "system.h"
const char *rx_inventory_requested(int *requested)
{
    if(!requested)return "Missing inventory request result";
    const char *error=rx_player_control_check();if(error)return error;
    *requested=bhInventoryRequestCheck();return NULL;
}
const char *rx_inventory_begin_transition(int *entered)
{
    if(!entered)return "Missing inventory transition result";
    int requested;const char *error=rx_inventory_requested(&requested);if(error)return error;
    if(!requested){*entered=0;return NULL;}
    error=rx_fade_check();if(error)return error;
    bhBeginInventoryTask();*entered=1;return NULL;
}
