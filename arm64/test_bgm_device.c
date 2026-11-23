#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_audio.h"
#include "adx_native.h"
#include "adxwrap.h"
#include "sdfunc.h"
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
extern ADXT_INFO AdxTInfo[4];
extern unsigned AdxPlayFlag[2];
extern int CurrentBgmNo,CurrentBgmVolume;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL BGM: %s\n",why);exit(1);}}
static unsigned char code[16]={8,0,0,0,14,0,0,0,0xa6,45,0,45,0,0,0,0};
static RxRoomScripts script;
static const char *run(unsigned track,unsigned fade,unsigned volume){
 code[9]=track;code[10]=fade;code[11]=volume;check(!rx_room_scripts_open(code,sizeof(code),&script),"script fixture");rom->evtp=&script;return rx_events_start_room();
}
static RxAdxHeader header(unsigned track){
 uint32_t lsn,size;check(adx_afs_locate(0,track,&lsn,&size),"actual BGM member");
 size_t sectors=((size_t)size+2047)/2048;unsigned char *data=malloc(sectors*2048);check(data&&cdvd_read_sectors(lsn,sectors,data)==(int)sectors,"BGM source bytes");
 RxAdxHeader h;check(!rx_adx_header(data,size,&h),"bounded actual ADX header");
 RxAdxHeader canary,before;memset(&canary,0xa5,sizeof(canary));before=canary;
 for(unsigned bytes=0;bytes<36;++bytes)check(rx_adx_header(data,bytes,&canary)&&!memcmp(&canary,&before,sizeof(canary)),"truncated ADX wrote output");
 const unsigned offsets[]={0,4,5,6,7,18,19};
 for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i){unsigned offset=offsets[i];unsigned char old=data[offset];data[offset]=255;check(rx_adx_header(data,size,&canary)&&!memcmp(&canary,&before,sizeof(canary)),"invalid ADX format wrote output");data[offset]=old;}
 unsigned char old=data[h.data_offset-1];data[h.data_offset-1]^=1;check(rx_adx_header(data,size,&canary)!=NULL,"invalid ADX signature accepted");data[h.data_offset-1]=old;
 free(data);return h;
}
static void skip(unsigned frames){static int16_t discard[4800*2];while(frames){unsigned n=frames>4800?4800:frames;memset(discard,0,n*4);rx_adx_mix(discard,n);frames-=n;}}
static void reject(unsigned track,unsigned fade,unsigned volume){
 ADXT_INFO info=AdxTInfo[0];int number=CurrentBgmNo,level=CurrentBgmVolume;unsigned flag=AdxPlayFlag[0];
 check(run(track,fade,volume)!=NULL,"invalid BGM reached original handler");
 check(!memcmp(&info,&AdxTInfo[0],sizeof(info))&&CurrentBgmNo==number&&CurrentBgmVolume==level&&AdxPlayFlag[0]==flag,"invalid BGM changed playback/fade state");
}
int main(int argc,char **argv){
 check(argc==2||argc==3,"supply OBB and optional output directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
 const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);
 error=rx_opening_start(&opening,0);check(!error,error);
 RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"full opening must remain gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 const RxRoomScripts *original=rom->evtp;
 const unsigned offsets[]={1794,4696,6630};unsigned char requests[3][4];
 for(unsigned i=0;i<3;++i){check(offsets[i]+4<=original->bytes&&original->data[offsets[i]]==0xa6,"actual opening BgmOnEx bytes");memcpy(requests[i],original->data+offsets[i],4);}
 rx_event_audio_end();check(!rx_event_audio_init(0)&&!rx_event_bgm_mount(),"headless original ADX setup");
 for(unsigned volume=0;volume<128;++volume)for(unsigned fade=0;fade<256;++fade){
  check(!run(45,fade,volume),"original BgmOnEx dispatch");
  ADXT_INFO *info=&AdxTInfo[0];int timer=fade*10+1,count=((timer/100)*30+((timer%100)*6)/10)/2;if(!count)count=1;
  float last=volume>20?-(float)volume+20:0;
  check(CurrentBgmNo==45&&CurrentBgmVolume==-(int)volume&&AdxPlayFlag[0]==1&&GetAdxStatus(0)==3,"original BGM metadata/start differs");
  check(info->Volume==-127&&info->LimitMaxVol==last&&info->VolLast==last&&info->VolSave==-127&&info->FadeCntMax==count&&info->FadeFunc==1&&info->VolSpeed==(-127-last)/count,"original BGM fade request differs");
 }
 printf("PASS 32768 original BgmOnEx number/volume/fade requests\n");
 static int16_t pcm[4800*2];
 for(unsigned track_index=0;track_index<3;++track_index){unsigned char *request=requests[track_index];unsigned track=request[1];
  check(!run(track,request[2],request[3]),"real original opening music request");
  check(!rx_event_bgm_tick()&&AdxTInfo[0].FadeFunc==0&&AdxTInfo[0].Volume==-(float)request[3]+20,"original immediate fade progression");
  ADXT_SetOutVol(AdxTInfo[0].Handle,0);FILE *out=NULL;if(argc==3){char path[512];snprintf(path,sizeof(path),"%s/bgm-%u.pcm",argv[2],track);out=fopen(path,"wb");check(out!=NULL,"PCM output");}
  uint64_t energy=0;for(unsigned block=0;block<20;++block){memset(pcm,0,sizeof(pcm));rx_adx_mix(pcm,4800);for(unsigned i=0;i<9600;++i)energy+=abs(pcm[i]);if(out)check(fwrite(pcm,1,sizeof(pcm),out)==sizeof(pcm),"PCM write");}if(out)fclose(out);
  check(energy>100000&&GetAdxStatus(0)==3,"original ADX PCM absent");int samples,rate;ADXT_GetTime(AdxTInfo[0].Handle,&samples,&rate);check(samples==96000&&rate==48000,"original ADX sample clock");
  printf("PASS original BGM %u two-second PCM, energy %llu, sample-clock %d/%d\n",track,(unsigned long long)energy,samples,rate);
 }
 for(unsigned volume=128;volume<256;++volume)reject(45,0,volume);
 for(unsigned track=121;track<256;++track)reject(track,0,45);
 ADXT owned=AdxTInfo[0].Handle;AdxTInfo[0].Handle=(ADXT)(uintptr_t)1;reject(45,0,45);AdxTInfo[0].Handle=AdxTInfo[1].Handle;reject(45,0,45);AdxTInfo[0].Handle=owned;
 for(unsigned length=1;length<4;++length){unsigned char truncated[14]={8,0,0,0};truncated[4]=8+length;truncated[8]=0xa6;
  RxRoomScripts short_script;check(!rx_room_scripts_open(truncated,10+length,&short_script),"truncated BGM fixture");rom->evtp=&short_script;ADXT_INFO before=AdxTInfo[0];check(rx_events_start_room()&&!memcmp(&before,&AdxTInfo[0],sizeof(before)),"truncated BGM changed stream");}
 unsigned char prefix[22]={8,0,0,0,20,0,0,0,5,1,0,0,0,0,0xa6,255,0,45,0,0,0,0};RxRoomScripts bad;
 check(!rx_room_scripts_open(prefix,sizeof(prefix),&bad),"BGM prefix fixture");rom->evtp=&bad;unsigned before_flag=sys->ev_flg[0];sys->ev_flg[0]=0;
 check(rx_events_start_room()&&sys->ev_flg[0]==0,"BGM preflight executed preceding flag command");sys->ev_flg[0]=before_flag;
 /* Muted original command must be a complete no-op, even for invalid operands. */
 ADXT_INFO state=AdxTInfo[0];int old_number=CurrentBgmNo,old_volume=CurrentBgmVolume;unsigned old_flag=AdxPlayFlag[0],old_ss=sys->ss_flg;sys->ss_flg|=0x4000000;
 for(unsigned i=0;i<256;++i)check(!run(i,i,255-i),"original muted BGM request");
 check(!memcmp(&state,&AdxTInfo[0],sizeof(state))&&old_number==CurrentBgmNo&&old_volume==CurrentBgmVolume&&old_flag==AdxPlayFlag[0],"muted BGM changed state");sys->ss_flg=old_ss;
 const unsigned fade_cases[]={0,1,10,30,255},volume_cases[]={0,20,21,45,127};
 for(unsigned f=0;f<5;++f)for(unsigned v=0;v<5;++v){
  check(!run(45,fade_cases[f],volume_cases[v]),"fade fixture");ADXT_INFO wanted=AdxTInfo[0];
  while(wanted.FadeFunc){for(unsigned tick=0;tick<2&&wanted.FadeFunc;++tick){wanted.Volume-=wanted.VolSpeed;if(--wanted.FadeCntMax<0){wanted.Volume=wanted.VolLast;wanted.FadeFunc=0;}}
   check(!rx_event_bgm_tick(),"original fade tick");check(AdxTInfo[0].Volume==wanted.Volume&&AdxTInfo[0].FadeCntMax==wanted.FadeCntMax&&AdxTInfo[0].FadeFunc==wanted.FadeFunc,"original complete fade timing differs");
  }
 }
 RxAdxHeader loop=header(45);check(loop.loop&&loop.end_sample>loop.start_sample+256,"actual loop fixture");check(!run(45,0,0),"loop request");ADXT_SetOutVol(AdxTInfo[0].Handle,0);
 skip(loop.start_sample);int16_t first[512]={0},again[512]={0};rx_adx_mix(first,256);skip(loop.end_sample-loop.start_sample-256);rx_adx_mix(again,256);
 check(!memcmp(first,again,sizeof(first))&&GetAdxStatus(0)==3,"authored ADX loop PCM differs across restart");
 RxAdxHeader one=header(83);check(!one.loop&&!run(83,0,0),"one-shot request");ADXT_SetOutVol(AdxTInfo[0].Handle,0);skip(one.total);
 check(GetAdxStatus(0)==5,"original one-shot completion status");int count,rate;ADXT_GetTime(AdxTInfo[0].Handle,&count,&rate);check(count==(int)one.total,"one-shot sample clock");memset(pcm,0,sizeof(pcm));rx_adx_mix(pcm,4800);for(unsigned i=0;i<9600;++i)check(!pcm[i],"finished stream emitted audio");
 StopAdx(0);check(GetAdxStatus(0)==0&&!AdxTInfo[0].Flag,"original stop");
 AFS_PATINFO mono_archive[]={{"BGM2.AFS",9,128,NULL},{NULL,0,0,NULL}};check(!CreatePartitionEx(mono_archive)&&!rx_adx_prepare(9,45),"original mono low-rate sentinel");
 ADXT_StartAfs(AdxTInfo[1].Handle,9,45);ADXT_SetOutVol(AdxTInfo[1].Handle,0);memset(pcm,0,sizeof(pcm));rx_adx_mix(pcm,4800);ADXT_GetTime(AdxTInfo[1].Handle,&count,&rate);check(count==1&&rate==11025&&GetAdxStatus(1)==5,"mono low-rate resampling/completion");
 /* Lifecycle suspension preserves native sample and original fade clocks. */
 check(!run(45,10,45),"pause request");skip(100);ADXT_GetTime(AdxTInfo[0].Handle,&count,&rate);state=AdxTInfo[0];rx_event_audio_pause(1);memset(pcm,1,sizeof(pcm));rx_event_audio_render(pcm,4800);check(!rx_event_bgm_tick(),"suspended fade");int paused_count;ADXT_GetTime(AdxTInfo[0].Handle,&paused_count,&rate);check(paused_count==count&&!memcmp(&state,&AdxTInfo[0],sizeof(state)),"suspension advanced clocks");for(unsigned i=0;i<9600;++i)check(!pcm[i],"suspended audio not silent");rx_event_audio_pause(0);rx_event_audio_render(pcm,4800);ADXT_GetTime(AdxTInfo[0].Handle,&paused_count,&rate);check(paused_count==count+4800,"resume did not continue source clock");
 /* Reinitialize the real AAudio callback and observe actual source consumption. */
 rx_event_audio_end();check(!rx_event_audio_init(1)&&!rx_event_bgm_mount()&&!run(45,0,45)&&!rx_event_bgm_tick(),"device BGM start");/* AAudio startup is asynchronous, especially after GPU/equipment stress.
    Keep the source-consumption assertion, but wait for its condition with a
    bounded deadline rather than assuming a fixed 160 ms startup latency. */
 for(unsigned wait=0;wait<200;++wait){usleep(10000);ADXT_GetTime(AdxTInfo[0].Handle,&count,&rate);if(rx_event_audio_frames()>1000&&count>1000&&GetAdxStatus(0)==3)break;}
 check(rx_event_audio_frames()>1000&&count>1000&&GetAdxStatus(0)==3,"AAudio did not consume original ADX samples within two seconds");
 printf("PASS original loops, one-shot end/stop, mono/resampling, 25 complete fades, mute/bounds/foreign input, lifecycle pause/resume and AAudio (%d source samples)\n",count);
 rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);puts("PASS BGM native teardown");return 0;
}

