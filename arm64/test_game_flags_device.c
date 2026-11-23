#include "opening.h"
#include "boot_disc.h"
#include "game_runtime.h"
#include "message_runtime.h"
#include "message.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL game flags: %s\n",why);exit(1);}}
typedef struct {unsigned st,cb,sp,pt,gm,on,ps,old,onb,psb,oldb,exm,light;} State;
static State read_state(void){return (State){sys->st_flg,sys->cb_flg,sys->sp_flg,sys->pt_flg,sys->gm_flg,
 sys->pad_on,sys->pad_ps,sys->pad_old,sys->pad_onb,sys->pad_psb,sys->pad_oldb,sys->exm_attr,rom->lgtp[1].flg};}
static void write_state(State s){sys->st_flg=s.st;sys->cb_flg=s.cb;sys->sp_flg=s.sp;sys->pt_flg=s.pt;sys->gm_flg=s.gm;
 sys->pad_on=s.on;sys->pad_ps=s.ps;sys->pad_old=s.old;sys->pad_onb=s.onb;sys->pad_psb=s.psb;sys->pad_oldb=s.oldb;sys->exm_attr=s.exm;rom->lgtp[1].flg=s.light;}
static unsigned expand(unsigned v,const unsigned *bits,unsigned count){unsigned out=0;for(unsigned k=0;k<count;++k)if(v&(1u<<k))out|=bits[k];return out;}
static void restore_pad(State *s){s->on=s->onb;s->ps=s->psb;s->old=s->oldb;}
static void return_view(State *s){s->pt|=3;if(!(s->gm&0x20000)){s->st&=~1u;s->gm|=0x800;}}
static void transfer_result(State *s){if(s->cb&0x20000000)s->cb=(s->cb&~0x20000000u)|0x1000;}
/* Independent ordered state-transition specification. Completion consumes the
   previous tick's request; a fresh page confirmation queues the next tick. */
