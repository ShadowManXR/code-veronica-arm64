#include "event_movie.h"
#include "movie_port.h"
#include "types.h"
#include "main.h"
#include "sdfunc.h"
#include "system.h"
#include <stdint.h>
extern void bhStartEventMovie(void);
extern unsigned char MovieTypeDef[22];
static const void *owner;
static SYS_WORK *system_owner;
static ROM_WORK *room_owner;
static RxMovie *owned_movie;
static int running;
static unsigned char *arena_mark;
const char *rx_event_movie_bind(const void *next)
{
    if(owner==next&&next&&!running&&sys==system_owner&&rom==room_owner&&!rx_game_movie_active())return NULL;
    if(!next||!sys||!rom||owner||rx_game_movie_active())return "Occupied or missing native event movie context";
    owner=next;system_owner=sys;room_owner=rom;owned_movie=NULL;running=0;return NULL;
}
int rx_event_movie_running(void){return running;}
const char *rx_event_movie_status_check(void)
{
    if(!owner||sys!=system_owner||rom!=room_owner)return "Movie event requires its owned native room context";
    if((rx_game_movie_active()!=NULL)!=(CheckPlayEndMovie()!=0))return "Native movie status no longer matches the decoder";
    if(rx_game_movie_active()!=owned_movie)return "Foreign decoder occupies the event movie context";
    return NULL;
}
const char *rx_event_movie_check(unsigned number)
{
    const char *error=rx_event_movie_status_check();if(error)return error;
    if(number>=22||(MovieTypeDef[number]&31)>=3||(unsigned)sys->mvi_tp>3||!rx_game_movie_available(number))return "Event movie requires a supported owned disc member";
    return NULL;
}
const char *rx_event_movie_tick(void)
{
    const char *error=rx_event_movie_status_check();if(error)return error;
    if(sys->cb_flg&0x4000000){
        if(running)return "Overlapping original event movie request";
        error=rx_event_movie_check(sys->mvi_no);if(error)return error;
        uintptr_t aligned=((uintptr_t)sys->memp+63)&~(uintptr_t)63;
        if(aligned<(uintptr_t)sys->memp||aligned>(uintptr_t)sys->endp)return "Event movie arena alignment exceeds native owner";
        bhStartEventMovie();running=1;MovieInfo.MovieSystemLastError=0;
    }
    if(!running)return NULL;
    if(sys->mvi_md>6||(sys->mvi_md&&sys->mvi_flg))return "Unsupported native movie system phase or texture eviction";
    unsigned phase=sys->mvi_md;
    if(phase&&sys->mvi_memp!=arena_mark)return "Foreign original event movie arena mark";
    if(sys->mvi_md==2){StopBgm(0);StopVoice(0);}
    bhSysCallMovie();owned_movie=rx_game_movie_active();
    if(!phase)arena_mark=sys->mvi_memp;
    if(MovieInfo.MovieSystemLastError){PlayStopMovieEx(0);owned_movie=NULL;return "Original event movie decoder failed";}
    if(phase==6){running=0;owned_movie=NULL;}
    return NULL;
}
void rx_event_movie_end(const void *current)
{
    if(owner!=current)return;
    if(running){PlayStopMovieEx(0);if(sys==system_owner){sys->sp_flg=sys->mvi_spb;sys->ts_flg=sys->mvi_tsb;}}
    owner=NULL;system_owner=NULL;room_owner=NULL;owned_movie=NULL;arena_mark=NULL;running=0;
}
