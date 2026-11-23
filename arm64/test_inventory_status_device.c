#include "opening.h"
#include "boot_disc.h"
#include "inventory_status.h"
#include "sub1.h"
#include "event_audio.h"
#include "item.h"
#include "ps2_dummy.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_sg_maloc.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
extern S_WORK swork;
extern SITEM sitem;
extern PULSE_WORK pulsework;
extern PARTS *sprset[15];
extern NJS_TEXANIM parts_15[8],*testset[23];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL inventory status: %s\n",why);exit(1);}}
static void good(const char *error){check(!error,error);}
static const RxInventorySpriteGroup *groups;
static unsigned char saved[16][40*sizeof(PARTS)];
static size_t group_count;
static unsigned char *cursor;
static void restore(void){
    rx_inventory_status_reset();sys->memp=cursor;
    /* Independent cold-entry cases do not run the per-frame audio consumer. */
    rx_sound_queue_reset();
    for(unsigned i=0;i<group_count;++i)memcpy(groups[i].parts,saved[i],groups[i].count*sizeof(PARTS));
    sys->st_flg=8;sys->mn_mode0=5;sys->mn_mode1=sys->mn_mode2=sys->mn_mode3=0;
    sys->ts_flg=0x1200;sys->cb_flg=0;sys->ply_id=0;
}
static void rejected_init(void){
    S_WORK before=swork;SITEM item=sitem;PULSE_WORK pulse=pulsework;unsigned char *before_cursor=sys->memp;
    unsigned char sprites[16][40*sizeof(PARTS)];
    for(unsigned i=0;i<group_count;++i)memcpy(sprites[i],groups[i].parts,groups[i].count*sizeof(PARTS));
    check(rx_inventory_status_initialize()!=NULL,"malformed initialization admitted");
    check(!memcmp(&before,&swork,sizeof(before))&&!memcmp(&item,&sitem,sizeof(item))&&!memcmp(&pulse,&pulsework,sizeof(pulse))&&sys->memp==before_cursor,"rejected initialization changed native state");
    for(unsigned i=0;i<group_count;++i)check(!memcmp(sprites[i],groups[i].parts,groups[i].count*sizeof(PARTS)),"rejected initialization changed sprites");
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");
    RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
    good(rx_android_mount_disc(&disc,argv[1]));good(rx_android_start_disc(&disc,&boot));good(rx_opening_start(&opening,0));
    SYS_WORK *system=malloc(sizeof(*system));check(system!=NULL,"system snapshot");memcpy(system,sys,sizeof(*system));BH_PWORK player=*plp;
    groups=rx_inventory_status_groups(&group_count);check(group_count==16,"source sprite group count");
    for(unsigned i=0;i<group_count;++i){check(groups[i].count<=40&&groups[i].end<groups[i].count,"source table extent");memcpy(saved[i],groups[i].parts,groups[i].count*sizeof(PARTS));}
    cursor=sys->memp;check((uintptr_t)cursor>UINT32_MAX,"test must exercise native pointers above 4GiB");
    unsigned items[256];memcpy(items,sys->itm,sizeof(items));
    const int health[]={-1,0,29,30,59,60,119,120,200};
    const unsigned flags[]={0,0x40000,0xc0000,0x140000,0x1c0000};
    const unsigned colors[]={0xff80ff80,0xadad00,0xd64200,0xad0000,0xbd008c};
    const float xs[5][5]={{34,89,144,216,0},{34,89,144,216,0},{23,82,141,216,0},{34,74,118,216,0},{23,113,183,216,0}};
    unsigned cases=0;
    for(unsigned owner=0;owner<4;++owner)for(unsigned slot=0;slot<16;++slot)for(unsigned h=0;h<9;++h)for(unsigned poison=0;poison<3;++poison)for(unsigned box=0;box<5;++box){
        restore();sys->ply_id=owner;sys->cb_flg=flags[box];plp->hp=health[h];plp->stflg=poison==1?0x80000:poison==2?0x200000:0;
        swork.listcsr_0=slot;swork.subscreenmode=(cases&1)?0x40:0;swork.statusflg=0xffffffff;swork.maincsr=7;
        for(unsigned i=0;i<256;++i)sys->itm[i]=((i%254+1)<<16)|(i+1);
        unsigned bank[256];memcpy(bank,sys->itm,sizeof(bank));
        int entered=-1;good(rx_inventory_status_begin(&entered));check(entered==1&&swork.subp==cursor,"original entry checkpoint");
        check(sys->ts_flg==0x1080&&swork.scr.dist==500&&swork.scr.w==352&&swork.scr.h==184&&swork.scr.cx==235&&swork.scr.cy==224&&sys->pad_ps==0,"source task/screen entry");
        unsigned char *allocation=(unsigned char*)(((uintptr_t)cursor+63)&~(uintptr_t)63);
        memset(allocation,0xa5,0x20000);good(rx_inventory_status_initialize());good(rx_inventory_status_check());
        unsigned hp=poison?4:health[h]>=120?0:health[h]>=60?1:health[h]>=30?2:3;
        unsigned offset=box==2||box==4?192:box==3?224:64;
        check(swork.pip==sys->itm+owner*16&&swork.bxp==sys->itm+offset&&swork.itemid==((bank[owner*16+slot]>>16)&255),"native item/box bank or selected item");
        check(swork.hp==hp&&swork.abc==hp&&swork.mode==((cases&1)?2:1)&&swork.maincsr==((cases&1)?2:7)&&swork.subscreenmode==2&&swork.flgtest==1,"health/poison or normal/reentry branch");
        check(swork.open1==23&&swork.open2==46&&swork.wait==1&&swork.subcsr==0&&swork.clip[0].x==32&&swork.clip[0].y==28&&swork.clip[1].x==384&&swork.clip[1].y==188,"source cursor and clipping setup");
        check(swork.statusflg==(((0xffffffffu&~(0x10u|0x8000u|0x80000u))&0xe014f0c0u)|0x10a280u),"source status flag mask");
        check(sitem.keep==allocation&&sitem.keepbackup==allocation&&sitem.mw.flg==0&&sys->memp==allocation+0x20000,"source workspace allocation");
        for(unsigned i=0;i<0x20000;++i)check(allocation[i]==0,"source workspace zeroing");
        check(!memcmp(bank,sys->itm,sizeof(bank)),"initialization mutated item quantities");
        for(unsigned i=0;i<14;++i)for(unsigned j=0;j<groups[i].end;++j)check(groups[i].parts[j].atr&0x20,"source sprite enable loop");
        for(unsigned i=0;i<5;++i){check(groups[10].parts[i].anim==owner+4*i,"source character portrait selection");check(!(groups[15].parts[i].atr&0x20),"source auxiliary sprite flag");check(pulsework.pos_02[i].x==xs[hp][i]&&pulsework.pos_02[i].y==0&&pulsework.ocol01[i]==colors[hp]&&pulsework.ocol00[i]==(hp==0&&i>0&&i<4?0xff00ff00u:0),"original pulse shape/color");check(pulsework.col00[i].color==0xffffffffu&&pulsework.col01[i].color==0xffffffffu&&pulsework.pos_00[i].x==0&&pulsework.pos_01[i].y==0,"original pulse reset");}
        check(pulsework.pulseflg==1&&pulsework.fadeflg==1&&pulsework.count==0,"original pulse flags");
        sys->gm_mode=cases%4;unsigned old_capacity=cases&2?0x8000000:0;
        sys->gm_flg=(sys->gm_flg&~0x8000000)|old_capacity;
        /* Original event bits count from the most-significant bit. */
        sys->ev_flg[0]=cases&1?0x03800000:0;
        good(rx_inventory_status_finish_setup());good(rx_inventory_status_check());
        unsigned capacity=sys->gm_mode==3?0x8000000:owner==3?old_capacity:cases&1?0x8000000:0;
        check((sys->gm_flg&0x8000000)==capacity&&swork.sspb==sys->memp&&(swork.statusflg&0x40),"source capacity event flags or final workspace checkpoint");
        check(swork.listcsr_0==2&&swork.listcsr_2==2&&swork.color00==0.6f&&swork.colorcount==0&&swork.colorflg==1,"source initial cursor state");
        check(parts_15[0].sx==((itemdata[(bank[owner*16+2]>>16)&255].type&0x100)?144:76),"source wide-item cursor dimensions");
        check(groups[9].parts[0].pos[0]==20&&groups[9].parts[0].pos[1]==4&&((groups[9].parts[0].atr&0x20)!=0)==(swork.mode==1),"source cursor position/visibility");
        for(unsigned i=0;i<12;++i){for(unsigned j=0;j<6;++j)check(cen_pos[i][j]==cen_pos99[i][j],"source layout copy");check(cen_pos[i][4]==cen_pos[i][2]-cen_pos[i][0]&&cen_pos[i][5]==cen_pos[i][3]-cen_pos[i][1],"source layout dimensions");}
        for(unsigned i=0;i<6;++i)check(groups[7].parts[i].col.r==0&&groups[7].parts[i].col.g==0&&groups[7].parts[i].col.b==0,"source background color reset");
        ++cases;
    }
    printf("PASS %u original status initializations: four native item banks, sixteen cursors, health boundaries/poison, five box contexts, normal/reentry modes, source sprite tables and health pulse; 128KiB source workspace zeroed above 4GiB\n",cases);
    for(unsigned id=0;id<156;++id){restore();sys->itm[2]=id<<16;int selected;good(rx_inventory_status_begin(&selected));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());check(parts_15[0].sx==((itemdata[id].type&0x100)?144:76),"original item metadata cursor width");}
    puts("PASS all 156 original item metadata entries through native CursorInit");
    restore();int entered;good(rx_inventory_status_begin(&entered));swork.listcsr_0=16;rejected_init();swork.listcsr_0=0;sys->ply_id=4;rejected_init();sys->ply_id=-1;rejected_init();sys->ply_id=0;swork.subtx_list.textures=(void*)(uintptr_t)1;rejected_init();swork.subtx_list.textures=swork.subtx;swork.subtx_list.nbTexture=1;rejected_init();swork.subtx_list.nbTexture=0;
    PARTS *root=sprset[0];sprset[0]=(void*)(uintptr_t)1;rejected_init();sprset[0]=root;
    char sentinel=groups[0].parts[groups[0].end].anim;groups[0].parts[groups[0].end].anim=0;rejected_init();groups[0].parts[groups[0].end].anim=sentinel;
    char first=groups[0].parts[0].anim;groups[0].parts[0].anim=-1;rejected_init();groups[0].parts[0].anim=first;
    swork.subp=cursor+1;rejected_init();swork.subp=cursor;sys->memp=cursor+1;rejected_init();sys->memp=cursor;
    unsigned char *end=sys->endp;sys->endp=cursor+64;rejected_init();sys->endp=end;
    unsigned char *work=plp->exp0;plp->exp0=(void*)(uintptr_t)1;rejected_init();plp->exp0=work;
    good(rx_inventory_status_initialize());rejected_init();sitem.keep=(void*)(uintptr_t)1;check(rx_inventory_status_check()!=NULL,"foreign workspace admitted");
    restore();sys->ts_flg=0;entered=99;good(rx_inventory_status_begin(&entered));check(entered==0,"no-request entry");sys->ts_flg=0x200;sys->mn_mode0=0;entered=99;check(rx_inventory_status_begin(&entered)!=NULL&&entered==99,"entry without menu handoff");check(rx_inventory_status_begin(NULL)!=NULL,"missing result");
    puts("PASS malformed native player/cursor/texture/table/sentinel/checkpoint/arena/workspace and duplicate initialization guards; source tables and state preserved on rejection");
    restore();good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());
    unsigned previous_item=swork.pip[2];swork.pip[2]=156u<<16;
    S_WORK before_finish=swork;check(rx_inventory_status_finish_setup()!=NULL&&!memcmp(&before_finish,&swork,sizeof(swork)),"invalid item metadata advanced setup");swork.pip[2]=previous_item;
    NJS_TEXANIM *previous_anim=testset[15];testset[15]=(void*)(uintptr_t)1;check(rx_inventory_status_finish_setup()!=NULL&&!memcmp(&before_finish,&swork,sizeof(swork)),"foreign cursor animation advanced setup");testset[15]=previous_anim;good(rx_inventory_status_finish_setup());before_finish=swork;check(rx_inventory_status_finish_setup()!=NULL&&!memcmp(&before_finish,&swork,sizeof(swork)),"duplicate setup advanced state");
    restore();unsigned package_bytes;unsigned char *package=rx_boot_read_member(4,145,&package_bytes);check(package&&package_bytes==1088048,"read original disc-selected ITEM archive member 145");
    S_WORK empty=swork;unsigned palette[1024];memcpy(palette,palbuf,sizeof(palette));
    for(size_t n=0;n<package_bytes;++n)check(rx_inventory_status_load_resources(package,n)!=NULL&&!memcmp(&empty,&swork,sizeof(empty))&&!memcmp(palette,palbuf,sizeof(palette)),"truncated PVP mutated status/palette");
    unsigned char *original=malloc(package_bytes);check(original!=NULL,"immutable package snapshot");memcpy(original,package,package_bytes);
    unsigned char flag=package[40],second_flag=package[48];package[40]=1;package[48]=0;check(rx_inventory_status_load_resources(package,package_bytes)!=NULL&&!memcmp(&empty,&swork,sizeof(empty)),"unsupported palette ordering admitted");package[40]=flag;package[48]=second_flag;
    unsigned char width=package[92+32+0x94];package[92+32+0x94]=1;check(rx_inventory_status_load_resources(package,package_bytes)!=NULL&&!memcmp(&empty,&swork,sizeof(empty)),"unsupported atlas dimensions admitted");package[92+32+0x94]=width;
    extern void *Ps2_free_last,*Ps2_now_free;void *heap_end=Ps2_free_last;Ps2_free_last=(unsigned char*)Ps2_now_free+1;check(rx_inventory_status_load_resources(package,package_bytes)!=NULL&&!memcmp(&empty,&swork,sizeof(empty)),"exhausted texture heap admitted");Ps2_free_last=heap_end;
    extern unsigned Ps2_texmemlist_num;unsigned slots=Ps2_texmemlist_num;Ps2_texmemlist_num=0;check(rx_inventory_status_load_resources(package,package_bytes)!=NULL&&!memcmp(&empty,&swork,sizeof(empty)),"exhausted texture slots admitted");Ps2_texmemlist_num=slots;
    for(unsigned cycle=0;cycle<4;++cycle){
        NJS_TEXLIST *selected=Ps2_current_texlist;unsigned texno=Ps2_current_texno;unsigned char *previous=sys->subtxp;
        good(rx_inventory_status_load_resources(package,package_bytes));check(swork.subtx_list.nbTexture==5&&sys->subtxp!=package&&!memcmp(original,package,package_bytes)&&ring_check()==1,"owned resource registration mutated disc bytes or heap");
        for(unsigned i=0;i<5;++i){unsigned char bank;check(njRoomTextureBank(swork.subtx+i,&bank),"original atlas missing native texture work");}
        check(njGetPaletteMode()==6&&!memcmp(palette,palbuf+3072,sizeof(palette)),"original palette push/mode");
        check(rx_inventory_status_load_resources(package,package_bytes)!=NULL,"duplicate resources accepted");
        good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());
        rx_inventory_status_reset();check(ring_check()==1&&njGetPaletteMode()==2&&!memcmp(palette,palbuf,sizeof(palette))&&sys->subtxp==previous&&Ps2_current_texlist==selected&&Ps2_current_texno==texno,"native resource retirement/palette restoration");restore();
    }
    free(original);syFree(package);
    puts("PASS original StatusMain shared setup: layout, item cursor, capacity flags and background; malformed metadata/cursor and duplicate guards");
    puts("PASS original member-145 five-atlas registration and four resource retire/reload cycles; all truncated prefixes reject without palette/status writes; disc bytes immutable and native texture ring/palette restored");
    restore();memcpy(sys,system,sizeof(*system));*plp=player;memcpy(sys->itm,items,sizeof(items));check(!opening.events_ready,"campaign gate unexpectedly enabled");rx_opening_end(&opening);check(!swork.subp&&!swork.pip&&!sitem.keep&&rx_inventory_status_check()!=NULL,"teardown retained arena pointers");
    free(system);rx_android_stop_disc();rx_android_unmount_disc(&disc);puts("PASS status native teardown; live StatusMain/render/actions remain gated");return 0;
}
