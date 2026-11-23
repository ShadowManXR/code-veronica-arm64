#include "opening.h"
#include "boot_disc.h"
#include "event_runtime.h"
#include "room_scripts.h"
#include "room.h"
#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern BH_SCEWORK bhEtask[16];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL item speed: %s\n",why);exit(1);}}
static unsigned char code[24];static RxRoomScripts scripts;
static O_WRK initial[32],expected[32],snapshot[32];
static const char *run(unsigned slot,unsigned speed,unsigned pad){memset(code,0,sizeof(code));code[0]=8;code[4]=18;unsigned char body[]={0x42,slot,speed,pad,5,1,30,0,0,0};memcpy(code+8,body,sizeof(body));check(!rx_room_scripts_open(code,20,&scripts),"item speed fixture");rom->evtp=&scripts;sys->ev_flg[0]&=~2u;return rx_events_start_room();}
static void reject(unsigned slot,unsigned bytes){unsigned char raw[24]={8,0,0,0,0,0,0,0,5,1,30,0,0,0,0x42,slot,255,255};raw[4]=14+bytes;RxRoomScripts bad;check(!rx_room_scripts_open(raw,16+bytes,&bad),"invalid item speed fixture");rom->evtp=&bad;unsigned flag=sys->ev_flg[0];sys->ev_flg[0]=0;check(rx_events_start_room()!=NULL&&!sys->ev_flg[0],"invalid speed executed prefix");sys->ev_flg[0]=flag;}
int main(int argc,char **argv){check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);error=rx_android_start_disc(&disc,&boot);check(!error,error);error=rx_opening_start(&opening,0);check(!error,error);
 RxRoomScripts *original=rom->evtp;RxEventFault fault={0};error=rx_events_validate_room(&fault);
 if(error)printf("Opening preflight %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
 check(!error&&opening.commands_ready&&!opening.events_ready,"all opening commands must validate without enabling missing live runtime");
 error=rx_opening_check_events(&fault);check(error&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&!fault.offset,"explicit live-runtime readiness gate");
 printf("PASS original opening bank: all %zu entries/%zu script bytes validate; full live runtime remains gated: %s\n",original->slots,original->bytes,error);
 check((uintptr_t)sys->itwp>UINT32_MAX,"item pool must use full-width native addresses");memcpy(initial,sys->itwp,sizeof(initial));memcpy(expected,initial,sizeof(expected));unsigned cases=0;
 for(unsigned slot=0;slot<32;++slot)for(unsigned speed=0;speed<256;++speed){expected[slot].aspd=speed;
  for(unsigned pad=0;pad<256;++pad){sys->itwp[slot].aspd=(int)(0x80001234u|(slot<<16));error=run(slot,speed,pad);check(!error,error);check(sys->ev_flg[0]&2,"original speed command length");check(!memcmp(expected+slot,sys->itwp+slot,sizeof(O_WRK)),"speed command changed unrelated item fields or consumed padding");++cases;}
  check(!memcmp(expected,sys->itwp,sizeof(expected)),"speed command changed a neighboring item slot");
 }
 printf("PASS %u whole-item updates: all 32 slots/every unsigned speed byte/every ignored padding byte, full aspd replacement and entire neighboring pool preservation\n",cases);
 unsigned offsets[]={7670,7674,7798,7802};for(unsigned i=0;i<4;++i){const unsigned char *p=original->data+offsets[i];check(original->bytes>offsets[i]+4&&p[0]==0x42&&p[1]==(i&1?4:0)&&p[2]==(i<2?100:0),"actual opening item speed bytes");error=run(p[1],p[2],p[3]);check(!error,error);check(sys->itwp[p[1]].aspd==p[2],"authored item speed result");}
 memcpy(snapshot,sys->itwp,sizeof(snapshot));for(unsigned slot=32;slot<256;++slot)reject(slot,4);for(unsigned bytes=1;bytes<4;++bytes)reject(0,bytes);check(!memcmp(snapshot,sys->itwp,sizeof(snapshot)),"rejected commands changed pool");
 O_WRK *pool=sys->itwp;const uintptr_t foreign[]={0,1,(uintptr_t)(pool+1),(uintptr_t)&ene[0]};for(unsigned i=0;i<4;++i){sys->itwp=(void *)foreign[i];reject(0,4);}sys->itwp=pool;
 void *records=rom->itmp;rom->itmp=(void *)(uintptr_t)1;reject(0,4);rom->itmp=records;int count=rom->itm_n;rom->itm_n=33;reject(0,4);rom->itm_n=count;
 unsigned char wait[]={12,0,0,0,18,0,0,0,20,0,0,0,0x14,0,0,0,0,0,0,0,0xfe,0x42,0,123,255,0xff,0};RxRoomScripts delayed;check(!rx_room_scripts_open(wait,sizeof(wait),&delayed),"delayed speed fixture");rom->evtp=&delayed;check(!rx_events_start_room(),"start delayed speed");memcpy(snapshot,pool,sizeof(snapshot));sys->itwp=(void *)(uintptr_t)1;check(rx_events_tick()!=NULL&&!memcmp(snapshot,pool,sizeof(snapshot)),"late foreign pool reached original speed handler");sys->itwp=pool;
 memcpy(sys->itwp,initial,sizeof(initial));rx_events_reset();rom->evtp=original;RxEventFault before={0};check(!rx_events_validate_room(&before),"original command coverage regressed after fixtures");unsigned flags=sys->ev_flg[0],cb=sys->cb_flg;BH_PWORK player=*plp;check(rx_opening_check_events(&before)!=NULL&&!opening.events_ready&&sys->ev_flg[0]==flags&&sys->cb_flg==cb&&!memcmp(&player,plp,sizeof(player)),"readiness gate executed a campaign prefix");
 puts("PASS four authored item-speed sites; atomic invalid slot/truncation/foreign/shifted pool/room mapping guards, late runtime rejection and non-mutating full-game readiness check");
 rx_opening_end(&opening);check(bhCheckNativeItemSpeed(0)!=NULL,"item pool owner survived room teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);puts("PASS native item-speed context retirement");return 0;}
