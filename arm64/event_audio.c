#include "event_audio.h"
#include "adx_native.h"
#include "adxwrap.h"
#include "main.h"
#include <math.h>
#include "audio/snd.h"
#include "sdc.h"
#include "sdfunc.h"
#include "ps2_sg_sd.h"
#include "ps2_snddrv.h"
#include "ps2_dummy.h"
#include "ps2_NaColi.h"
#include <limits.h>
#include <aaudio/AAudio.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

SndBanks g_snd_banks;
extern SND_WORK __snd_work__[40];
extern SDS_INFO SeInfo[20];
extern SDHANDLE *__snd_handle__[40];
extern SDMIDI __midi_handle_top;
extern SDSHOT __shot_handle_top,SeHandle[20];
extern int __shot_value,PlayerFootStepSwitch[3];
extern unsigned short use_se_info[6],req_se_info[6];
extern NO_NAME_31 GsSlotInfoSe[20],GsSlotInfoMi[8];
extern NJS_POINT3 CameraPos;
extern char CurrentRoomFxLevel;
extern void ExecNativeSoundPositions(void);
extern int __midi_value,__sg_sd_snd_init__,MaxSlotEventSe;
extern ADXT_INFO AdxTInfo[4];
extern ADX_WORK AdxDef[2];
extern unsigned AdxPlayFlag[2];
extern int MaxAdxStreamCnt,AdxStreamSleepFlag;
static int ready,bgm_mounted,voice_mounted,voice_disc;
static AAudioStream *stream;
static atomic_uint_fast64_t frames_rendered;
static atomic_int suspended;
void rx_event_audio_pause(int paused){atomic_store(&suspended,!!paused);}
static aaudio_data_callback_result_t audio(AAudioStream *s,void *ctx,void *data,int32_t frames)
{
    (void)s;(void)ctx;rx_event_audio_render(data,frames);atomic_fetch_add(&frames_rendered,(unsigned)frames);return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
uint64_t rx_event_audio_frames(void) { return atomic_load(&frames_rendered); }
const char *rx_event_audio_init(int device)
{
    if(ready)return NULL;
    atomic_store(&frames_rendered,0);atomic_store(&suspended,0);rx_sound_queue_reset();memset(__snd_work__,0,sizeof(__snd_work__));
    /* Preserve native pointer indirection used by the original sound API. */
    for(unsigned i=0;i<10;++i) __snd_handle__[i]=(SDHANDLE *)&__snd_work__[i];
    __midi_handle_top=(SDMIDI)__snd_handle__;__midi_value=8;__sg_sd_snd_init__=1;
    memset(SeInfo,0,sizeof(SDS_INFO)*20);memset(MidiInfo,0,sizeof(SDS_INFO)*8);memset(MidiHandle,0,sizeof(SDMIDI)*8);
    for(unsigned i=0;i<8;++i) RegistMidiSlot(i);
    __shot_value=20;__shot_handle_top=(SDSHOT)(__snd_handle__+10);
    memset(SeHandle,0,sizeof(SDSHOT)*20);memset(use_se_info,0,sizeof(use_se_info));memset(req_se_info,0,sizeof(req_se_info));
    memset(GsSlotInfoSe,0,sizeof(GsSlotInfoSe));memset(GsSlotInfoMi,0,sizeof(GsSlotInfoMi));memset(PlayerFootStepSwitch,0,sizeof(PlayerFootStepSwitch));
    for(unsigned i=0;i<20;++i){__snd_handle__[10+i]=(SDHANDLE *)(__snd_work__+8+i);RegistSeSlot(i);}
    if(plp)ExecNativeSoundPositions();
    InitGameSoundRequestState();SetEventVibrationMode(0);CustomMidiSlotDef(1,4);snd_synth_init(0);
    memset(AdxTInfo,0,sizeof(*AdxTInfo)*4);memset(AdxPlayFlag,0,sizeof(*AdxPlayFlag)*2);
    rx_adx_reset();RegistAdxStreamEx(2,4,AdxDef);
    if(!rx_adx_slot_handle(0,AdxTInfo[0].Handle)||!rx_adx_slot_handle(1,AdxTInfo[1].Handle)||AdxTInfo[1].Handle->maxnch!=1)return "Cannot initialize original native ADX streams";
    if(device) {
        AAudioStreamBuilder *builder=NULL;
        if(AAudio_createStreamBuilder(&builder)!=AAUDIO_OK)return "Cannot create event audio builder";
        AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setFormat(builder,AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setSampleRate(builder,48000);
        AAudioStreamBuilder_setChannelCount(builder,2);
        AAudioStreamBuilder_setDataCallback(builder,audio,NULL);
        aaudio_result_t result=AAudioStreamBuilder_openStream(builder,&stream);
        AAudioStreamBuilder_delete(builder);
        if(result!=AAUDIO_OK)return "Cannot open native event audio stream";
        if(AAudioStream_getSampleRate(stream)!=48000 || AAudioStream_requestStart(stream)!=AAUDIO_OK) {
            AAudioStream_close(stream);stream=NULL;return "Cannot start 48 kHz native event audio";
        }
    }
    ready=1;return NULL;
}
const char *rx_event_audio_check(unsigned slot)
{
    static const unsigned mapping[]={7,6,5,4,3};
    if(!ready)return "Original event audio requires native sound initialization";
    if(MaxSlotEventSe<0||MaxSlotEventSe>4)return "Invalid original event sound slot configuration";
    if(slot>(unsigned)MaxSlotEventSe)return NULL; /* Original rejected request returns 1 without touching a port. */
    unsigned bank=mapping[slot];
    if(!MidiInfo[bank].Flag)return NULL;
    uintptr_t handle=(uintptr_t)MidiHandle[bank],base=(uintptr_t)__snd_handle__;
    if(handle<base || handle-base>=10*sizeof(*__snd_handle__) || (handle-base)%sizeof(*__snd_handle__))
        return "Event sound references an absent native MIDI handle";
    uintptr_t work=(uintptr_t)*MidiHandle[bank],start=(uintptr_t)__snd_work__;
    if(work<start || work-start>=10*sizeof(*__snd_work__) || (work-start)%sizeof(*__snd_work__) ||
       ((SND_WORK *)work)->port_num>=10)return "Event sound references invalid native port work";
    if(!rx_sound_queue_room())return "Original sound command queue is full";
    return NULL;
}
const char *rx_event_audio_deliver(void)
{
    snd_synth_lock();
    for(int command;(command=rx_sound_queue_peek())>=0;) {
        unsigned port=((unsigned)command>>16)&255;
        signed char vol,pan;unsigned short pitch;rx_sound_queue_values(&vol,&pan,&pitch);
        unsigned kind=(unsigned)command>>24;
        uint8_t encoded[8]={(uint8_t)kind,(uint8_t)port,0,0,0,0,0,0};unsigned bytes=0;
        if(kind==0x21&&((unsigned)command&0xff00ffffu)==0x21000000u&&port<10)bytes=2;
        else if(kind==0x20&&port<10&&((unsigned)command>>8&255)<4){encoded[2]=(unsigned)command>>8;encoded[3]=command;encoded[4]=(uint8_t)vol;bytes=5;}
        else if(kind==0x27&&port<10&&((unsigned)command&65535)==0){encoded[4]=(uint8_t)vol;encoded[5]=(uint8_t)pan;encoded[6]=pitch;encoded[7]=pitch>>8;bytes=8;}
        if(kind<16&&port==2&&((unsigned)command&255)<16){
            unsigned program=(unsigned)command>>8&255;
            const char *error=kind==8?NULL:rx_event_shot_assets(program);
            if(error){snd_synth_unlock();return error;}
            encoded[2]=program;encoded[3]=command;bytes=4;
            if(kind&1)encoded[bytes++]=(uint8_t)vol;
            if(kind&2)encoded[bytes++]=(uint8_t)pan;
            if(kind&4){encoded[bytes++]=pitch>>8;encoded[bytes++]=pitch;}
        }
        /* Original StopSe on an idle handle encodes all-one request bits;
           the retail driver ignores the resulting absent port. */
        if(kind==8&&((unsigned)command&0xffffff)==0xffffff){rx_sound_queue_pop();continue;}
        if(!bytes){snd_synth_unlock();return "Unported original sound command in native MIDI transport";}
        snd_synth_post(encoded,bytes);rx_sound_queue_pop();
    }
    snd_synth_unlock();return NULL;
}
void rx_event_audio_render(int16_t *pcm,int frames) { if(frames<=0)return;if(atomic_load(&suspended)){memset(pcm,0,(size_t)frames*4);return;}snd_synth_render_pcm(pcm,frames);rx_adx_mix(pcm,(size_t)frames); }
const char *rx_event_bgm_mount(void)
{
    if(!ready)return "BGM requires initialized native sound";
    AFS_PATINFO archive[]={{"BGM1.AFS",0,128,NULL},{NULL,0,0,NULL}};
    if(CreatePartitionEx(archive))return "Cannot mount original BGM1 archive";
    PatId[0]=0;bgm_mounted=1;return NULL;
}
const char *rx_event_bgm_check(unsigned member,unsigned volume)
{
    if(sys->ss_flg&0x4000000)return NULL; /* Original PlayBgmEx is a no-op. */
    if(!ready||!bgm_mounted||PatId[0]!=0||!rx_adx_slot_handle(0,AdxTInfo[0].Handle)||MaxAdxStreamCnt!=2||AdxStreamSleepFlag)return "BGM requires its owned native stream and archive";
    if(volume>127)return "BGM volume exceeds original 128-entry attenuation table";
    return rx_adx_prepare(0,member);
}
const char *rx_event_bgm_stop_check(unsigned fade_byte)
{
    /* StopBgm does not test the sound-disabled flag. Unlike playback, it needs
       no archive/member, but deferred fading still requires its owned stream. */
    if(!ready||!rx_adx_slot_handle(0,AdxTInfo[0].Handle)||MaxAdxStreamCnt!=2||AdxStreamSleepFlag)
        return "BGM stop requires its owned native stream";
    if(fade_byte>255)return "BGM stop fade exceeds its original operand";
    if(fade_byte&&(!isfinite(AdxTInfo[0].Volume)||AdxTInfo[0].Volume<= -128||AdxTInfo[0].Volume>=1))
        return "BGM stop requires a bounded original fade volume";
    return NULL;
}
const char *rx_event_bgm_tick(void)
{
    if(!ready)return "BGM fade requires native sound initialization";
    if((sys->ss_flg&0x4000000)||atomic_load(&suspended))return NULL;
    /* A 30 Hz game step contains two original 60 Hz sound-server calls. Check
       each prospective table index, including float accumulation near endpoints. */
    for(unsigned tick=0;tick<2;++tick){
        for(unsigned i=0;i<4;++i)if(AdxTInfo[i].FadeFunc){
            ADXT_INFO *info=&AdxTInfo[i];float next=info->FadeCntMax?info->Volume-info->VolSpeed:info->VolLast;
            if(!rx_adx_slot_handle(i,info->Handle)||info->FadeCntMax<0||
               !isfinite(info->Volume)||!isfinite(info->VolSpeed)||!isfinite(info->VolLast)||
               info->VolLast< -127||info->VolLast>0||!isfinite(next)||next<= -128||next>=1)
                return "Invalid native original ADX fade state";
        }
        const char *se_error=rx_event_se_tick();if(se_error)return se_error;
        ExecAdxFadeManager();
    }
    return NULL;
}
void rx_event_audio_end(void)
{
    if(stream){AAudioStream_requestStop(stream);AAudioStream_close(stream);stream=NULL;}
    if(ready) {snd_synth_lock();snd_synth_reset_native();snd_synth_unlock();}
    rx_event_se_end();
    rx_adx_reset();memset(AdxTInfo,0,sizeof(*AdxTInfo)*4);memset(AdxPlayFlag,0,sizeof(*AdxPlayFlag)*2);
    ready=bgm_mounted=voice_mounted=0;MaxAdxStreamCnt=0;__sg_sd_snd_init__=0;rx_sound_queue_reset();
}

const char *rx_event_voice_mount(void)
{
    if(!ready)return "Voice requires native sound initialization";
    unsigned disc=(sys->ss_flg&1)?2:1;
    if(voice_mounted)return voice_disc==(int)disc&&PatId[1]==1?NULL:"Voice archive requires a native disc transition";
    char name[]="VOICE1.AFS";name[5]='0'+disc;
    AFS_PATINFO archive[]={{name,1,768,NULL},{NULL,0,0,NULL}};
    if(CreatePartitionEx(archive))return "Cannot mount original voice archive";
    PatId[1]=1;voice_disc=disc;voice_mounted=1;return NULL;
}
static const char *voice_stream_check(void)
{
    if(!ready||MaxAdxStreamCnt!=2||AdxStreamSleepFlag||!rx_adx_slot_handle(1,AdxTInfo[1].Handle)||AdxTInfo[1].Handle->maxnch!=1)
        return "Voice requires its owned native mono stream";
    return NULL;
}
const char *rx_event_voice_check(unsigned member,unsigned mode)
{
    const char *error=voice_stream_check();if(error)return error;
    if(!voice_mounted||PatId[1]!=1||voice_disc!=((sys->ss_flg&1)?2:1))return "Voice requires its mounted original disc archive";
    if(mode!=1)return "Original spatial voice mode is not ported yet";
    ADXT_INFO *info=AdxTInfo+1;
    if(!isfinite(info->Volume)||info->Volume<=-128||info->Volume>=1||
       !isfinite(info->LimitMaxVol)||info->LimitMaxVol< -127||info->LimitMaxVol>0||info->PanFunc)
        return "Invalid original voice fade or pan state";
    return rx_adx_prepare_channels(1,member,1);
}
const char *rx_event_voice_stop_check(unsigned fade)
{
    const char *error=voice_stream_check();if(error)return error;
    if(fade>255)return "Voice stop fade exceeds original operand";
    if(fade&&(!isfinite(AdxTInfo[1].Volume)||AdxTInfo[1].Volume<=-128||AdxTInfo[1].Volume>=1))return "Invalid original voice stop volume";
    return NULL;
}
extern void ExecNativeAdxStreamMonitor(void);
const char *rx_event_voice_monitor(void)
{
    if(atomic_load(&suspended))return NULL;
    const char *error=voice_stream_check();if(error)return error;
    if(!rx_adx_slot_handle(0,AdxTInfo[0].Handle))return "Original ADX monitor requires its owned music stream";
    if(plp)ExecNativeSoundPositions();
    ExecNativeAdxStreamMonitor();return NULL;
}

static const char *midi_server_check(void)
{
    if(!ready||__midi_value!=8||__midi_handle_top!=(SDMIDI)__snd_handle__||rx_sound_queue_free()<64)return "Invalid native event MIDI server state";
    for(unsigned i=0;i<8;++i){
        if(MidiHandle[i]!=(SDMIDI)(__snd_handle__+i)||*MidiHandle[i]!=(SDHANDLE)(__snd_work__+i))return "Foreign native MIDI server handle";
        SND_WORK *work=__snd_work__+i;
        if(work->port_num>=10||work->bank_num>=4||work->vol_timer||work->pan_timer||work->pitch_timer)return "Unported native MIDI SDK timer/bank/port";
        SDS_INFO *info=MidiInfo+i;
        if(!isfinite(info->Volume)||!isfinite(info->VolLast)||!isfinite(info->VolSpeed)||!isfinite(info->Pan)||!isfinite(info->PanLast)||!isfinite(info->PanSpeed)||
           info->Volume<= -128||info->Volume>=1||info->VolLast< -127||info->VolLast>0||info->Pan<= -129||info->Pan>=128||info->PanLast< -128||info->PanLast>127)return "Invalid original MIDI fade or pan state";
    }
    if(__shot_value!=20||__shot_handle_top!=(SDSHOT)(__snd_handle__+10))return "Foreign native shot handle table";
    for(unsigned i=0;i<20;++i){
        if(SeHandle[i]!=(SDSHOT)(__snd_handle__+10+i)||*SeHandle[i]!=(SDHANDLE)(__snd_work__+8+i)||SeInfo[i].Flag!=1)return "Foreign native shot server handle";
        SND_WORK *w=__snd_work__+8+i;SDS_INFO *info=SeInfo+i;
        if(w->channel_num< -1||w->channel_num>=16||w->port_num>=5||w->vol_timer||w->pan_timer||w->pitch_timer||w->pitch>16383)return "Invalid native shot SDK state";
        if(w->channel_num>=0&&(w->port_num!=2||w->bank_num>=5))return "Original shot bank/program is not ported yet";
        if(!isfinite(info->Volume)||info->Volume<=-128||info->Volume>=1||!isfinite(info->VolLast)||info->VolLast< -127||info->VolLast>0||!isfinite(info->VolSpeed)||
           !isfinite(info->Pan)||info->Pan< -128||info->Pan>127||!isfinite(info->PanLast)||info->PanLast< -128||info->PanLast>127||!isfinite(info->PanSpeed)||
           (info->FadeFunc&&info->FadeCntMax!=0)||(info->PanFunc&&info->PanCntMax!=0))return "Original timed shot fade/pan is not ported yet";
    }
    for(unsigned i=0;i<8;++i){SDS_INFO *info=MidiInfo+i;
        float volume=info->FadeFunc?(info->FadeCntMax>0?info->Volume-info->VolSpeed:info->VolLast):info->Volume;
        float pan=info->PanFunc?(info->PanCntMax>0?info->Pan-info->PanSpeed:info->PanLast):info->Pan;
        if(!isfinite(volume)||volume<= -128||volume>=1||!isfinite(pan)||pan<= -129||pan>=128)return "Original MIDI fade step exceeds native signed parameters";
    }
    return NULL;
}
const char *rx_event_se_check(const unsigned char *p)
{
    if(p[1]==1){
        if(p[2]>=3||p[7]||p[8]>=5||p[12]>1||p[3]>127||p[4]>127||p[3]==1||p[4]==1||p[5]==127||p[6]==127)return "Original footstep variant/fade/sentinel is not ported yet";
        const char *error=midi_server_check();if(error)return error;
        if(PlayerFootStepSwitch[p[2]]<0||PlayerFootStepSwitch[p[2]]>1)return "Invalid original footstep alternation";
        return rx_event_shot_assets(p[8]);
    }
    if(p[1]!=7)return "Original EasySESet sound family is not ported yet";
    if(p[2]>=5||MaxSlotEventSe!=4)return "EasySESet requires the native five-slot event configuration";
    if(p[3]>127||p[4]>127)return "EasySESet volume exceeds original signed attenuation range";
    const char *error=rx_event_audio_check(p[2]);if(error)return error;
    error=midi_server_check();if(error)return error;
    if(p[6]!=127&&p[7]){
        SDS_INFO *info=MidiInfo+7-p[2];float start=(int)p[5]-128;
        if(p[5]==127)start=(!info->PanFunc&&info->FadeFunc==2)?info->Pan:0;
        float delta=((int)p[6]-128)-start;
        /* CallNativeEventSe resets current pan to zero before the pan manager.
           Keep source trajectories inside the SDK's signed-byte conversion. */
        if(delta< -128||delta>127)return "EasySESet pan trajectory exceeds native signed-byte range";
    }
    if(!isfinite(MidiInfo[7-p[2]].Volume)||MidiInfo[7-p[2]].Volume<= -128||MidiInfo[7-p[2]].Volume>=1)return "Invalid native event MIDI volume state";
    return rx_event_se_assets(p[14]);
}
const char *rx_event_se_tick(void)
{
    if(!rx_event_se_mounted()||atomic_load(&suspended))return NULL;
    const char *check=midi_server_check();if(check)return check;
    snd_synth_lock();snd_synth_flush();get_iop_snddata.midi_info=snd_synth_midi_active();
    for(unsigned i=0;i<5;++i)get_iop_snddata.se_info[i]=snd_synth_se_active(i);
    sdSysServer();snd_synth_unlock();
    const char *error=rx_event_audio_deliver();if(error)return error;
    /* ExecSoundSynchProgram orders the SDK server/transport before fades.
       Stops requested by a fade stay queued for the next server phase. */
    ExecSoundFadeManager();ExecSoundPanManager();
    return NULL;
}

const char *rx_event_step_position_check(const void *position)
{
    const NJS_POINT3 *p=position;
    if(!p||cam.mtx!=cmmat||cam.ay< -65536||cam.ay>65536||CurrentRoomFxLevel)return "Footsteps require the native camera and supported room sound environment";
    const float values[]={p->x,p->y,p->z,CameraPos.x,CameraPos.y,CameraPos.z};
    for(unsigned i=0;i<6;++i)if(!isfinite(values[i])||fabsf(values[i])>1000000)return "Invalid original footstep position";
    for(unsigned i=0;i<16;++i)if(!isfinite((*cam.mtx)[i]))return "Invalid original footstep camera matrix";
    float distance=njDistanceP2P(&CameraPos,(NJS_POINT3*)p);
    if(!isfinite(distance)||distance>=INT_MAX||(int)distance==510)return "Original footstep distance exceeds attenuation table";
    unsigned angle=(unsigned)wadGetAngle(&CameraPos,cam.ay,(NJS_POINT3*)p)&65535;
    unsigned index=(0.005493164f*angle)/5.0f;
    if(index>=72)return "Original footstep angle exceeds its bounded native pan tables";
    return NULL;
}
