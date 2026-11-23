#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_audio.h"
#include "adx_native.h"
#include "adxwrap.h"
#include "sdfunc.h"
#include "event.h"
#include "main.h"
#include "room_scripts.h"
#include "audio/audio.h"
#include "ps2/cdvd_image.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
extern ADXT_INFO AdxTInfo[4];extern unsigned AdxPlayFlag[2];
extern int MaxAdxStreamCnt,AdxStreamSleepFlag;extern short AdxVolTbl[128];
extern BH_SCEWORK bhEtask[16],*bhCetask;extern unsigned char *bhScePtr;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL voice: %s\n",why);exit(1);}}
static unsigned char code[64];static RxRoomScripts script;
static const char *run(const unsigned char *command,unsigned bytes){memset(code,0,sizeof(code));code[0]=8;code[4]=8+bytes+6;memcpy(code+8,command,bytes);unsigned char marker[]={5,1,30,0,0,0};memcpy(code+8+bytes,marker,6);check(!rx_room_scripts_open(code,bytes+16,&script),"voice fixture");rom->evtp=&script;sys->ev_flg[0]&=~2u;return rx_events_start_room();}
static void reject(const unsigned char *command,unsigned bytes){unsigned char data[40]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};data[4]=14+bytes;memcpy(data+14,command,bytes);RxRoomScripts bad;check(!rx_room_scripts_open(data,bytes+16,&bad),"invalid voice fixture");rom->evtp=&bad;ADXT_INFO before[4];memcpy(before,AdxTInfo,sizeof(before));unsigned play[2];memcpy(play,AdxPlayFlag,sizeof(play));unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0]&&!memcmp(before,AdxTInfo,sizeof(before))&&!memcmp(play,AdxPlayFlag,sizeof(play)),"invalid voice executed prefix or changed stream state");sys->ev_flg[0]=flags;}
static void render(int16_t *pcm,unsigned frames){memset(pcm,0,frames*4);rx_event_audio_render(pcm,frames);}
static void skip(unsigned frames){static int16_t pcm[9600];while(frames){unsigned n=frames>4800?4800:frames;render(pcm,n);frames-=n;}}
static RxAdxHeader header(unsigned member){uint32_t lsn,size;check(adx_afs_locate(1,member,&lsn,&size),"original voice member");unsigned sectors=(size+2047)/2048;unsigned char *data=malloc(sectors*2048);check(data&&cdvd_read_sectors(lsn,sectors,data)==(int)sectors,"original voice bytes");RxAdxHeader h;const char *error=rx_adx_header(data,size,&h);check(!error,error);free(data);return h;}
static void start(const unsigned char *command){const char *error=run(command,8);check(!error,error);check(sys->ev_flg[0]&2,"VoiceOn length/continuation");}
int main(int argc,char **argv){check(argc==2||argc==3,"supply OBB and optional PCM output directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"complete opening stays gated at next original opcode");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 unsigned char actual[2][8];const unsigned offsets[]={3112,3134};for(unsigned i=0;i<2;++i){check(original->bytes>offsets[i]+8&&original->data[offsets[i]]==0x19,"original VoiceOn bytes");memcpy(actual[i],original->data+offsets[i],8);check(actual[i][1]==1&&actual[i][2]==16+i&&actual[i][3]==0&&actual[i][4]==1,"original dialogue operands");}
 rx_event_audio_end();check(!rx_event_audio_init(0)&&!rx_event_voice_mount()&&!rx_event_bgm_mount(),"headless native voice setup");ADXT_INFO clean=AdxTInfo[1];
 const unsigned levels[]={0,1,45,127};unsigned cases=0;
 for(unsigned source=0;source<4;++source)for(unsigned state=0;state<2;++state)for(unsigned level=0;level<4;++level)for(unsigned fade=0;fade<256;++fade){
  AdxTInfo[1]=clean;AdxPlayFlag[1]=0;StopAdx(1);unsigned char command[8];memcpy(command,actual[0],8);command[1]=source==0?0:source==3?2:1;command[6]=source==3?4:source==2?1:0;command[5]=fade;command[7]=fade;
  if(state)start(actual[0]);SetVolumeAdx(1,-(int)levels[level]);ADXT_INFO expected=AdxTInfo[1],music=AdxTInfo[0];expected.Flag=1;expected.Pan=0;
  if(fade){int timer=fade*10,count=((timer/100)*30+(timer%100)*6/10)/2;if(!count)count=1;expected.FadeFunc=1;expected.FadeCntMax=count;expected.VolLast=expected.LimitMaxVol;expected.VolSpeed=(expected.Volume-expected.VolLast)/count;expected.VolSave=expected.Volume;}else expected.Volume=0;
  start(command);check(!memcmp(&expected,AdxTInfo+1,sizeof(expected))&&!memcmp(&music,AdxTInfo,sizeof(music))&&AdxPlayFlag[1]==1&&GetAdxStatus(1)==3&&bhEtask[0].mode2==1,"original voice request/task/source state");++cases;
 }
 printf("PASS %u original VoiceOn requests: player/two actors/object, active/idle, four levels, all fade bytes and padding; complete stream/task state and music isolation\n",cases);
 /* All task ownership markers must follow the original stop-before-replace branch. */
 AdxTInfo[1]=clean;check(!rx_event_voice_check(17,1),"prepare replacement dialogue");for(unsigned mode2=0;mode2<256;++mode2){start(actual[0]);skip(16);BH_SCEWORK expected;memset(&expected,0xa5,sizeof(expected));expected.mode2=mode2;bhEtask[0]=expected;bhCetask=bhEtask;expected.mode2=1;bhScePtr=actual[1];check(bhVoiceOn()==1&&bhScePtr==actual[1]+8&&!memcmp(&expected,bhEtask,sizeof(expected)),"original voice mode2 handoff");int samples,rate;ADXT_GetTime(AdxTInfo[1].Handle,&samples,&rate);check(samples==0&&GetAdxStatus(1)==3,"voice replacement did not restart source");}
 puts("PASS all 256 original task ownership markers, stop-before-replace and exact eight-byte consumption");
 static int16_t pcm[96000],reference[96000],music[3200],voice[3200],mixed[3200];
 for(unsigned track=0;track<2;++track){AdxTInfo[1]=clean;start(actual[track]);render(pcm,48000);uint64_t energy=0;for(unsigned i=0;i<96000;++i)energy+=abs(pcm[i]);check(energy>100000,"original dialogue PCM absent");int count,rate;ADXT_GetTime(AdxTInfo[1].Handle,&count,&rate);check(count==48000&&rate==48000,"original dialogue sample clock");if(argc==3){char path[512];snprintf(path,sizeof(path),"%s/voice-%u.pcm",argv[2],16+track);FILE *out=fopen(path,"wb");check(out&&fwrite(pcm,1,sizeof(pcm),out)==sizeof(pcm),"voice reference PCM output");fclose(out);}printf("PASS actual opening dialogue %u: 96000 stereo PCM values, energy %llu, 48000 Hz source clock\n",16+track,(unsigned long long)energy);}
 /* The monitor clears the original play flag only after completed/idle status. */
 AdxTInfo[1]=clean;start(actual[0]);RxAdxHeader h=header(16);check(h.channels==1&&!h.loop,"original mono one-shot voice");skip(h.total+10);check(GetAdxStatus(1)==5&&AdxPlayFlag[1]==1,"native completion changed original monitor flag early");check(!rx_event_voice_monitor()&&GetAdxStatus(1)==0&&!AdxPlayFlag[1]&&!AdxTInfo[1].Flag,"original stream completion monitor");
 /* Complete stop-fade timelines retain the original marker until the monitor. */
 unsigned phases=0;for(unsigned level=0;level<4;++level)for(unsigned fade=0;fade<256;++fade){AdxTInfo[1]=clean;start(actual[0]);SetVolumeAdx(1,-(int)levels[level]);ADXT_INFO expected=AdxTInfo[1],bgm=AdxTInfo[0];if(fade){int t=fade*10,n=((t/100)*30+(t%100)*6/10)/2;if(!n)n=1;expected.FadeFunc=2;expected.FadeCntMax=n;expected.VolLast=-127;expected.VolSave=expected.Volume;expected.VolSpeed=(expected.Volume+127)/n;}else expected.Flag=0;
  check(!run((unsigned char[]){0x1a,fade},2)&&(sys->ev_flg[0]&2)&&!memcmp(&expected,AdxTInfo+1,sizeof(expected))&&!memcmp(&bgm,AdxTInfo,sizeof(bgm)),"original VoiceOff state/length/isolation");check(AdxPlayFlag[1]==!!fade,"VoiceOff original play marker");
  while(expected.FadeFunc){for(unsigned tick=0;tick<2&&expected.FadeFunc;++tick){expected.Volume-=expected.VolSpeed;if(--expected.FadeCntMax<0){expected.Volume=expected.VolLast;expected.FadeFunc=0;expected.Flag=0;}++phases;}check(!rx_event_bgm_tick()&&!memcmp(&expected,AdxTInfo+1,sizeof(expected)),"original complete voice stop fade");}
  check(GetAdxStatus(1)==0,"voice stop did not stop source");check(!rx_event_voice_monitor()&&!AdxPlayFlag[1],"monitor did not retire stopped voice");render(pcm,800);for(unsigned i=0;i<1600;++i)check(!pcm[i],"stopped voice not silent");
 }
 printf("PASS all 256 VoiceOff fade bytes at four levels, %u original fade phases, PCM silence and source completion monitor\n",phases);
 /* Exact shared mixer summation and independent music/voice source clocks. */
 AdxTInfo[1]=clean;check(!run((unsigned char[]){0xa6,45,0,45},4)&&!rx_event_bgm_tick(),"music fixture");render(music,1600);StopAdx(0);start(actual[0]);render(voice,1600);check(!run((unsigned char[]){0xa6,45,0,45},4)&&!rx_event_bgm_tick(),"music restart");start(actual[0]);render(mixed,800);for(unsigned i=0;i<1600;++i){int sum=(int)music[i]+voice[i];if(sum>32767)sum=32767;else if(sum< -32768)sum=-32768;check(mixed[i]==sum,"voice/music mix differs");}check(!run((unsigned char[]){0x1a,0},2),"voice stop over music");render(mixed,800);check(!memcmp(mixed,music+1600,3200),"stopping voice interrupted music");StopAdx(0);
 AdxTInfo[1]=clean;start(actual[0]);render(reference,1600);start(actual[0]);render(pcm,800);ADXT_INFO held=AdxTInfo[1];int samples,rate;ADXT_GetTime(held.Handle,&samples,&rate);rx_event_audio_pause(1);render(pcm,800);check(!rx_event_bgm_tick()&&!rx_event_voice_monitor()&&!memcmp(&held,AdxTInfo+1,sizeof(held)),"paused voice fade/monitor advanced");for(unsigned i=0;i<1600;++i)check(!pcm[i],"paused voice not silent");int after;ADXT_GetTime(held.Handle,&after,&rate);check(samples==after,"paused source clock advanced");rx_event_audio_pause(0);render(pcm,800);check(!memcmp(pcm,reference+1600,3200),"voice resume lost exact PCM position");
 puts("PASS exact simultaneous music/voice mix, independent stop, paused clocks and exact PCM resume");
 check(rx_adx_prepare_channels(0,45,1)!=NULL,"stereo member accepted for original mono voice stream");
 /* Whole-script preflight must reject before a preceding flag executes. */
 for(unsigned mode=0;mode<256;++mode)if(mode!=1){unsigned char command[8];memcpy(command,actual[0],8);command[4]=mode;reject(command,8);}for(unsigned bytes=1;bytes<8;++bytes)reject(actual[0],bytes);reject((unsigned char[]){0x1a},1);
 unsigned char invalid[8];memcpy(invalid,actual[0],8);invalid[1]=3;reject(invalid,8);memcpy(invalid,actual[0],8);invalid[6]=255;reject(invalid,8);memcpy(invalid,actual[0],8);invalid[2]=invalid[3]=255;reject(invalid,8);
 ADXT handle=AdxTInfo[1].Handle;AdxTInfo[1].Handle=(void *)(uintptr_t)1;reject(actual[0],8);reject((unsigned char[]){0x1a,0},2);AdxTInfo[1].Handle=AdxTInfo[0].Handle;reject(actual[0],8);AdxTInfo[1].Handle=handle;
 ETTY_WORK *mapping=rom->enep;rom->enep=(void *)(uintptr_t)1;reject(actual[0],8);rom->enep=mapping;O_WRK *objects=sys->obwp;sys->obwp=(void *)(uintptr_t)1;memcpy(invalid,actual[0],8);invalid[1]=2;invalid[6]=4;reject(invalid,8);sys->obwp=objects;
 int partid=PatId[1];PatId[1]=0;reject(actual[0],8);PatId[1]=partid;float volume=AdxTInfo[1].Volume;AdxTInfo[1].Volume=NAN;reject(actual[0],8);reject((unsigned char[]){0x1a,1},2);AdxTInfo[1].Volume=volume;
 unsigned ss=sys->ss_flg;sys->ss_flg^=1;reject(actual[0],8);sys->ss_flg=ss;
 puts("PASS atomic unsupported-spatial/source/member/truncated/foreign-handle/pool/archive/nonfinite/disc preflight rejection");
 rx_event_audio_end();check(!rx_event_audio_init(1)&&!rx_event_voice_mount(),"AAudio voice initialization");uint64_t callbacks=rx_event_audio_frames();start(actual[0]);for(unsigned wait=0;wait<200;++wait){usleep(10000);ADXT_GetTime(AdxTInfo[1].Handle,&samples,&rate);if(rx_event_audio_frames()>callbacks+1000&&samples>1000&&GetAdxStatus(1)==3)break;}ADXT_GetTime(AdxTInfo[1].Handle,&samples,&rate);check(samples>1000&&GetAdxStatus(1)==3,"AAudio did not consume original dialogue");check(!run((unsigned char[]){0x1a,0},2)&&GetAdxStatus(1)==0,"AAudio original voice stop");puts("PASS original dialogue through Quest AAudio callback and original immediate stop");
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);check(rx_event_voice_check(16,1)!=NULL,"retired voice context accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
