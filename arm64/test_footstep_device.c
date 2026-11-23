#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "event_audio.h"
#include "room_scripts.h"
#include "random_runtime.h"
#include "face_control_runtime.h"
#include "room.h"
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
extern SDS_INFO SeInfo[20];extern NO_NAME_31 GsSlotInfoSe[20],GsSlotInfoMi[8];extern SND_REQ RequestInfo;extern SND_WORK __snd_work__[40];extern unsigned short use_se_info[6],req_se_info[6];extern int PlayerFootStepSwitch[3];extern SDSHOT SeHandle[20];extern NJS_POINT3 CameraPos;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL footstep: %s\n",why);exit(1);}}
static SDS_INFO initial_se[20],initial_midi[8];static NO_NAME_31 initial_sf[20],initial_mf[8];static SND_WORK initial_work[40];static SND_REQ request_seed;
static void reset(unsigned seed){snd_synth_lock();snd_synth_reset_native();snd_synth_unlock();memcpy(SeInfo,initial_se,sizeof(initial_se));memcpy(MidiInfo,initial_midi,sizeof(initial_midi));memcpy(GsSlotInfoSe,initial_sf,sizeof(initial_sf));memcpy(GsSlotInfoMi,initial_mf,sizeof(initial_mf));memcpy(__snd_work__,initial_work,sizeof(initial_work));memset(PlayerFootStepSwitch,0,sizeof(int)*3);memset(use_se_info,0,12);memset(req_se_info,0,12);memset(&get_iop_snddata,0,sizeof(get_iop_snddata));rx_sound_queue_reset();RequestInfo=request_seed;rx_ps2_srand(seed);}
static unsigned char code[40];static RxRoomScripts script;
static const char *run(const unsigned char *command){memset(code,0,sizeof(code));code[0]=8;code[4]=30;memcpy(code+8,command,16);unsigned char marker[]={5,1,30,0,0,0};memcpy(code+24,marker,6);check(!rx_room_scripts_open(code,32,&script),"footstep fixture");rom->evtp=&script;sys->ev_flg[0]&=~2u;return rx_events_start_room();}
static void execute(const unsigned char *command){const char *error=run(command);check(!error,error);check(sys->ev_flg[0]&2,"original footstep instruction length");}
static uint64_t hash(const SndBlob *b){uint64_t h=UINT64_C(14695981039346656037);for(unsigned i=0;i<b->size;++i)h=(h^b->data[i])*UINT64_C(1099511628211);return h;}
static unsigned pitch(unsigned seed,unsigned type){uint32_t r=(seed*UINT32_C(1103515245)+12345)&0x7fffffff;unsigned index=(unsigned)(4.0f*(-(int)r/-2.1474836e9f))&3;return (index==1||index==2?256:0)+(type?512:0);}
static void render(int16_t *pcm,unsigned n){memset(pcm,0,n*4);rx_event_audio_render(pcm,n);}
static SDS_INFO negative_se[20];static SND_WORK negative_work[40];
static void reject(const unsigned char *command,unsigned bytes){unsigned char data[40]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};data[4]=14+bytes;memcpy(data+14,command,bytes);RxRoomScripts bad;check(!rx_room_scripts_open(data,bytes+16,&bad),"bad footstep fixture");rom->evtp=&bad;memcpy(negative_se,SeInfo,sizeof(negative_se));memcpy(negative_work,__snd_work__,sizeof(negative_work));SND_REQ request=RequestInfo;int switches[3];memcpy(switches,PlayerFootStepSwitch,sizeof(switches));unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0]&&!memcmp(negative_se,SeInfo,sizeof(negative_se))&&!memcmp(negative_work,__snd_work__,sizeof(negative_work))&&!memcmp(&request,&RequestInfo,sizeof(request))&&!memcmp(switches,PlayerFootStepSwitch,sizeof(switches)),"bad footstep executed prefix or changed original audio state");sys->ev_flg[0]=flags;}
int main(int argc,char **argv){check(argc==2,"supply OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full opening remains gated at next original work selector");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);printf("Room actor 1 primary source model: %u owned joints\n",ene[rom->enep[1].wrk_no].mlwP->obj_num);unsigned char actual[16];memcpy(actual,original->data+5820,16);check(!memcmp(actual,(unsigned char[]){0x86,1,1,64,64,128,128,0,0,1,1,26,0,0,44,1},16),"actual opening footstep bytes");
 rx_event_audio_end();check(!rx_event_audio_init(0)&&!rx_event_se_mount(0),"headless original shot setup");check(g_snd_banks.se_hd[2].size==2624&&hash(&g_snd_banks.se_hd[2])==UINT64_C(0xa3bf6aae9d309c7e)&&g_snd_banks.bd[5].size==174528&&hash(&g_snd_banks.bd[5])==UINT64_C(0xf81b170780741b8f),"shot assets differ from independent disc hashes");puts("PASS original room shot header/body ownership and independent ISO hashes");

 for(unsigned record=0;record<2;++record){BH_PWORK *actor=&ene[rom->enep[record].wrk_no];const ML_WORK *face=rx_face_control_model(actor);
  check(face&&actor->mlwP==actor->mdl&&actor->mlwP->obj_num==31&&actor->mlwP->objP==face->objP&&actor->mlwP->owP==face->owP,"original expanded facial model not published to actor");
  for(unsigned joint=0;joint<31;++joint){check((actor->mlwP->owP[joint].flg&4)==(joint>=6&&joint<=14?4u:0u),"original nine protected facial nodes");check(!bhCheckNativeEventSound(1,record,joint),"owned expanded source joint rejected");}
  check(bhCheckNativeEventSound(1,record,31)!=NULL,"out of range expanded source joint accepted");
 }
 puts("PASS both original 31-node facial models published to script actors, all owned joints and nine protected nodes");
 memcpy(initial_se,SeInfo,sizeof(initial_se));memcpy(initial_midi,MidiInfo,sizeof(initial_midi));memcpy(initial_sf,GsSlotInfoSe,sizeof(initial_sf));memcpy(initial_mf,GsSlotInfoMi,sizeof(initial_mf));memcpy(initial_work,__snd_work__,sizeof(initial_work));memset(&request_seed,0x5a,sizeof(request_seed));
 unsigned cases=0;for(unsigned id=0;id<3;++id)for(unsigned floor=0;floor<5;++floor)for(unsigned type=0;type<2;++type)for(unsigned seed=0;seed<256;++seed){reset(seed);unsigned char command[16];memcpy(command,actual,16);command[2]=id;command[8]=floor;command[12]=type;unsigned tone=pitch(seed,type);execute(command);unsigned slot=12+id*2;check(PlayerFootStepSwitch[id]==1&&RequestInfo.SlotNo==slot&&RequestInfo.BankNo==2&&RequestInfo.ListNo==floor&&RequestInfo.Pitch==(int)tone&&RequestInfo.PitchDelayTime==0&&RequestInfo.PanDelayTime==-1&&RequestInfo.VolumeDelayTime==-1&&RequestInfo.FxInput==-1,"original footstep request/alternation/RNG differs");
  for(unsigned side=0;side<2;++side){unsigned lane=11+id*2+side;SDS_INFO expected=initial_se[lane];expected.Volume=expected.VolLast=-64;expected.Pan=expected.PanLast=0;expected.FadeCntMax=expected.PanCntMax=-1;check(!memcmp(&expected,SeInfo+lane,sizeof(expected))&&GsSlotInfoSe[lane].Flag==6,"original paired footstep fade/pan state differs");}
  SND_WORK expected=initial_work[8+slot];expected.vol=64;expected.pan=64;expected.pitch=8192+tone*3;expected.port_num=2;expected.bank_num=floor;expected.channel_num=0;expected.priority=0;expected.req=254;check(!memcmp(&expected,__snd_work__+8+slot,sizeof(expected)),"whole original footstep SDK state differs");++cases;
 }
 printf("PASS %u original walk/run requests: three IDs/five floor programs/all 256 RNG seeds; request fields, paired controllers and exact SDK pitch/channel state\n",cases);

 for(unsigned record=0;record<2;++record)for(unsigned joint=0;joint<31;++joint){unsigned char command[16];memcpy(command,actual,16);command[10]=record;command[11]=joint;reset(1);int yaw=cam.ay;const float *position=&ene[rom->enep[record].wrk_no].mlwP->owP[joint].mtx[12];
  check(!rx_event_step_position_check(position),"owned joint sound position must admit every original listening direction");execute(command);cam.ay=yaw;check(RequestInfo.SlotNo==14,"expanded source footstep did not execute");}
 for(unsigned offset=5806;offset<6078;){unsigned char *p=(unsigned char *)original->data+offset;
  if(*p==0x86){reset(1);execute(p);check(RequestInfo.SlotNo==14,"authored alternating footstep did not execute");offset+=16;}
  else if(*p==0x65||*p==0xfc)offset+=4-(2*(*p==0xfc));else if(*p==0x38)offset+=6;else if(*p==0xfe||*p==0xfd)++offset;else if(*p==0xff)break;else check(0,"unexpected authored footstep event instruction");
 }
 puts("PASS all 62 expanded actor/joint sound sources and actual opening alternating-footstep commands");
 unsigned levels=0;for(unsigned start=0;start<128;++start)if(start!=1)for(unsigned last=0;last<128;++last)if(last!=1){reset(1);unsigned char command[16];memcpy(command,actual,16);command[3]=start;command[4]=last;execute(command);check(SeInfo[14].Volume==-(int)last&&__snd_work__[22].vol==128-last,"original immediate shot volume conversion");++levels;}
 for(unsigned pan=0;pan<256;++pan)if(pan!=127){reset(1);unsigned char command[16];memcpy(command,actual,16);command[5]=255-pan;command[6]=pan;if(command[5]==127)command[5]=128;execute(command);check(SeInfo[14].Pan==(int)pan-128&&__snd_work__[22].pan==((int)pan-128)/2+64,"original immediate shot pan conversion");}
 printf("PASS %u full explicit volume operand pairs and 255 pan targets with original signed parameter quantization\n",levels);
 static int16_t pcm[96000],reference[96000];uint64_t total_energy=0;for(unsigned floor=0;floor<5;++floor)for(unsigned type=0;type<2;++type){reset(12345);unsigned char command[16];memcpy(command,actual,16);command[8]=floor;command[12]=type;execute(command);render(pcm,800);check(!rx_event_se_tick(),"second original shot server phase");render(pcm+1600,47200);unsigned value=8192+pitch(12345,type)*3;
  reset(12345);unsigned char request[]={6,2,floor,0,64,value>>8,value},change[]={15,2,floor,0,64,64,value>>8,value};snd_synth_lock();snd_synth_command(request,sizeof(request));snd_synth_unlock();render(reference,800);snd_synth_lock();snd_synth_command(change,sizeof(change));snd_synth_unlock();render(reference+1600,47200);check(!memcmp(pcm,reference,sizeof(pcm)),"original shot transport PCM differs from independent backend commands");for(unsigned i=0;i<96000;++i)total_energy+=abs(pcm[i]);}
 check(total_energy>100000,"original footstep PCM absent");printf("PASS ten original floor/walk/run requests: 960000 exact stereo sample values against independent driver commands, energy %llu\n",(unsigned long long)total_energy);
 reset(1);execute(actual);render(pcm,800);execute(actual);render(pcm,800);execute(actual);check(__snd_work__[22].channel_num==8&&__snd_work__[21].channel_num==1&&PlayerFootStepSwitch[1]==1,"original alternating slots/retrigger channel rotation");
 puts("PASS original alternating left/right slots and active retrigger channel rotation/cancel path");
 reset(1);execute(actual);render(pcm,800);check(!rx_event_se_tick(),"parameter phase before pause");render(reference,800);reset(1);execute(actual);render(pcm,800);check(!rx_event_se_tick(),"same parameter phase");SND_WORK paused=__snd_work__[22];rx_event_audio_pause(1);render(pcm,800);check(!rx_event_se_tick()&&!memcmp(&paused,__snd_work__+22,sizeof(paused)),"paused original shot metadata advanced");for(unsigned i=0;i<1600;++i)check(!pcm[i],"paused shot not silent");rx_event_audio_pause(0);render(pcm,800);check(!memcmp(pcm,reference,3200),"shot resume lost exact PCM position");StopSe(14);check(!rx_event_audio_deliver(),"original shot stop");
 reset(1);execute(actual);render(pcm,800);rx_event_se_end();render(pcm,800);for(unsigned i=0;i<1600;++i)check(!pcm[i],"retired shot bank retained live PCM");check(!g_snd_banks.se_hd[2].data&&!g_snd_banks.bd[5].data&&!rx_event_se_mount(0),"shot bank owned release/remount");puts("PASS shot pause/exact resume, original stop, active bank retirement and remount");
 reset(1);unsigned char bad[16];memcpy(bad,actual,16);for(unsigned n=1;n<16;++n)reject(actual,n);bad[2]=3;reject(bad,16);memcpy(bad,actual,16);bad[7]=1;reject(bad,16);bad[7]=0;bad[8]=5;reject(bad,16);bad[8]=0;bad[12]=2;reject(bad,16);bad[12]=0;bad[3]=1;reject(bad,16);bad[3]=64;bad[5]=127;reject(bad,16);bad[5]=128;bad[11]=255;reject(bad,16);

 memcpy(bad,actual,16);bad[11]=30;BH_PWORK *source=&ene[rom->enep[1].wrk_no];float y=source->mlwP->owP[30].mtx[13];source->mlwP->owP[30].mtx[13]=NAN;reject(bad,16);source->mlwP->owP[30].mtx[13]=y;
 ML_WORK saved=*source->mlwP;source->mlwP->obj_num=27;reject(bad,16);*source->mlwP=saved;source->mlwP->owP=(void *)(uintptr_t)1;reject(bad,16);*source->mlwP=saved;bad[11]=31;reject(bad,16);
 SDSHOT handle=SeHandle[14];SeHandle[14]=(void*)(uintptr_t)1;reject(actual,16);SeHandle[14]=handle;SndBlob hd=g_snd_banks.se_hd[2];g_snd_banks.se_hd[2].data=(void*)(uintptr_t)1;reject(actual,16);g_snd_banks.se_hd[2]=hd;NJS_POINT3 position=CameraPos;CameraPos.x=NAN;reject(actual,16);CameraPos=position;
 puts("PASS atomic unsupported variant/fade/sentinel/floor/source/truncated/foreign-handle/bank/nonfinite-position preflight guards");
 rx_event_audio_end();check(!rx_event_audio_init(1)&&!rx_event_se_mount(0),"AAudio shot setup");uint64_t frames=rx_event_audio_frames();execute(actual);for(unsigned wait=0;wait<200&&rx_event_audio_frames()<frames+2000;++wait)usleep(10000);check(rx_event_audio_frames()>=frames+2000,"AAudio did not render original shot callback");puts("PASS original opening footstep through Quest AAudio callback");
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
