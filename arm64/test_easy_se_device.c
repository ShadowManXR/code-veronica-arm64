#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_audio.h"
#include "room_scripts.h"
#include "main.h"
#include "sdc.h"
#include "sdfunc.h"
#include "ps2_sg_sd.h"
#include "ps2_snddrv.h"
#include "audio/snd.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
extern NO_NAME_31 GsSlotInfoMi[8];extern SND_REQ RequestInfo;extern SND_WORK __snd_work__[40];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL EasySESet: %s\n",why);exit(1);}}
static SDS_INFO initial_info[8];static NO_NAME_31 initial_flags[8];static SND_WORK initial_work[40];static SND_REQ initial_request;
static void reset(void){snd_synth_lock();snd_synth_reset_native();snd_synth_unlock();memcpy(MidiInfo,initial_info,sizeof(initial_info));memcpy(GsSlotInfoMi,initial_flags,sizeof(initial_flags));memcpy(__snd_work__,initial_work,sizeof(initial_work));RequestInfo=initial_request;memset(&get_iop_snddata,0,sizeof(get_iop_snddata));rx_sound_queue_reset();}
static uint64_t hash(const SndBlob *blob){uint64_t value=UINT64_C(14695981039346656037);for(unsigned i=0;i<blob->size;++i)value=(value^blob->data[i])*UINT64_C(1099511628211);return value;}
static unsigned char data[64];static RxRoomScripts script;
static const char *run(const unsigned char command[16]){memset(data,0,sizeof(data));data[0]=8;data[4]=30;memcpy(data+8,command,16);unsigned char marker[]={5,1,30,0,0,0};memcpy(data+24,marker,6);check(!rx_room_scripts_open(data,32,&script),"EasySESet fixture");rom->evtp=&script;sys->ev_flg[0]&=~2u;return rx_events_start_room();}
static void reject(const unsigned char *command,unsigned bytes){unsigned char data[40]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};data[4]=14+bytes;memcpy(data+14,command,bytes);RxRoomScripts bad;check(!rx_room_scripts_open(data,16+bytes,&bad),"invalid sound fixture");rom->evtp=&bad;SDS_INFO before[8];memcpy(before,MidiInfo,sizeof(before));SND_REQ request=RequestInfo;unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0]&&!memcmp(before,MidiInfo,sizeof(before))&&!memcmp(&request,&RequestInfo,sizeof(request)),"bad sound executed prefix or changed request metadata");sys->ev_flg[0]=flags;}
int main(int argc,char **argv){check(argc==2,"supply OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full opening must stay gated at next original opcode");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 rx_event_audio_end();check(!rx_event_audio_init(0)&&!rx_event_se_mount(0)&&!rx_event_bgm_mount(),"headless native sound initialization");
 check(g_snd_banks.sq[2].size==4736&&hash(&g_snd_banks.sq[2])==UINT64_C(0xcc1b590a942fd2b0)&&g_snd_banks.midi_hd[2].size==5248&&hash(&g_snd_banks.midi_hd[2])==UINT64_C(0x13cfc1289e71e70a)&&g_snd_banks.bd[2].size==576896&&hash(&g_snd_banks.bd[2])==UINT64_C(0xa9a53b9d7481eecf),"SQ/HD/BD differ from independent ISO references");
 puts("PASS owned room SQ/HD/BD against independent original ISO hashes");
 memcpy(initial_info,MidiInfo,sizeof(initial_info));memcpy(initial_flags,GsSlotInfoMi,sizeof(initial_flags));memcpy(initial_work,__snd_work__,sizeof(initial_work));memset(&initial_request,0x5a,sizeof(initial_request));
 unsigned char command[16];memcpy(command,original->data+2578,16);check(command[0]==0x86&&command[1]==7&&command[2]==1&&command[14]==1&&command[15]==2,"actual opening event-sound bytes");
 reset();error=run(command);check(!error,error);check(sys->ev_flg[0]&2,"original sound command did not consume 16 bytes");
 unsigned port=6;SDS_INFO expected=initial_info[port];expected.Volume=expected.VolLast=-10;expected.FadeCntMax=-1;expected.Pan=expected.PanLast=0;expected.PanCntMax=-1;
 check(!memcmp(&expected,MidiInfo+port,sizeof(expected))&&GsSlotInfoMi[port].Flag==6,"original event volume/pan state differs");
 SND_WORK driver=initial_work[port];driver.vol=118;driver.bank_num=2;driver.channel_num=1;check(!memcmp(&driver,__snd_work__+port,sizeof(driver)),"original SDK MIDI request differs");
 SND_REQ request=initial_request;request.SlotNo=6;request.BankNo=2;request.ListNo=1;request.Priority=0;request.PitchDelayTime=-2;request.SpeedDelayTime=-1;request.FxInput=-1;request.PanDelayTime=-2;request.VolumeDelayTime=-1;check(!memcmp(&request,&RequestInfo,sizeof(request)),"original event request mapping differs");
 static int16_t actual[48000*2],reference[48000*2];rx_event_audio_render(actual,800);check(!rx_event_se_tick(),"second original sound-server phase");rx_event_audio_render(actual+1600,47200);uint64_t energy=0;for(unsigned i=0;i<96000;++i)energy+=abs(actual[i]);check(energy>10000,"original event produced no audible PCM");
 reset();snd_synth_lock();unsigned char start[]={0x20,6,2,127,1},parameters[]={0x27,6,0,0,127,64,0,64};snd_synth_command(start,5);snd_synth_command(parameters,8);snd_synth_unlock();rx_event_audio_render(reference,800);parameters[4]=118;snd_synth_lock();snd_synth_command(parameters,8);snd_synth_unlock();rx_event_audio_render(reference+1600,47200);check(!memcmp(actual,reference,sizeof(actual)),"original request PCM differs from independent native driver commands");
 printf("PASS actual EasySESet -> original request/fades/SDK MIDI server -> start/parameter transport -> SQ/SPU2 PCM; 96000 exact samples, energy %llu\n",(unsigned long long)energy);

 unsigned parameter_cases=0,pan_rejects=0;
 /* Complete start/target volume grid, with a nonzero duration so start values
    and the -1 sentinel cannot be hidden by immediate completion. */
 for(unsigned start=0;start<128;++start)for(unsigned last=0;last<128;++last){reset();memcpy(command,original->data+2578,16);command[2]=(start+last)%5;command[3]=start;command[4]=last;command[7]=17;error=run(command);check(!error,error);unsigned port=7-command[2];SDS_INFO expected=initial_info[port];expected.PanFunc=1;expected.PanCntMax=16;expected.PanSpeed=-0.0f;
  if(last!=1){float first=start==1?0:(float)-(int)start;expected.VolLast=(float)-(int)last;expected.VolSpeed=-(expected.VolLast-first)/17.0f;expected.Volume=first-expected.VolSpeed;expected.FadeFunc=expected.VolSpeed<0?1:2;expected.FadeCntMax=16;}
  check(!memcmp(&expected,MidiInfo+port,sizeof(expected)),"volume grid/sentinel differs from independent original manager state");int volume=128+(int)expected.Volume;if(volume>127)volume=127;check(__snd_work__[port].vol==volume&&__snd_work__[port].pan==64&&GsSlotInfoMi[port].Flag==((last!=1?2:0)|4),"volume grid SDK/flag mapping");++parameter_cases;
 }
 /* The original native-event call resets current pan to zero after computing
    pan speed. Overshooting signed-byte trajectories remain gated. */
 for(unsigned start=0;start<256;++start)for(unsigned last=0;last<256;++last){reset();memcpy(command,original->data+2578,16);command[2]=(start+last)%5;command[5]=start;command[6]=last;command[7]=17;float delta=((int)last-128)-(start==127?0:(int)start-128);
  if(last!=127&&(delta< -128||delta>127)){reject(command,16);++pan_rejects;continue;}
  error=run(command);check(!error,error);unsigned port=7-command[2];SDS_INFO expected=initial_info[port];expected.Volume=expected.VolLast=-10;expected.VolSpeed=-0.0f;expected.FadeFunc=2;expected.FadeCntMax=16;
  if(last!=127){expected.PanLast=(int)last-128;expected.PanSpeed=-delta/17.0f;expected.Pan=-expected.PanSpeed;expected.PanCntMax=16;expected.PanFunc=1;}
  check(!memcmp(&expected,MidiInfo+port,sizeof(expected)),"pan grid/sentinel/reset differs from independent original state");int pan=((int)expected.Pan)/2+64;check(__snd_work__[port].pan==pan&&GsSlotInfoMi[port].Flag==(2|(last!=127?4:0)),"pan grid SDK/flags");++parameter_cases;
 }
 printf("PASS %u volume/pan parameter states across five slots, full volume/pan operand grids, sentinels and source pan reset; %u oversized pan trajectories reject atomically\n",parameter_cases,pan_rejects);
 unsigned fade_steps=0;
 for(unsigned frames=0;frames<256;++frames){reset();memcpy(command,original->data+2578,16);command[3]=10;command[4]=40;command[5]=98;command[6]=188;command[7]=frames;error=run(command);check(!error,error);float volume=-10,pan=0,vs=frames?30.0f/frames:0,ps=frames?-90.0f/frames:0;
  for(unsigned step=0;step<frames+2;++step){int count=(int)frames-(int)step-1;if(count<0){count=-1;volume=-40;pan=60;}else{volume-=vs;pan-=ps;}
   check(fabs(MidiInfo[6].Volume-volume)<0.00001&&fabs(MidiInfo[6].Pan-pan)<0.00001&&MidiInfo[6].FadeCntMax==count&&MidiInfo[6].PanCntMax==count&&MidiInfo[6].FadeFunc==(count<0?0:2)&&MidiInfo[6].PanFunc==(count<0?0:1),"full original fade/pan timeline");check(__snd_work__[6].vol==128+(int)volume&&__snd_work__[6].pan==((int)pan)/2+64,"SDK parameter quantization timeline");++fade_steps;if(step+1<frames+2)check(!rx_event_se_tick(),"next original MIDI phase");
  }
 }
 printf("PASS all 256 fade durations, %u full original volume/pan server phases and SDK quantization\n",fade_steps);
 /* Encoded sound bank bits are overwritten by the original event bank 2. */
 for(unsigned bank=0;bank<256;++bank){reset();memcpy(command,original->data+2578,16);command[15]=bank;check(!run(command)&&RequestInfo.BankNo==2&&RequestInfo.ListNo==1,"encoded bank remap");}
 reset();memcpy(command,original->data+2578,16);check(!run(command),"lifecycle event start");rx_event_audio_render(actual,800);SDS_INFO paused=MidiInfo[6];SND_WORK paused_driver=__snd_work__[6];rx_event_audio_pause(1);check(!rx_event_se_tick(),"paused event MIDI tick");memset(actual,1,1600*sizeof(*actual));rx_event_audio_render(actual,800);for(unsigned i=0;i<1600;++i)check(!actual[i],"paused event PCM not silent");check(!memcmp(&paused,MidiInfo+6,sizeof(paused))&&!memcmp(&paused_driver,__snd_work__+6,sizeof(paused_driver)),"pause advanced source state");rx_event_audio_pause(0);check(!rx_event_se_tick(),"resume event MIDI phase");rx_event_audio_render(actual,800);check(!memcmp(actual,reference+1600,1600*sizeof(*actual)),"resume lost original source position");
 check(!StopNativeEventSe(1)&&!rx_event_audio_deliver(),"original event stop transport");snd_synth_lock();unsigned playing=snd_synth_midi_active();snd_synth_unlock();check(!(playing&(1u<<6)),"original event port still playing after stop");
 reset();check(!run(command),"event bank release start");rx_event_audio_render(actual,800);rx_event_se_end();memset(actual,1,1600*sizeof(*actual));rx_event_audio_render(actual,800);for(unsigned i=0;i<1600;++i)check(!actual[i],"retired event bank still rendered cached voices");check(!rx_event_se_mounted()&&!g_snd_banks.sq[2].data&&!g_snd_banks.midi_hd[2].data&&!g_snd_banks.bd[2].data,"owned event bank release");check(!rx_event_se_mount(0),"event bank remount");
 puts("PASS original event stop, paused clocks/exact PCM resume, bank retirement and owned remount");
 reset();memcpy(command,original->data+2578,16);
 reset();for(unsigned bytes=1;bytes<16;++bytes)reject(command,bytes);for(unsigned type=0;type<256;++type)if(type!=7&&type!=1){command[1]=type;reject(command,16);}command[1]=7;command[2]=5;reject(command,16);command[2]=1;command[3]=128;reject(command,16);command[3]=10;
 SndBlob bank=g_snd_banks.sq[2];g_snd_banks.sq[2].data=(void *)(uintptr_t)1;reject(command,16);g_snd_banks.sq[2]=bank;command[11]=255;reject(command,16);command[11]=0;
 puts("PASS unsupported family/slot/volume/truncated/foreign-bank/joint atomic preflight rejection");

 /* Shared AAudio callback, with the real disc event sequence. */
 rx_event_audio_end();check(!rx_event_audio_init(1)&&!rx_event_se_mount(0),"real device event audio start");memcpy(command,original->data+2578,16);uint64_t callbacks=rx_event_audio_frames();check(!run(command),"real device original event request");for(unsigned wait=0;wait<200&&rx_event_audio_frames()<callbacks+1000;++wait)usleep(10000);check(rx_event_audio_frames()>=callbacks+1000,"AAudio did not render event source");puts("PASS original room event request through the Quest AAudio callback");
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);check(!g_snd_banks.sq[2].data&&!g_snd_banks.midi_hd[2].data&&!g_snd_banks.bd[2].data&&!rx_event_se_mounted(),"native event bank teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
