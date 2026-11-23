#include "game_runtime.h"
#include "game.h"
#include "main.h"
#include "room.h"

const char *rx_game_flags_tick(void)
{
    if(!sys)return "Original game transitions require native system work";
    /* Validate the only pointer write before any of the original scalar
       transitions. All other branches touch system scalars only. */
    if((sys->cb_flg&8)&&(sys->exm_attr&2)){
        const char *error=bhCheckNativeInteractionLight();if(error)return error;
    }
    bhGameMessageTransitions();
    return NULL;
}
