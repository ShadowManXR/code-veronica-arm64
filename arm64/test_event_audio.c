#include "event_audio.h"
#include "audio/snd.h"
#include "sdc.h"
#include "sdfunc.h"
#include "ps2_sg_sd.h"
#include "ps2_snddrv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
extern int MaxSlotEventSe;
extern int EventVibrationMode;
extern int CurrentBgmNo,CurrentBgmVolume,NextBgmVolume,ReqFadeBgmNo,CurrentBgSeNo[2],ReqFadeBgSe[2];
static void check(int ok,const char *why) {if(!ok){fprintf(stderr,"FAIL event audio: %s\n",why);exit(1);}}
static void put32(unsigned char *p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(i*8));}
static void play(unsigned port){unsigned char c[]={0x20,(unsigned char)port,0,127,0};snd_synth_command(c,5);}
void rx_check_event_audio(void (*execute)(const unsigned char *,size_t),const unsigned char *retail_stop)
{
    check(retail_stop[0]==0x18&&retail_stop[1]==3,"original RM_0000 SeOff bytes differ");
    check(rx_event_audio_check(0)!=NULL,"uninitialized driver passed readiness");
    SetEventVibrationMode(17);
    check(!rx_event_audio_init(0),"headless sound initialization");
    check(!EventVibrationMode&&CurrentBgmNo==-1&&CurrentBgmVolume==-127&&NextBgmVolume==-127&&!ReqFadeBgmNo&&
          CurrentBgSeNo[0]==-1&&CurrentBgSeNo[1]==-1&&!ReqFadeBgSe[0]&&!ReqFadeBgSe[1],"native sound did not restore original deferred request defaults");
    /* Independently built, bounded HD/SQ/SPU2 bank: constant looping ADPCM,
       fast attack/release, one sustained note. No retail offsets are assumed. */
    static unsigned char hd[640],bd[16],sq[128];
    const unsigned chunks[]={0x40,0xc0,0x140,0x1c0};
    for(unsigned i=0;i<4;++i){put32(hd+0x24+i*4,chunks[i]);put32(hd+chunks[i]+12,0);put32(hd+chunks[i]+16,32);}
    put32(hd+0x60,0x1e0);hd[0x64]=1;hd[0x65]=20;hd[0x66]=128;hd[0x67]=64;
    hd[0x242]=0;hd[0x244]=127;hd[0x250]=128;hd[0x251]=64;
    hd[0xe1]=0;hd[0xe2]=127;hd[0xe3]=1;
    hd[0x162]=0;hd[0x164]=127;hd[0x16b]=60;hd[0x16d]=64;hd[0x170]=128;
    hd[0x172]=15;hd[0x174]=0xc0;hd[0x175]=0x5f;hd[0x189]=3;
    hd[0x1e4]=0x80;hd[0x1e5]=0xbb;
    bd[0]=4;bd[1]=7;memset(bd+2,0x77,14);
    put32(sq+0x24,0x40);put32(sq+0x4c,0);put32(sq+0x50,32);put32(sq+0x60,8);sq[0x64]=96;
    const unsigned char note[]={0,0x90,60,127,0x83,0x80,0,0xff,0x2f,0};memcpy(sq+0x68,note,sizeof(note));
    SndBanks saved=g_snd_banks;g_snd_banks.midi_hd[0]=(SndBlob){hd,sizeof(hd),sizeof(hd)};
    g_snd_banks.bd[0]=(SndBlob){bd,sizeof(bd),sizeof(bd)};g_snd_banks.sq[0]=(SndBlob){sq,sizeof(sq),sizeof(sq)};
    int16_t pcm[800*2];
    for(unsigned slot=0;slot<5;++slot) {
        snd_synth_reset_native();unsigned port=7-slot;play(port);play(0);rx_event_audio_render(pcm,800);
        check(snd_synth_midi_active()==((1u<<port)|1),"sustained SQ ports did not start");
        int baseline=pcm[1598];check(baseline>100,"shared ADPCM/ADSR mixer produced no PCM");
        execute((const unsigned char[]){0x93,255,0,0},4);
        check(ReqFadeBgmNo==1&&CurrentBgmNo==-1&&CurrentBgmVolume==-127&&NextBgmVolume==-127&&
              snd_synth_midi_active()==((1u<<port)|1),"deferred BgmOff2 performed an immediate native sound stop");
        rx_event_audio_render(pcm,800);check(pcm[1598]==baseline,"deferred BgmOff2 changed existing synth PCM");
        unsigned char code[]={0x18,(unsigned char)slot,0,0};
        if(slot==3)memcpy(code,retail_stop,2);
        execute(code,sizeof(code));
        check(snd_synth_midi_active()==1&&rx_sound_queue_peek()<0,"original event stop targeted wrong port or left pending commands");
        rx_event_audio_render(pcm,800);check(pcm[1598]>0&&abs(pcm[1598]*2-baseline)<=2,"stop did not release only the selected audible voice");
        unsigned char stop[]={0x21,0};snd_synth_command(stop,2);rx_event_audio_render(pcm,800);
        for(unsigned i=0;i<1600;++i)check(pcm[i]==0||i<8,"stopped ADPCM voice did not settle to silence");
    }
    for(unsigned limit=0;limit<5;++limit) for(unsigned slot=limit+1;slot<256;++slot) {
        MaxSlotEventSe=(int)limit;unsigned char code[]={0x18,(unsigned char)slot,0,0};execute(code,sizeof(code));
        check(rx_sound_queue_peek()<0,"out-of-allocation original stop queued a command");
    }
    MaxSlotEventSe=4;SDMIDI old_handle=MidiHandle[7];MidiHandle[7]=(SDMIDI)(uintptr_t)0x100000004ull;
    check(rx_event_audio_check(0)!=NULL,"foreign high MIDI handle accepted");MidiHandle[7]=old_handle;
    SDHANDLE old_work=*old_handle;*old_handle=NULL;check(rx_event_audio_check(0)!=NULL,"null sound work accepted");*old_handle=old_work;
    unsigned old_port=((SND_WORK *)old_work)->port_num;((SND_WORK *)old_work)->port_num=10;
    check(rx_event_audio_check(0)!=NULL,"out-of-range MIDI port accepted");((SND_WORK *)old_work)->port_num=old_port;
    MidiInfo[7].Flag=0;execute((const unsigned char[]){0x18,0,0,0},4);check(rx_sound_queue_peek()<0,"inactive MIDI slot queued stop");MidiInfo[7].Flag=1;
    for(unsigned i=0;i<128;++i)check(!SdrBgmStop(i%10),"original sound queue filled early");
    check(rx_event_audio_check(0)!=NULL&&SdrBgmStop(0)==-1,"full original queue accepted extra stop");
    check(!rx_event_audio_deliver()&&rx_sound_queue_room(),"full sound queue failed wraparound drain");
    for(unsigned i=0;i<256;++i){check(!SdrBgmStop(i%10),"queue wrap producer");check(!rx_event_audio_deliver(),"queue wrap delivery");}
    check(!SdrBgmStop(255)&&rx_event_audio_deliver()!=NULL,"invalid queued port was silently consumed");rx_sound_queue_reset();
    MaxSlotEventSe=5;check(rx_event_audio_check(0)!=NULL,"corrupt original allocation accepted");MaxSlotEventSe=4;
    rx_event_audio_end();g_snd_banks=saved;
    check(!rx_event_audio_init(1),"Quest AAudio stream failed");
    for(unsigned tries=0;tries<1000&&!rx_event_audio_frames();++tries){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
    check(rx_event_audio_frames()>0,"Quest AAudio callback did not render PCM");rx_event_audio_end();
    check(!rx_event_audio_init(0),"sound restart failed");
    check(!snd_synth_midi_active()&&rx_sound_queue_peek()<0,"restart retained playback or queue state");
    puts("PASS original SeOff -> slot mapping -> StopMidi -> sdMidiStop -> SdrBgmStop queue -> shared SQ/ADPCM PCM release, isolation, bounds/full/wrap and Quest AAudio callback");
}
