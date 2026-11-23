#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "room.h"
#include "event.h"
#include "main.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern BH_SCEWORK bhEtask[16],*bhCetask;extern unsigned char *bhScePtr;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL actor control: %s\n",why);exit(1);}}
static unsigned state_cases;
static void verify(BH_PWORK *a,unsigned sub,unsigned operand,unsigned pad){
 BH_PWORK expected=*a;BH_SCEWORK task;memset(&task,0xa5,sizeof(task));task.work=a;task.mode0=7;task.mode1=99;bhEtask[0]=task;bhCetask=bhEtask;
 unsigned char command[]={0x67,sub,operand,pad};unsigned bytes=sub==149?4:2;
 if(sub==128){expected.mnwP=sys->emtp[a->id];expected.mode0=1;expected.mode2=0;expected.ct2=(int)((unsigned)expected.ct2&~255u);expected.stflg&=~0x10000u;expected.flg&=~0x200000u;}
 if(sub==144){expected.mode0=1;expected.mode2=0;expected.mode3=3;expected.ct2=(int)((unsigned)expected.ct2&~255u);expected.stflg&=~0x10000u;expected.flg&=~0x200000u;}
 if(sub==149){if(operand)expected.flg|=0x10000;else expected.flg&=~0x10000u;}
 if(sub==128||sub==139||sub==144)task.mode0=task.mode1=0;
 if(sub!=0&&sub!=139)check(!bhCheckNativeSubControl(sub,a),"valid actor control ownership");
 bhScePtr=command;check(bhSubCtr()==1&&bhScePtr==command+bytes,"original actor control length/result");
 check(!memcmp(&expected,a,sizeof(expected)),"whole actor state differs from original control contract");check(!memcmp(&task,bhEtask,sizeof(task)),"original actor control changed unexpected task state");++state_cases;
}
static unsigned char data[64];static RxRoomScripts script;
static const char *run(unsigned record,const unsigned char *command,unsigned bytes,int delayed){memset(data,0,sizeof(data));data[0]=12;data[4]=18;data[8]=20;data[12]=20;data[20]=0x65;data[21]=1;data[22]=record;unsigned start=24;if(delayed)data[start++]=0xfe;memcpy(data+start,command,bytes);data[start+bytes]=0xff;check(!rx_room_scripts_open(data,start+bytes+2,&script),"actor control task fixture");rom->evtp=&script;return rx_events_start_room();}
static void reject(const unsigned char *command,unsigned bytes){unsigned char data[32]={8,0,0,0,0,0,0,0,5,1,30,0,0,0};data[4]=14+bytes;memcpy(data+14,command,bytes);RxRoomScripts bad;check(!rx_room_scripts_open(data,16+bytes,&bad),"bad actor control fixture");rom->evtp=&bad;unsigned flags=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"bad actor control executed preceding flags");sys->ev_flg[0]=flags;}
static unsigned bits(unsigned i){return i<2?(i?~0u:0):i<34?1u<<(i-2):~(1u<<(i-34));}
int main(int argc,char **argv){check(argc==2,"supply OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_opening_check_events(&fault);check(error&&!opening.events_ready,"complete opening must remain gated");printf("Opening readiness %02x offset %zu: %s\n",fault.opcode,fault.offset,error);check(fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"unexpected next original dependency");
 for(unsigned record=0;record<2;++record){BH_PWORK *actor=&ene[rom->enep[record].wrk_no];BH_PWORK saved=*actor;check((uintptr_t)sys->emtp[actor->id]>UINT32_MAX,"full-width native enemy motion bank");
  for(unsigned sub=128;sub<=144;sub+=16)for(unsigned flag=0;flag<66;++flag)for(unsigned status=0;status<66;++status){*actor=saved;actor->flg=bits(flag);actor->stflg=bits(status);actor->mode0=7;actor->mode1=flag;actor->mode2=42;actor->mode3=status;actor->ct2=(int)(0xa5ff0000u|flag<<8|status);actor->mtn_no=0x1234;actor->frm_no=0x004d1234;actor->mtn_add=12345;verify(actor,sub,0,0);}
  for(unsigned sub=128;sub<=144;sub+=16)for(unsigned byte=0;byte<256;++byte)for(unsigned pattern=0;pattern<4;++pattern){*actor=saved;actor->ct2=(int)((unsigned[]){0,0xffffff00,0x12345600,0x80000000}[pattern]|byte);verify(actor,sub,0,0);}
  for(unsigned operand=0;operand<256;++operand)for(unsigned pad=0;pad<256;++pad){*actor=saved;actor->flg=bits((operand+pad)%66);verify(actor,149,operand,pad);}
  for(unsigned sub=0;sub<=139;sub+=139){*actor=saved;verify(actor,sub,0,0);}
  *actor=saved;
 }
 printf("PASS %u original actor-control states: both actors, flag bits/complements, low-byte counters, preserved modes/frame fields, all flag operands/padding, native bank identities and exact task release\n",state_cases);
 for(unsigned sub=0;sub<256;++sub)if(sub!=0&&sub!=128&&sub!=139&&sub!=144&&sub!=149)reject((unsigned char[]){0x67,sub,0,0,0,0,0,0},8);
 reject((unsigned char[]){0x67,128},1);for(unsigned bytes=1;bytes<4;++bytes)reject((unsigned char[]){0x67,149,1,255},bytes);
 BH_PWORK *actor=&ene[rom->enep[0].wrk_no];MN_WORK *bank=sys->emtp[actor->id];sys->emtp[actor->id]=(void *)(uintptr_t)1;reject((unsigned char[]){0x67,128},2);sys->emtp[actor->id]=plp->mnwPb;reject((unsigned char[]){0x67,128},2);sys->emtp[actor->id]=bank;
 ETTY_WORK *mapping=rom->enep;rom->enep=(void *)(uintptr_t)1;reject((unsigned char[]){0x67,144},2);rom->enep=mapping;
 for(unsigned sub=128;sub<=149;++sub)if(sub==128||sub==144||sub==149)for(unsigned pointer=0;pointer<3;++pointer){unsigned char command[]={0x67,sub,1,255};unsigned bytes=sub==149?4:2;check(!run(0,command,bytes,1),"start delayed actor control");BH_PWORK before=*actor;bhEtask[0].work=pointer==0?NULL:pointer==1?(void *)(uintptr_t)1:plp;check(rx_events_tick()!=NULL&&!memcmp(&before,actor,sizeof(before)),"invalid target reached original actor handler");}
 /* No-op and task-only release never dereference a work pointer. */
 for(unsigned sub=0;sub<=139;sub+=139){unsigned char command[]={0x67,sub};check(!run(0,command,2,1),"start delayed task-only control");BH_PWORK before=*actor;bhEtask[0].work=(void *)(uintptr_t)1;check(!rx_events_tick()&&!memcmp(&before,actor,sizeof(before)),"task-only control touched foreign work");}
 check(original->data[2482]==0x67&&original->data[2483]==128,"actual original actor handoff bytes");BH_PWORK before=*actor;error=run(0,original->data+2482,2,0);check(!error,error);check(actor->mnwP==bank&&actor->mode0==1&&actor->mode2==0&&actor->mode1==before.mode1&&actor->mode3==before.mode3&&!bhEtask[0].mode0&&!bhEtask[0].mode1,"actual source bank/control handoff");
 puts("PASS actual opening actor-handoff bytes, variable lengths, atomic unsupported/truncated/foreign-bank/mapping/null/reassigned-target rejection and work-free task release");
 rx_events_reset();rom->evtp=original;rx_opening_end(&opening);check(bhCheckNativeSubControl(128,NULL)!=NULL,"retired actor handoff context accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;}
