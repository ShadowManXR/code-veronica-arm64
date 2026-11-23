#include "types.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "event_audio.h"
#include "main.h"
#include "sdfunc.h"
#include "sdc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int CurrentBgmNo,CurrentBgmVolume,NextBgmVolume,ReqFadeBgmNo;
extern int CurrentBgSeNo[2],ReqFadeBgSe[2];
extern NO_NAME_30 BgSePrmBuf[2];
extern unsigned Event_T_timer;
typedef struct {int number,volume,next,request,bg[2],flags[2];NO_NAME_30 parameters[2];} State;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL sound request: %s\n",why);exit(1);}}
static State get(void)
{
    State s={CurrentBgmNo,CurrentBgmVolume,NextBgmVolume,ReqFadeBgmNo,{CurrentBgSeNo[0],CurrentBgSeNo[1]},
             {ReqFadeBgSe[0],ReqFadeBgSe[1]},{BgSePrmBuf[0],BgSePrmBuf[1]}};return s;
}
static void set(State s)
{
    CurrentBgmNo=s.number;CurrentBgmVolume=s.volume;NextBgmVolume=s.next;ReqFadeBgmNo=s.request;
    memcpy(CurrentBgSeNo,s.bg,sizeof(s.bg));memcpy(ReqFadeBgSe,s.flags,sizeof(s.flags));memcpy(BgSePrmBuf,s.parameters,sizeof(s.parameters));
}
static void expect(State wanted,const char *why){State actual=get();check(!memcmp(&actual,&wanted,sizeof(actual)),why);}
static void run(void (*execute)(const unsigned char *,size_t),const unsigned char *code,size_t bytes)
{
    unsigned char entry[32]={0};check(bytes+8<=sizeof(entry),"request fixture capacity");memcpy(entry,code,bytes);
    const unsigned char marker[]={5,1,30,0,0,0,0,0};memcpy(entry+bytes,marker,sizeof(marker));execute(entry,bytes+sizeof(marker));
    check(sys->ev_flg[0]&2,"request handler did not advance/continue to following command");
}
static void reject(const unsigned char *code,size_t bytes)
{
    unsigned char data[32]={8,0,0,0};data[4]=(unsigned char)(8+bytes);memcpy(data+8,code,bytes);
    RxRoomScripts scripts;check(!rx_room_scripts_open(data,bytes+10,&scripts),"malformed request fixture");
    void *old=rom->evtp;rom->evtp=&scripts;Event_T_timer=123;State before=get();
    check(rx_events_start_room()!=NULL&&Event_T_timer==123,"bad request reached original handler");expect(before,"bad request mutated deferred state");
    rx_events_reset();rom->evtp=old;
}
void rx_check_sound_requests(void (*execute)(const unsigned char *,size_t),const unsigned char *retail)
{
    check(retail[0]==0x93&&retail[1]==0,"original BgmOff2 bytes differ");State saved=get(),seed;
    memset(&seed,0x55,sizeof(seed));set(seed);SDS_INFO midi[8];memcpy(midi,MidiInfo,sizeof(midi));int queue=rx_sound_queue_peek();
    InitGameSoundRequestState();State initial={-1,-127,-127,0,{-1,-1},{0,0},{{0},{0}}};expect(initial,"original request initialization differs");
    check(!memcmp(midi,MidiInfo,sizeof(midi))&&queue==rx_sound_queue_peek(),"metadata initialization touched MIDI/driver transport");
    const int masks[]={0,1,2,3,8,-1};
    for(unsigned extended=0;extended<2;++extended)for(unsigned id=0;id<256;++id)
        for(unsigned previous=0;previous<3;++previous)for(unsigned mask=0;mask<6;++mask) {
            unsigned char code[]={extended?0xa7:0x95,(unsigned char)id,(unsigned char)(id*7),0xff};
            int volume=extended?-(int)code[2]:-45;State before=seed;
            before.number=previous?(int)id:(int)(id^1);before.volume=previous==2?volume:123;
            before.request=masks[mask];set(before);State wanted=before;
            wanted.number=(int)id;wanted.volume=volume;if(!previous)wanted.request=8;else if(previous==1)wanted.request=2;
            run(execute,code,sizeof(code));expect(wanted,"original deferred music number/volume/request precedence differs");
            check(queue==rx_sound_queue_peek(),"deferred music request emitted an immediate sound command");
        }
    for(unsigned padding=0;padding<256;++padding) {
        set(seed);unsigned char code[]={0x93,(unsigned char)padding};if(!padding)memcpy(code,retail,2);
        State wanted=seed;wanted.number=-1;wanted.volume=wanted.next=-127;wanted.request=1;
        run(execute,code,sizeof(code));expect(wanted,"original deferred music stop/padding semantics differ");
    }
    const unsigned sounds[]={0,1,255,256,32767,32768,65535};
    for(unsigned slot=0;slot<2;++slot)for(unsigned sound=0;sound<7;++sound)
        for(unsigned same=0;same<2;++same)for(unsigned mask=0;mask<6;++mask) {
            State before=seed;before.bg[slot]=same?(int)sounds[sound]:-1;before.flags[slot]=masks[mask];set(before);State wanted=before;
            if(!same){wanted.bg[slot]=(int)sounds[sound];wanted.flags[slot]|=2;}
            unsigned char code[]={0x94,(unsigned char)slot,(unsigned char)sounds[sound],(unsigned char)(sounds[sound]>>8)};
            run(execute,code,sizeof(code));expect(wanted,"original deferred ambience unsigned ID/OR flags/slot isolation differs");
            set(before);wanted=before;wanted.bg[slot]=-1;wanted.flags[slot]=1;
            run(execute,(const unsigned char[]){0x92,(unsigned char)slot},2);expect(wanted,"original deferred ambience stop failed to replace flags");
        }
    for(unsigned slot=2;slot<256;++slot){reject((const unsigned char[]){0x92,(unsigned char)slot},2);reject((const unsigned char[]){0x94,(unsigned char)slot,0,0},4);}
    const unsigned char commands[][4]={{0x92,0},{0x93,0},{0x94,0,0,0},{0x95,0,0,0},{0xa7,0,0,0}};
    for(unsigned i=0;i<5;++i)for(unsigned bytes=1;bytes<(i<2?2u:4u);++bytes)reject(commands[i],bytes);
    set(saved);
    puts("PASS original deferred BGM/ambience commands: initialization, number/volume precedence, pending flags, unsigned IDs, both slots, padding and malformed-input preservation; no immediate stop/fade");
}