static State reference(State s){
 if(!(s.cb&1)){
  if(s.st&0x200000){s.st&=~0x200080u;transfer_result(&s);if(!(s.cb&4))s.st&=~4u;s.sp=UINT32_MAX;return_view(&s);restore_pad(&s);}
  if(s.st&0x400000){transfer_result(&s);
   if(s.cb&0x200000)s.st&=~0x200u;
   else{unsigned old=s.st;s.st&=~0x400200u;
    if(old&0x2000){s.sp=UINT32_MAX;s.st&=~0x2200u;if(!(s.cb&4))s.st&=~4u;restore_pad(&s);}
    if(old&0x80){s.st&=~0x80u;return_view(&s);}
   }
  }
 }
 if(s.cb&8){s.cb^=8;s.sp=0x38|((s.exm&0x20)?0:0x80)|((s.exm&0x10)?2:0);
  s.pt&=~((s.exm&2?1u:0)|(s.exm&4?2u:0));if(s.exm&2)s.light&=~2u;}
 if(s.st&0x80){if((s.gm&0x30000)==0x30000)s.sp|=0x40;if(!(s.st&0x200)&&(s.ps&0x200))s.st|=0x200000;}
 if(s.cb&0x20){s.cb^=0x20;s.sp=0x30|((s.st&0x80)?(8|((s.exm&0x20)?0:0x80)):0)|((s.exm&0x10)?2:0);}
 if((s.st&0x5200)==0x5200&&((s.ps&0x1800)||((s.cb&0x1000)&&!(s.cb&1)))){
  s.st|=0x400000;s.cb=(s.cb|0x2000)&~0x1000u;
 }
 return s;
}
static void tick(void){const char *e=rx_game_flags_tick();check(!e,e);}
int main(int argc,char **argv){
 check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening o={0};
 const char *e=rx_android_mount_disc(&disc,argv[1]);check(!e,e);e=rx_android_start_disc(&disc,&boot);check(!e,e);e=rx_opening_start(&o,0);check(!e,e);
 check(rom->lgt_n>=2,"original interaction light fixture");State original=read_state();
 const unsigned st_bits[]={0x200000,0x400000,0x80,0x2000,0x200,0x4000,0x1000};
 const unsigned cb_bits[]={1,4,8,0x20,0x1000,0x200000,0x20000000};
 const unsigned gm_bits[]={0x10000,0x20000},exm_bits[]={2,4,0x10,0x20},pad_bits[]={0x200,0x800,0x1000};
 const unsigned count=1u<<23;
 for(unsigned i=0;i<count;++i){State s={0};
  s.st=0x00800005|expand(i,st_bits,7);s.cb=0x08000040|expand(i>>7,cb_bits,7);
  s.gm=0x40008030|expand(i>>14,gm_bits,2);s.exm=0x80|expand(i>>16,exm_bits,4);
  s.on=0x7000000c|expand(i>>20,pad_bits,3);s.ps=s.on;s.old=~s.on;
  s.onb=i*0x9e3779b9u;s.psb=~s.onb;s.oldb=i^0x12345678;s.sp=i^0x76543210;s.pt=i^0x87654321;s.light=i^0xa5a5a5a5;
  State expected=reference(s);write_state(s);tick();State after=read_state();
  if(memcmp(&expected,&after,sizeof(after))){printf("Case %u st%x cb%x gm%x exm%x pad%x\n",i,s.st,s.cb,s.gm,s.exm,s.ps);check(0,"whole original transition state differs from independent specification");}
 }
 printf("PASS %u original post-model transitions: exhaustive message/cancel/door/result/task/confirm/view/light branches and ordered pad restoration\n",count);
 write_state(original);sys->st_flg=0;sys->cb_flg=0;sys->pad_ps=sys->pad_on=0;bhSetMessage(0,0);sys->mes_ct=4096;
 e=rx_message_tick(0);check(!e,e);check((sys->st_flg&0x5200)==0x5200,"original full-page completion");
 sys->pad_ps=0x800;tick();check((sys->st_flg&0x400200)==0x400200&&(sys->cb_flg&0x2000),"fresh confirm did not queue original message completion");
 tick();check(!(sys->st_flg&0x400200),"next original tick did not retire confirmed page");
 sys->st_flg=0x5200;sys->cb_flg=0;sys->pad_ps=0x1000;tick();check(sys->st_flg&0x400000,"alternate original confirmation missing");
 sys->st_flg=0x5200;sys->cb_flg=1;sys->pad_ps=0;tick();check(!(sys->st_flg&0x400000),"door gate synthesized confirmation");
 puts("PASS original font/controller end-page plus fresh/alternate confirm and deferred next-tick retirement");
 write_state(original);sys->cb_flg=8;sys->exm_attr=2;sys->st_flg=0x600080;State before=read_state();LGT_WORK *lights=rom->lgtp;int light_count=rom->lgt_n;
 rom->lgtp=(void *)(uintptr_t)1;check(rx_game_flags_tick()!=NULL,"foreign room light accepted");rom->lgtp=lights;State after=read_state();check(!memcmp(&before,&after,sizeof(after)),"foreign light guard executed scalar prefix");
 rom->lgtp=NULL;check(rx_game_flags_tick()!=NULL,"null room light accepted");rom->lgtp=lights;after=read_state();check(!memcmp(&before,&after,sizeof(after)),"null light guard executed scalar prefix");
 rom->lgt_n=1;check(rx_game_flags_tick()!=NULL,"short room light bank accepted");rom->lgt_n=light_count;after=read_state();check(!memcmp(&before,&after,sizeof(after)),"short light guard executed scalar prefix");
 rom->lgtp=NULL;sys->exm_attr=0;tick();rom->lgtp=lights;check(!(sys->cb_flg&8),"pointer-free task transition was unnecessarily blocked");
 puts("PASS atomic foreign/null/short interaction-light boundaries and pointer-free branch independence");
 write_state(original);check(!o.events_ready,"full opening unexpectedly enabled");rx_opening_end(&o);sys->cb_flg=8;sys->exm_attr=2;check(rx_game_flags_tick()!=NULL,"retired room light owner accepted");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
