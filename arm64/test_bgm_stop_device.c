#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_audio.h"
#include "adx_native.h"
#include "adxwrap.h"
#include "sdfunc.h"
#include "main.h"
#include "room_scripts.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
extern ADXT_INFO AdxTInfo[4];extern unsigned AdxPlayFlag[2];
extern int CurrentBgmNo,CurrentBgmVolume,NextBgmVolume,ReqFadeBgmNo;
extern int MaxAdxStreamCnt,AdxStreamSleepFlag;
extern short AdxVolTbl[128];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL BGM stop: %s\n",why);exit(1);}}
static unsigned char data[32];static RxRoomScripts scripts;
static const char *execute(const unsigned char *command,size_t bytes){
 memset(data,0,sizeof(data));data[0]=8;data[4]=8+bytes+6;memcpy(data+8,command,bytes);
 const unsigned char marker[]={5,1,30,0,0,0};memcpy(data+8+bytes,marker,6);
 check(!rx_room_scripts_open(data,bytes+16,&scripts),"BGM stop script fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~2u;return rx_events_start_room();
}
static void start(void){check(!execute((unsigned char[]){0xa6,45,0,45},4)&&!rx_event_bgm_tick(),"playing original BGM fixture");}
static void stop(unsigned value){check(!execute((unsigned char[]){0x16,value},2)&&(sys->ev_flg[0]&2),"original stop dispatch/continuation");}
static void reject(unsigned value){ADXT_INFO before=AdxTInfo[0];int number=CurrentBgmNo,volume=CurrentBgmVolume,next=NextBgmVolume,request=ReqFadeBgmNo;
 check(execute((unsigned char[]){0x16,value},2)!=NULL,"invalid stop reached handler");check(!memcmp(&before,&AdxTInfo[0],sizeof(before))&&number==CurrentBgmNo&&volume==CurrentBgmVolume&&next==NextBgmVolume&&request==ReqFadeBgmNo,"invalid stop changed state");
}
static int16_t pcm[4800*2];
int main(int argc,char **argv){
 check(argc==2,"supply ISO OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"full opening must stay gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next opening dependency");
 const RxRoomScripts *original=rom->evtp;check(original->bytes>2154&&original->data[2152]==0x16,"actual original BgmOff bytes");unsigned char retail[2];memcpy(retail,original->data+2152,2);
 rx_event_audio_end();check(!rx_event_audio_init(0)&&!rx_event_bgm_mount(),"headless original music setup");
 unsigned count=0;const float levels[]={0,-25,-107,-127};
 for(unsigned state=0;state<2;++state)for(unsigned mute=0;mute<2;++mute)for(unsigned level=0;level<4;++level)for(unsigned fade=0;fade<256;++fade){
  sys->ss_flg&=~0x4000000u;start();if(!state)StopAdx(0);SetVolumeAdx(0,levels[level]);
  /* Pending fade is deliberately retained by the original immediate-stop path. */
  RequestAdxFadeFunction(0,1,80);ADXT_INFO before=AdxTInfo[0],wanted=before;
  if(mute)sys->ss_flg|=0x4000000u;ReqFadeBgmNo=0x55;NextBgmVolume=29;
  if(fade){int timer=fade*10,n=((timer/100)*30+(timer%100)*6/10)/2;if(!n)n=1;
   wanted.FadeCntMax=n;wanted.VolLast=-127;wanted.VolSpeed=(wanted.Volume+127)/n;wanted.VolSave=wanted.Volume;wanted.FadeFunc=2;
  }else wanted.Flag=0;
  unsigned play_before=AdxPlayFlag[0];stop(fade);
  check(CurrentBgmNo==-1&&CurrentBgmVolume==-127&&NextBgmVolume==-127&&ReqFadeBgmNo==0x55,"original stop metadata or deferred-request isolation");
  check(!memcmp(&wanted,&AdxTInfo[0],sizeof(wanted)),"original immediate/fade stop changed unrelated stream work");
  check(AdxPlayFlag[0]==(fade?play_before:0)&&GetAdxStatus(0)==(fade&&state?3:0),"original stop flags/status differ");
  if(mute){ADXT_INFO held=AdxTInfo[0];check(!rx_event_bgm_tick()&&!memcmp(&held,&AdxTInfo[0],sizeof(held)),"muted stop fade advanced");sys->ss_flg&=~0x4000000u;}
  /* Native delivery must retain original countdown and end-flag semantics. */
  for(unsigned tick=0;wanted.FadeFunc;++tick){check(tick<1000,"fade failed to terminate");for(unsigned sub=0;sub<2&&wanted.FadeFunc;++sub){wanted.Volume-=wanted.VolSpeed;if(--wanted.FadeCntMax<0){wanted.Volume=wanted.VolLast;wanted.Flag=0;wanted.FadeFunc=0;}}
   check(!rx_event_bgm_tick(),"native original stop fade tick");check(!memcmp(&wanted,&AdxTInfo[0],sizeof(wanted)),"original full stop fade timeline differs");
  }
  check(GetAdxStatus(0)==0&&AdxPlayFlag[0]==(fade?play_before:0),"original fade-complete play marker was rewritten");
  memset(pcm,0,sizeof(pcm));rx_event_audio_render(pcm,4800);for(unsigned i=0;i<9600;++i)check(!pcm[i],"stopped original BGM emitted PCM");++count;
 }
 printf("PASS %u original BgmOff full timelines: all fade bytes, active/idle, four levels, muted/unmuted, retained pending immediate fade, exact metadata/play flags and silence\n",count);
 /* Verify an intermediate fade reaches the mixer, at an unchanged source
    cursor, rather than just updating controller metadata. */
 sys->ss_flg&=~0x4000000u;start();ADXT_SetOutVol(AdxTInfo[0].Handle,0);
 for(unsigned i=0;i<10;++i){memset(pcm,0,sizeof(pcm));rx_adx_mix(pcm,4800);}
 int16_t reference[2048];memset(reference,0,sizeof(reference));rx_adx_mix(reference,1024);
 start();for(unsigned i=0;i<10;++i){memset(pcm,0,sizeof(pcm));rx_adx_mix(pcm,4800);}stop(70);
 int source,rate;ADXT_GetTime(AdxTInfo[0].Handle,&source,&rate);check(source==48000,"stop fade reset the source cursor");
 for(unsigned i=0;i<10;++i)check(!rx_event_bgm_tick(),"intermediate fade tick");
 memset(pcm,0,sizeof(pcm));rx_adx_mix(pcm,1024);double gain=pow(10.0,AdxVolTbl[(int)-AdxTInfo[0].Volume]/200.0);uint64_t full=0,reduced=0;
 for(unsigned i=0;i<2048;++i){check(abs(pcm[i]-(int)lrint(reference[i]*gain))<=1,"original fade attenuation did not reach PCM");full+=abs(reference[i]);reduced+=abs(pcm[i]);}
 check(full>10000&&reduced<full/3,"native stop fade did not attenuate music");
 puts("PASS original intermediate stop fade: source cursor retained; PCM matches attenuation table within one sample unit");
 sys->ss_flg&=~0x4000000u;start();check(!execute(retail,2),"actual RM_0000 BgmOff instruction");
 ADXT owned=AdxTInfo[0].Handle;AdxTInfo[0].Handle=(ADXT)(uintptr_t)1;reject(1);AdxTInfo[0].Handle=AdxTInfo[1].Handle;reject(0);AdxTInfo[0].Handle=owned;
 float volume=AdxTInfo[0].Volume;AdxTInfo[0].Volume=NAN;reject(1);AdxTInfo[0].Volume=volume;
 int streams=MaxAdxStreamCnt;MaxAdxStreamCnt=0;reject(1);MaxAdxStreamCnt=streams;AdxStreamSleepFlag=1;reject(0);AdxStreamSleepFlag=0;
 check(execute((unsigned char[]){0x16},1)!=NULL,"truncated stop executed");
 /* Original immediate stop can recover even if the volume is invalid; it does
    not dereference the attenuation table or silently cancel a pending fade. */
 start();AdxTInfo[0].FadeFunc=0;AdxTInfo[0].Volume=NAN;stop(0);check(GetAdxStatus(0)==0&&isnan(AdxTInfo[0].Volume),"immediate stop modified unused volume");AdxTInfo[0].Volume=0;
 /* Lifecycle holds a queued fade; a new original start replaces the stop fade. */
 start();stop(70);ADXT_INFO held=AdxTInfo[0];rx_event_audio_pause(1);check(!rx_event_bgm_tick()&&!memcmp(&held,&AdxTInfo[0],sizeof(held)),"suspended stop fade advanced");rx_event_audio_pause(0);start();check(AdxTInfo[0].FadeFunc==0&&GetAdxStatus(0)==3,"original restart failed to replace stop fade");
 rx_event_audio_end();check(!rx_event_audio_init(1)&&!rx_event_bgm_mount(),"AAudio stop test initialization");start();usleep(80000);stop(0);int samples;ADXT_GetTime(AdxTInfo[0].Handle,&samples,&rate);check(GetAdxStatus(0)==0&&samples==0,"device immediate stop did not reset stream");usleep(80000);ADXT_GetTime(AdxTInfo[0].Handle,&samples,&rate);check(samples==0&&rx_event_audio_frames()>1000,"AAudio advanced stopped source");
 /* Reject before executing an earlier instruction; stop needs no mounted bank. */
 unsigned char prefixed[20]={8,0,0,0,18,0,0,0,5,1,30,0,0,0,0x16,1,0,0,0,0};RxRoomScripts bad;
 check(!rx_room_scripts_open(prefixed,sizeof(prefixed),&bad),"atomic stop fixture");rom->evtp=&bad;unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;
 owned=AdxTInfo[0].Handle;AdxTInfo[0].Handle=(ADXT)(uintptr_t)1;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"invalid stop executed a preceding flag command");AdxTInfo[0].Handle=owned;sys->ev_flg[0]=flags;
 rx_event_audio_end();check(!rx_event_audio_init(0),"bank-free stop initialization");stop(0);stop(1);check(!rx_event_bgm_tick(),"idle fade without mounted archive");rx_event_audio_end();reject(0);
 puts("PASS actual stop bytes, foreign/wrong-slot/sleep/volume/truncated guards, lifecycle hold, restart and live AAudio stop");
 rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
