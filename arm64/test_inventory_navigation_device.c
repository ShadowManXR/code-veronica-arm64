#include "opening.h"
#include "boot_disc.h"
#include "inventory_status.h"
#include "inventory_sprite.h"
#include "item_names.h"
#include "event_audio.h"
#include "particle_draw.h"
#include "android_egl.h"
#include "pad_input.h"
#include "system.h"
#include "main.h"
#include "sub1.h"
#include "message.h"
#include "fileview.h"
#include "ps2_sg_maloc.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
extern S_WORK swork;
extern SITEM sitem;
extern PARTS parts_09b[8],parts_00b[24];
static unsigned char *cursor;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL inventory navigation: %s\n",why);exit(1);}}
static void good(const char *error){check(!error,error);}
static void ready(unsigned capacity,unsigned wide){
    rx_inventory_status_reset();sys->memp=cursor;sys->ply_id=0;sys->st_flg=8;sys->cb_flg=0;sys->ts_flg=0x1200;rx_sound_queue_reset();
    sys->mn_mode0=5;sys->mn_mode1=sys->mn_mode2=sys->mn_mode3=0;sys->mn_md0=sys->mn_md1=sys->mn_md2=sys->mn_md3=0;
    memset(sys->itm,0,64);int entered;good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());
    if(capacity==10)sys->gm_flg|=0x8000000;else sys->gm_flg&=~0x8000000;
    swork.pip[1]=41u<<16|1;for(unsigned i=0;i<capacity-wide;++i)swork.pip[i+2]=i<wide?(2u<<16|300):(20u<<16|1);
    good(rx_inventory_status_build_items());swork.statusflg&=~(0x8000|0x40|0x10000|0x2000|0x10000000);sys->pad_on=sys->pad_ps=0;
}
static unsigned physical(unsigned cell,unsigned wide){return cell==1?1:cell<2+2*wide?2+(cell-2)/2:cell-wide;}
static void cancel_reference(S_WORK *st,float cen[12][6]){
    st->statusflg=(st->statusflg|4|0x20000)&~0x10000;st->mode=32;
    cen[1][3]=-80;cen[2][2]=-448;cen[3][2]=640;cen[4][2]=808;cen[5][2]=644;cen[6][3]=484;cen[8][2]=-380;cen[9][2]=644;cen[10][3]=-384;cen[11][2]=660;
    for(unsigned i=1;i<12;++i){cen[i][4]=cen[i][2]-(i==3?352:cen[i][0]);if(i!=3)cen[i][5]=cen[i][3]-cen[i][1];}
}
static void selection_reference(S_WORK *st,float cen[12][6],unsigned char *attr,unsigned capacity,unsigned wide,unsigned held,unsigned pressed){
    if(st->statusflg&0x8000)st->statusflg=(st->statusflg|0x80|0x10000)&~(0x10|0x8000);
    st->itemid=st->pip[st->listcsr_0]>>16&255;st->statusflg|=0x100000;
    if(st->statusflg&0x40)return;
    if(pressed&0x5800){st->keywait=st->boxwait=0;
        if(pressed&0x800){if(!(st->statusflg&0x20)&&st->itemid){st->testmode=0;st->mode=4;st->flag_=0;}}
        else if(pressed&0x1000){*attr&=~0x20;st->mode=2;st->testmode=0;st->maincsr=3;}
        else{st->testmode=2;cancel_reference(st,cen);st->flgchk&=~2;}return;
    }
    if(st->statusflg&0x20)return;
    if(held&15){if(st->keywait==5)++st->boxwait;else ++st->keywait;}else st->keywait=st->boxwait=0;
    if(st->keywait!=1&&st->boxwait<3)return;st->boxwait=0;
    unsigned cell=st->listcsr_2;int vertical=0;
    if(held&1){if(cell==1){*attr&=~0x20;st->mode=2;st->testmode=0;st->maincsr=3;st->keywait=st->boxwait=0;vertical=1;}else{cell=cell<3?1:cell-2;vertical=1;}}
    else if((held&2)&&cell<capacity){cell+=2;vertical=1;}
    if(held&8){if(cell<capacity+1){unsigned p=physical(cell,wide);if(p<wide+2){if(cell<capacity)cell+=cell&1?1:2;}else ++cell;}}
    else if(held&4){if(cell>2){if(physical(cell,wide)<wide+2){if(physical(cell,wide)>2)cell-=2;}else --cell;}else if(cell==1&&!vertical){*attr&=~0x20;st->mode=2;st->testmode=0;st->maincsr=3;st->keywait=st->boxwait=0;}}
    st->listcsr_2=cell;st->listcsr_0=physical(cell,wide);
}
static void rejected(void){S_WORK before=swork;float cen[12][6];memcpy(cen,cen_pos,sizeof(cen));PARTS sprites[24];memcpy(sprites,parts_00b,sizeof(sprites));
    check(rx_inventory_status_input_tick()!=NULL&&!memcmp(&before,&swork,sizeof(before))&&!memcmp(cen,cen_pos,sizeof(cen))&&!memcmp(sprites,parts_00b,sizeof(sprites)),"rejected input changed source menu/work");}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
    good(rx_android_mount_disc(&disc,argv[1]));good(rx_android_start_disc(&disc,&boot));good(rx_opening_start(&opening,0));cursor=sys->memp;
    const unsigned char *bank;size_t bank_bytes;unsigned count;good(rx_system_messages_data(&bank,&bank_bytes,&count));check(count==494&&bank_bytes==83624&&(uintptr_t)bank>UINT32_MAX,"owned original system-message bank");
    const unsigned pressed[]={0,0x800,0x1000,0x4000,0x1800,0x4800,0x5000,0x5800};unsigned cases=0;
    for(unsigned capacity=8;capacity<=10;capacity+=2)for(unsigned wide=0;wide<=capacity/2;++wide){
        ready(capacity,wide);S_WORK base=swork;float centers[12][6];memcpy(centers,cen_pos,sizeof(centers));unsigned char attr=parts_09b[0].atr;
        for(unsigned cell=1;cell<=capacity+1;++cell)for(unsigned held=0;held<16;++held)for(unsigned key=0;key<6;++key)for(unsigned repeat=0;repeat<3;++repeat)for(unsigned button=0;button<8;++button)for(unsigned lock=0;lock<3;++lock){
            swork=base;memcpy(cen_pos,centers,sizeof(centers));parts_09b[0].atr=attr;swork.testmode=1;swork.listcsr_2=cell;swork.listcsr_0=physical(cell,wide);swork.keywait=key;swork.boxwait=repeat;swork.statusflg|=(unsigned[]){0,0x20,0x40}[lock];
            sys->pad_on=held;sys->pad_ps=pressed[button];rx_sound_queue_reset();S_WORK expected=swork;float expected_cen[12][6];memcpy(expected_cen,centers,sizeof(centers));unsigned char expected_attr=attr;
            selection_reference(&expected,expected_cen,&expected_attr,capacity,wide,held,pressed[button]);unsigned items[16];memcpy(items,swork.pip,sizeof(items));
            const char *error=rx_inventory_status_input_tick();if(error){fprintf(stderr,"cap%u wide%u cell%u held%u key%u repeat%u press%x lock%u\n",capacity,wide,cell,held,key,repeat,pressed[button],lock);good(error);}
            check(!memcmp(&expected,&swork,sizeof(expected))&&!memcmp(expected_cen,cen_pos,sizeof(expected_cen))&&parts_09b[0].atr==expected_attr&&!memcmp(items,swork.pip,sizeof(items)),"source navigation/repeat/priority/handoff differs from independent grid reference");++cases;
        }
    }
    printf("PASS %u original CursorMove states: both capacities, every wide prefix/logical cell, all direction masks, repeat counters, button priorities and locks; exact mode/item/cancel targets and no item-word writes\n",cases);
    unsigned menus=0;
    for(unsigned selected=0;selected<4;++selected)for(unsigned held=0;held<16;++held)for(unsigned key=0;key<6;++key)for(unsigned repeat=0;repeat<3;++repeat)for(unsigned lock=0;lock<3;++lock){
        ready(8,0);swork.mode=2;swork.testmode=1;swork.number=1;swork.wait=0;swork.maincsr=selected;swork.keywait=key;swork.boxwait=repeat;swork.statusflg|=(unsigned[]){0,0x20,0x40}[lock];
        bhSetMessage(1,180-selected);sys->pad_on=held;sys->pad_ps=0;unsigned k=key,r=repeat,target=selected;
        if(!(swork.statusflg&0x40)){if(held&12){if(k==5)++r;else ++k;}else k=r=0;if(k==1||r>=3){r=0;if(!(swork.statusflg&0x20)){if(held&8){if(target<3)++target;}else if((held&4)&&target>0)--target;}}}
        rx_sound_queue_reset();good(rx_inventory_status_input_tick());check(swork.maincsr==target&&swork.keywait==k&&swork.boxwait==r&&swork.mode==2,"original top-menu direction/repeat bounds");
        check(sys->mes_idx==(int)(181-target)&&sys->mes_dp==(unsigned int*)(bank+sys->mes_sp[181-target]),"original system help pointer/index handoff");
        for(unsigned i=0;i<4;++i)check(parts_00b[i].parts_num==(i==target?11:0)&&parts_00b[i+11].parts_num==(i==target?12:10),"original top-menu highlight table selection");++menus;
    }
    for(unsigned game=0;game<4;++game)for(unsigned selected=0;selected<4;++selected){
        ready(8,0);sys->gm_mode=game;swork.mode=2;swork.maincsr=selected;swork.testmode=0;bhSetMessage(1,game==3&&selected==2?199:180-selected);sys->st_flg|=0x4000;sys->pad_ps=0x800;sys->pad_on=0;rx_sound_queue_reset();good(rx_inventory_status_input_tick());
        if(game==3&&selected==2){check(swork.mode==2&&swork.testmode==1&&!swork.wait,"original battle-mode map refusal");continue;}
        check(swork.mode==2&&swork.testmode==2&&swork.wait==3&&swork.number==2,"original confirm delay entry");sys->pad_ps=0;
        for(unsigned tick=1;tick<=4;++tick){rx_sound_queue_reset();good(rx_inventory_status_input_tick());if(tick<4)check(swork.mode==2&&swork.wait==3-(int)tick,"original four-frame confirmation delay");}
        check(swork.mode==(unsigned[]){32,8,32,1}[selected],"original EXIT/FILE/MAP/ITEM handoff");
        if(selected==1)check(swork.itemid==139&&swork.wn==2&&sitem.mw.rdid==139&&(sitem.mw.flg&1)&&fvwork.mode_00==0&&fvwork.filenum==0,"original file request metadata");
        if(selected==2)check(swork.flgchk&8,"original map request flag");
        if(selected==3){check(!swork.testmode,"original return to item cursor");good(rx_inventory_status_input_tick());check(swork.listcsr_0==2&&swork.listcsr_2==2&&swork.testmode==1,"original cursor initialization on menu return");}else rejected();
    }
    printf("PASS %u top-menu repeat/navigation cases and all confirm-delay handoffs across four game modes; file/map/closing handlers remain unexecuted\n",menus);
    sys->gm_mode=0;
    for(unsigned control=0;control<4;++control)for(unsigned direction=0;direction<4;++direction){
        ready(8,0);swork.testmode=1;swork.listcsr_0=swork.listcsr_2=4;RxPadInput pad;rx_pad_init(&pad);sys->keytype=control;sys->sp_flg=~0u;sys->tk_flg=0x1000;sys->pad_on=sys->pad_oncpy=sys->pad_ps=sys->pad_old=0;rx_pad_poll_game(&pad);bhSysCallPad();
        pad.keys=(unsigned[]){RX_PAD_UP,RX_PAD_RIGHT,RX_PAD_DOWN,RX_PAD_LEFT}[direction];rx_pad_poll_game(&pad);bhSysCallPad();rx_sound_queue_reset();good(rx_inventory_status_input_tick());check(swork.listcsr_2==(unsigned[]){2,5,6,3}[direction],"original four control tables did not feed cursor navigation");
    }
    ready(8,1);swork.pip[2]=20u<<16|1;rejected();swork.pip[2]=2u<<16|300;swork.testmode=1;swork.listcsr_0=3;swork.listcsr_2=3;rejected();swork.listcsr_0=2;swork.listcsr_2=2;swork.keywait=6;rejected();swork.keywait=0;swork.pip[11]=255u<<16;rejected();swork.pip[11]=0;
    ready(8,0);swork.mode=2;swork.maincsr=4;rejected();swork.maincsr=3;swork.number=3;rejected();swork.number=1;
    unsigned int *owned=sys->mes_sp;sys->mes_sp=(void*)(uintptr_t)1;rejected();sys->mes_sp=owned;unsigned offset=owned[178];owned[178]=UINT32_MAX;rejected();owned[178]=offset;
    puts("PASS all four original pad tables feed navigation; malformed cursor/boundary/phase/system-bank states reject before source menu writes");
    unsigned raw_bytes;unsigned char *raw=rx_boot_read_file("SYSMES.ALD",&raw_bytes);check(raw!=NULL,"system message fixture");rx_item_names_clear();
    for(size_t n=0;n<89084;++n)check(rx_system_messages_load(raw,n)!=NULL&&!sys->mes_sp,"truncated second bank published");good(rx_item_names_load(raw,raw_bytes));good(rx_system_messages_load(raw,raw_bytes));syFree(raw);
    ready(8,0);rx_inventory_status_reset();sys->memp=cursor;unsigned resource_bytes;unsigned char *resources=rx_boot_read_member(4,145,&resource_bytes);check(resources!=NULL,"status atlases");good(rx_inventory_status_load_resources(resources,resource_bytes));syFree(resources);
    int entered;sys->ts_flg|=0x200;good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());good(rx_inventory_status_build_items());for(unsigned t=0;t<16;++t)good(rx_inventory_sprite_tick());
    RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"Quest menu EGL");glViewport(0,0,640,480);RxParticleDraw *draw=NULL;RxInventorySpriteFrame sprite={0};unsigned gpu=0;const unsigned help[]={177,178,179,180,199};
    size_t groups_count;const RxInventorySpriteGroup *groups=rx_inventory_status_groups(&groups_count);
    for(unsigned i=0;i<5;++i){bhSetMessage(1,help[i]);sys->mes_ct=1000;sys->pad_on=sys->pad_ps=0;good(rx_message_tick(2));check(rx_message_frame()->count>0&&rx_system_message_current(),"system help did not reach original message controller");
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);swork.mode=2;swork.maincsr=help[i]==199?2:180-help[i];sys->gm_mode=help[i]==199?3:0;swork.testmode=1;swork.number=1;swork.wait=0;good(rx_inventory_status_input_tick());
        for(unsigned n=0;n<groups[0].end;++n){good(rx_inventory_sprite_collect(0,n,&sprite));good(rx_inventory_sprite_present(&draw,&sprite));}good(rx_message_present(&draw));check(glGetError()==GL_NO_ERROR,"original top menu/system help failed on Quest GPU");++gpu;
    }
    printf("PASS owned 494-entry second bank, 89084 truncated prefixes, five original help messages through message controller and Quest GLES alongside source header sprites (%u draws)\n",gpu);
    rx_inventory_sprite_destroy(&sprite);rx_particle_close(&draw);rx_egl_close(&egl);check(!opening.events_ready,"full runtime unexpectedly enabled");rx_opening_end(&opening);check(!sys->mes_ip&&!sys->mes_sp,"system text roots survived teardown");rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
