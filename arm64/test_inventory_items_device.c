#include "opening.h"
#include "boot_disc.h"
#include "inventory_status.h"
#include "inventory_sprite.h"
#include "event_audio.h"
#include "particle_draw.h"
#include "android_egl.h"
#include "main.h"
#include "sub1.h"
#include "item.h"
#include "ps2_sg_maloc.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
extern S_WORK swork;
extern NJS_TEXANIM parts_18[16];
_Static_assert(sizeof(ITEM_WORK)==6,"Original packed item metadata changed");
static unsigned char *cursor;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL inventory items: %s\n",why);exit(1);}}
static void good(const char *error){check(!error,error);}
static unsigned type(unsigned id){const unsigned char *p=(const void*)itemdata;p+=id*6+4;return p[0]|(unsigned)p[1]<<8;}
static void ready(unsigned owner,unsigned extended){
    rx_inventory_status_reset();sys->memp=cursor;sys->ply_id=owner;sys->st_flg=8;sys->cb_flg=0;sys->ts_flg=0x1200;
    rx_sound_queue_reset(); /* Independent cold entries have no audio frame loop. */
    sys->mn_mode0=5;sys->mn_mode1=sys->mn_mode2=sys->mn_mode3=0;memset(sys->itm+owner*16,0,64);
    int entered;good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());
    if(extended)sys->gm_flg|=0x8000000;else sys->gm_flg&=~0x8000000;
}
typedef struct {unsigned word,slot,key;} Entry;
static int order(const void *a,const void *b){const Entry *x=a,*y=b;return x->key!=y->key?(x->key>y->key?1:-1):(x->slot>y->slot?1:x->slot<y->slot?-1:0);}
static unsigned mark(unsigned word,unsigned flags){unsigned id=(word>>16)&255,t=type(id);if(((t&0x111)&&(flags&0x10))||((t&2)&&(flags&0x20)))word|=0x08000000;return word;}
static unsigned append_digits(unsigned word,unsigned *out,unsigned n){
    unsigned id=(word>>16)&255,t=type(id),value=word&65535;if(!(t&0x3800))return n;
    if(word&0x08000000){out[n++]=11;return n;}
    int percent=(t&0x1800)==0x1000;
    if(percent){value/=itemdata[id].max/100;if((word&65535)&&!value)value=1;}
    char digits[16];snprintf(digits,sizeof(digits),"%u",value);
    for(const char *p=digits;*p;++p)out[n++]=(unsigned)(*p-'0');if(percent)out[n++]=10;return n;
}
static void reject(void){
    size_t count;const RxInventorySpriteGroup *groups=rx_inventory_status_groups(&count);
    unsigned before_items[384];memcpy(before_items,sys->itm,sizeof(before_items));S_WORK before=swork;
    unsigned char sprites[16][40*sizeof(PARTS)];size_t ends[16];NJS_TEXANIM icons[16];memcpy(icons,parts_18,sizeof(icons));
    for(unsigned i=0;i<count;++i){ends[i]=groups[i].end;memcpy(sprites[i],groups[i].parts,groups[i].count*sizeof(PARTS));}
    check(rx_inventory_status_build_items()!=NULL,"malformed item display admitted");
    check(!memcmp(before_items,sys->itm,sizeof(before_items))&&!memcmp(&before,&swork,sizeof(before))&&!memcmp(icons,parts_18,sizeof(icons)),"rejection mutated item/work/UV data");
    for(unsigned i=0;i<count;++i)check(ends[i]==groups[i].end&&!memcmp(sprites[i],groups[i].parts,groups[i].count*sizeof(PARTS)),"rejection mutated sprite ownership/data");
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
    good(rx_android_mount_disc(&disc,argv[1]));good(rx_android_start_disc(&disc,&boot));good(rx_opening_start(&opening,0));cursor=sys->memp;
    check((uintptr_t)cursor>UINT32_MAX,"native work must exercise addresses above 4GiB");
    unsigned cases=0;
    for(unsigned owner=0;owner<4;++owner)for(unsigned extended=0;extended<2;++extended)for(unsigned id=0;id<156;++id)for(unsigned flags=0;flags<4;++flags)for(unsigned sample=0;sample<4;++sample){
        ready(owner,extended);unsigned *bank=swork.pip,capacity=extended?10:8;
        bank[0]=2;bank[1]=41u<<16|1;for(unsigned i=12;i<16;++i)bank[i]=0xdead0000u+i;
        unsigned value=0x5a5a,t=type(id);if((t&0x1800)==0x800)value=(unsigned[]){0,1,99,999}[sample];
        if((t&0x1800)==0x1000)value=(unsigned[]){0,1,itemdata[id].max/2,itemdata[id].max}[sample];
        unsigned packed=id?(id<<16)|value|(sample&1?0x80000000u:0)|(sample&2?0x10000000u:0):0;
        if(id&&sample==3&&id%3==0)packed=(packed&~65535u)|65535u|0x08000000;
        unsigned slot=2+id%10;bank[slot]=packed;bank[2+(slot-1)%10]=2u<<16|300;bank[2+slot%10]=20u<<16|1;
        swork.flgchk=flags<<4;
        unsigned expected[16];memcpy(expected,bank,sizeof(expected));Entry entries[10];unsigned wide=0;
        for(unsigned i=0;i<10;++i){unsigned word=expected[i+2],item=(word>>16)&255;entries[i]=(Entry){word,i,item?((type(item)&0x100)?0:1):2};wide+=item&&((type(item)&0x100)!=0);}
        qsort(entries,10,sizeof(*entries),order);
        for(unsigned i=0;i<10;++i)expected[i+2]=entries[i].key==2?0:mark(entries[i].word,flags<<4);
        unsigned all[384];memcpy(all,sys->itm,sizeof(all));memcpy(all+owner*16,expected,sizeof(expected));
        good(rx_inventory_status_build_items());check(!memcmp(all,sys->itm,sizeof(all)),"original stable partition/flags changed adjacent words or another bank");
        size_t count;const RxInventorySpriteGroup *groups=rx_inventory_status_groups(&count);
        check(groups[13].end==capacity+2-wide,"original physical icon count does not match logical wide slots");
        unsigned digits[64],n=0;for(unsigned i=1;i<=capacity+1;++i)n=append_digits(expected[i],digits,n);n=append_digits(expected[expected[0]],digits,n);
        check(groups[14].end==n,"original item/equipped quantity glyph count");for(unsigned i=0;i<n;++i)check((unsigned char)groups[14].parts[i].anim==digits[i],"source quantity sequence differs from independent decimal formatting");
        check(!memcmp(parts_18,parts_18+expected[0],sizeof(*parts_18)),"equipped icon is not the original selected animation");
        check(groups[4].parts[6].pos[1]==(extended?208:160),"original side-pack extension");
        good(rx_inventory_status_build_items());check(!memcmp(all,sys->itm,sizeof(all)),"repeated source sorting changed inventory");
        ++cases;
    }
    printf("PASS %u original item-display builds across four high-address banks, 156 IDs, eight/ten-cell capacities, four packed-flag policies and quantity boundaries; stable 32-bit sorting, glyph sequences, equipment and adjacent bank/tail preservation\n",cases);
    const unsigned fixtures[][4]={{20,4,102,72},{30,4,102,48},{63,4,170,144},{136,4,136,168},{142,3,34,216},{145,3,204,216}};
    for(unsigned i=0;i<6;++i){ready(0,0);swork.pip[2]=fixtures[i][0]<<16|1;good(rx_inventory_status_build_items());check(parts_18[2].texid==fixtures[i][1]&&parts_18[2].u1==fixtures[i][2]&&parts_18[2].v1==fixtures[i][3],"original aliased item icon atlas fixture");}
    ready(0,1);swork.pip[11]=255u<<16;reject();swork.pip[11]=0;swork.pip[0]=12;reject();swork.pip[0]=0;swork.pip[2]=12u<<16|65535;reject();swork.pip[2]=2u<<16|301;reject();
    for(unsigned i=2;i<12;++i)swork.pip[i]=2u<<16|300;reject();
    for(unsigned i=1;i<12;++i)swork.pip[i]=143u<<16|100;reject();
    ready(0,0);swork.subcsr=3;reject();swork.subcsr=0;swork.mode=2;reject();swork.mode=1;sys->cb_flg=0x20000;reject();sys->cb_flg=0;
    puts("PASS six original atlas alias fixtures; invalid IDs/cursors/cell totals/quantities/glyph capacity and unported modes reject before inventory or sprite writes");
    ready(0,1);swork.pip[0]=2;swork.pip[1]=41u<<16|1;swork.pip[2]=12u<<16|99;swork.pip[3]=2u<<16|300;swork.pip[4]=20u<<16|1;
    gs_reset();unsigned bytes;unsigned char *data=rx_boot_read_member(4,145,&bytes);check(data!=NULL,"status texture package");
    /* Resource loading precedes entry, as in original system task 5. */
    rx_inventory_status_reset();sys->memp=cursor;good(rx_inventory_status_load_resources(data,bytes));syFree(data);
    int entered;sys->ts_flg|=0x200;good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());sys->gm_flg|=0x8000000;
    for(unsigned tick=0;tick<16;++tick)good(rx_inventory_sprite_tick());good(rx_inventory_status_build_items());
    RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"Quest inventory EGL");glViewport(0,0,640,480);RxParticleDraw *draw=NULL;RxInventorySpriteFrame frame={0};
    size_t count;const RxInventorySpriteGroup *groups=rx_inventory_status_groups(&count);unsigned rendered=0;
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    for(unsigned group=13;group<=14;++group)for(unsigned i=0;i<groups[group].end;++i){good(rx_inventory_sprite_collect(group,i,&frame));good(rx_inventory_sprite_present(&draw,&frame));rendered+=frame.count;}
    check(rendered>15&&glGetError()==GL_NO_ERROR,"dynamic icon/quantity sprites failed on Quest GPU");
    printf("PASS %u source-built dynamic icon/equipment/quantity polygons on Quest GLES with bounded native table publication\n",rendered);
    rx_inventory_status_reset();check(groups[13].end==0&&groups[14].end==0&&groups[13].parts[0].anim==-1&&groups[14].parts[0].anim==-1,"dynamic views survived reset");
    rx_inventory_sprite_destroy(&frame);rx_particle_close(&draw);rx_egl_close(&egl);check(!opening.events_ready,"full runtime unexpectedly enabled");rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
