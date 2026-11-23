#include "inventory_status.h"
#include "player_control_runtime.h"
#include "enemy01_work.h"
#include "enemy01_parts.h"
#include "main.h"
#include "sub1.h"
#include "item.h"
#include "fileview.h"
#include "room_engine.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_dummy.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* These are native source objects, never overlays of retail pointer words.
   This component owns entry/initialization only, not the StatusMain loop. */
extern S_WORK swork;
extern SITEM sitem;
/* Original file-screen work; the file task itself is not admitted yet. */
FV_WORK fvwork;
#include "inventory_status_tables.inc"
static unsigned phase;
static unsigned char *checkpoint,*scratch,*after;
static unsigned char *package,*previous_subtx;
static NJS_TEXLIST *previous_list;
static unsigned previous_texno;
static NJS_TEXNAME resource_names[5];
static const char *context_check(void);
_Static_assert(sizeof(PARTS)==48,"Original pointer-free sprite layout changed");

const RxInventorySpriteGroup *rx_inventory_status_groups(size_t *count)
{if(count)*count=sizeof(groups)/sizeof(*groups);return groups;}
const S_WORK *rx_inventory_status_work(void){return &swork;}
void rx_inventory_status_reset(void)
{
    if(package){
        for(unsigned i=0;i<5;++i){unsigned char bank;
            if(njRoomTextureBank(resource_names+i,&bank)){
                NJS_TEXLIST owned={resource_names+i,1};njReleaseTexture(&owned);
            }
        }
        Ps2TextureGarbageCollectionAll();
        PopPalletBuffer();sys->gm_flg|=4;
        sys->subtxp=previous_subtx;
        Ps2_current_texlist=previous_list;Ps2_current_texno=previous_texno;
        free(package);package=NULL;
    }
    memset(&swork,0,sizeof(swork));memset(&sitem,0,sizeof(sitem));
    memset(parts_14b,0,sizeof(parts_14b));memset(parts_19b,0,sizeof(parts_19b));
    parts_14b[0].anim=parts_19b[0].anim=-1;
    groups[13].end=groups[14].end=0;
    swork.subtx_list.textures=swork.subtx;
    phase=0;checkpoint=scratch=after=NULL;
}
static const char *tables_check(void)
{
    for(unsigned i=0;i<sizeof(groups)/sizeof(*groups);++i){
        const RxInventorySpriteGroup *g=&groups[i];
        if(i<15&&sprset[i]!=g->parts)return "Foreign inventory sprite table";
        for(size_t j=0;j<=g->end;++j)
            if((g->parts[j].anim==-1)!=(j==g->end))return "Invalid inventory sprite sentinel";
    }
    return NULL;
}
static const char *context_check(void)
{
    const char *error=rx_player_control_check();if(error)return error;
    if((unsigned)sys->ply_id>=4||swork.listcsr_0>=16)return "Invalid inventory player/cursor";
    if(swork.subtx_list.textures!=swork.subtx||swork.subtx_list.nbTexture!=(package?5:0))
        return "Inventory resources are not owned by the initializer";
    if(package&&sys->subtxp!=package)return "Foreign inventory texture stream";
    if(package&&memcmp(swork.subtx,resource_names,sizeof(resource_names)))return "Foreign inventory atlas views";
    return tables_check();
}
static unsigned word(const unsigned char *p)
{return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static int inventory_pictures(const unsigned char *data,size_t bytes)
{
    if(!rx_engine_validate_palette_pictures(data,bytes,5))return 0;
    unsigned images=0;
    while(word(data)!=UINT32_MAX){
        const unsigned char *p=data+32;unsigned n=word(data+4);
        if(word(data)==0x00494c50){
            if(n<4)return 0;
            unsigned palettes=word(p);if(!palettes)return 0;
            for(unsigned i=0;i<palettes;++i)if(p[8+i*8]!=(i?1:0))return 0;
        }else{
            unsigned dimension=images?512:256;
            if(images>=5||p[0x93]!=(images?5:4)||p[0x90]!=(images?7:5)||
               (p[0x94]|(unsigned)p[0x95]<<8)!=dimension||(p[0x96]|(unsigned)p[0x97]<<8)!=dimension)return 0;
            ++images;
        }
        data+=32+n;
    }
    return images==5;
}
const char *rx_inventory_status_load_resources(const void *data,size_t bytes)
{
    const char *error=context_check();if(error)return error;
    if(phase||package)return "Inventory resources are already in use";
    if(!data||!bytes||!inventory_pictures(data,bytes))
        return "Invalid original inventory texture stream";
    extern unsigned Ps2_texmemlist_num;
    unsigned available=0;
    if(!Ps2_tex_info||Ps2_texmemlist_num>256||ring_check()!=1)return "Invalid native inventory texture heap";
    for(unsigned i=0;i<Ps2_texmemlist_num;++i)available+=Ps2_tex_info[i].globalIndex==UINT32_MAX;
    if(available<5)return "Insufficient original inventory texture slots";
    extern void *Ps2_now_free,*Ps2_free_last;
    uintptr_t first=(uintptr_t)Ps2_now_free,last=(uintptr_t)Ps2_free_last;
    size_t required=0;const unsigned char *packet=data;
    while(word(packet)!=UINT32_MAX){
        if(word(packet)==0x324d4954)required+=(size_t)word(packet+32+0x80)+128;
        packet+=32+word(packet+4);
    }
    if(!first||last<first||required>=last-first)return "Insufficient native inventory texture memory";
    unsigned char *copy=malloc(bytes);if(!copy)return "Could not own inventory texture stream";
    memcpy(copy,data,bytes);
    previous_subtx=sys->subtxp;previous_list=Ps2_current_texlist;previous_texno=Ps2_current_texno;
    package=copy;sys->subtxp=copy;
    SbsTextureInit();
    memcpy(resource_names,swork.subtx,sizeof(resource_names));
    int valid=swork.subtx_list.nbTexture==5&&ring_check()==1;
    for(unsigned i=0;valid&&i<5;++i){unsigned char bank;valid=njRoomTextureBank(swork.subtx+i,&bank);}
    if(!valid){rx_inventory_status_reset();return "Could not register original inventory atlases";}
    return NULL;
}
const char *rx_inventory_status_begin(int *entered)
{
    if(!entered)return "Missing inventory status entry result";
    const char *error=context_check();if(error)return error;
    if(!(sys->ts_flg&0x200)&&!(swork.subscreenmode&0x40)){*entered=0;return NULL;}
    if(phase)return "Inventory status task is already owned";
    if(!(sys->st_flg&8)||sys->mn_mode0!=5||sys->mn_mode1||sys->mn_mode2||sys->mn_mode3)
        return "Inventory status entry requires the original menu transition";
    if(!rx_enemy_arena_preview(0))return "Inventory status has no native game arena";
    checkpoint=sys->memp;
    StatusTaskBegin();phase=1;*entered=1;return NULL;
}
const char *rx_inventory_status_initialize(void)
{
    const char *error=context_check();if(error)return error;
    if(phase!=1||swork.subp!=checkpoint||sys->memp!=checkpoint)
        return "Inventory initialization lost its entry checkpoint";
    unsigned char *candidate=rx_enemy_arena_preview(0x20000);
    if(!candidate||rx_enemy01_work_overlaps(candidate,0x20000)||rx_enemy01_parts_overlap(candidate,0x20000))
        return "Inventory view workspace is unavailable or overlaps actor work";
    StatusInitPrepare();
    StatusInitNormal(&swork);
    scratch=candidate;after=candidate+0x20000;phase=2;
    return rx_inventory_status_check();
}
const char *rx_inventory_status_check(void)
{
    const char *error=context_check();if(error)return error;
    unsigned box=(sys->cb_flg&0x40000)?((sys->cb_flg&0x80000)?192:(sys->cb_flg&0x100000)?224:64):64;
    if((phase!=2&&phase!=3)||swork.subp!=checkpoint||sys->memp!=after||!rx_enemy_arena_preview(0)||
       sitem.keep!=scratch||sitem.keepbackup!=scratch||swork.pip!=sys->itm+sys->ply_id*16||
       swork.bxp!=sys->itm+box||swork.subscreenmode!=2||swork.flgtest!=1)
        return "Inventory initial-state ownership is invalid";
    return NULL;
}
const char *rx_inventory_status_finish_setup(void)
{
    const char *error=rx_inventory_status_check();if(error)return error;
    if(phase!=2)return "Inventory setup is already complete";
    if(((swork.pip[2]>>16)&255)>=156)return "Inventory cursor item exceeds original item data";
    extern NJS_TEXANIM *testset[23],parts_15[8];
    if(testset[15]!=parts_15)return "Foreign inventory cursor animation";
    StatusInitFinish(&swork);phase=3;return NULL;
}
static const char *quantity_cost(unsigned packed,unsigned *cost)
{
    unsigned id=(packed>>16)&255,type=itemdata[id].type,value=packed&65535;
    *cost=0;if(!(type&0x3800))return NULL;
    if(packed&0x08000000){*cost=1;return NULL;}
    if((type&0x1800)==0x800){if(value>999)return "Inventory quantity exceeds original digit atlas";}
    else if((type&0x1800)==0x1000){
        unsigned divisor=itemdata[id].max/100;
        if(!divisor||value>itemdata[id].max)return "Invalid original percentage quantity";
        value/=divisor;if((packed&65535)&&!value)value=1;
        *cost=1;
    }else return "Unsupported original quantity format";
    *cost+=value>=100?3:value>=10?2:1;return NULL;
}
const char *rx_inventory_status_build_items(void)
{
    const char *error=rx_inventory_status_check();if(error)return error;
    if(phase!=3||swork.subcsr||sys->cb_flg&(0x1e0010)||swork.mode!=1)
        return "Item display requires normal inventory selection";
    unsigned capacity=(sys->gm_flg&0x8000000)?10:8;
    if(swork.pip[0]>capacity+1)return "Invalid original equipped-item cursor";
    /* Predict only bounds for the original constructors, before any writes. */
    unsigned sorted[12]={swork.pip[0],swork.pip[1]},at=2,cells=0;
    for(unsigned i=1;i<12;++i)if(((swork.pip[i]>>16)&255)>=156)return "Invalid inventory item ID";
    cells=(itemdata[(swork.pip[1]>>16)&255].type&0x100)?2:1;
    for(unsigned wide=1;;--wide){
        for(unsigned i=2;i<12;++i){unsigned packed=swork.pip[i],id=(packed>>16)&255,type=itemdata[id].type;
            if(!id||((type&0x100)!=0)!=wide)continue;
            if((wide&&(swork.flgchk&0x10))||(!wide&&(((type&0x11)&&(swork.flgchk&0x10))||((type&2)&&(swork.flgchk&0x20)))))packed|=0x08000000;
            sorted[at++]=packed;cells+=wide?2:1;
        }
        if(!wide)break;
    }
    if(cells>capacity+1)return "Items exceed original inventory cell capacity";
    unsigned digits=0,cost;
    for(unsigned i=1;i<=capacity+1;++i){error=quantity_cost(sorted[i],&cost);if(error)return error;digits+=cost;}
    error=quantity_cost(sorted[sorted[0]],&cost);if(error)return error;digits+=cost;
    if(digits>=sizeof(parts_19b)/sizeof(*parts_19b))return "Inventory quantity sprites exceed original work";
    ItemSort();StatusItemSprites(&swork);
    size_t icons=0,numbers=0;
    while(icons<16&&parts_14b[icons].anim!=-1)++icons;
    while(numbers<40&&parts_19b[numbers].anim!=-1)++numbers;
    if(icons>=16||numbers>=40)return "Original item constructors exceeded checked bounds";
    groups[13].end=icons;groups[14].end=numbers;
    return rx_inventory_status_check();
}

const char *rx_inventory_status_cursor_tick(void)
{
    const char *error=rx_inventory_status_check();if(error)return error;
    unsigned capacity=(sys->gm_flg&0x8000000)?10:8;
    if(phase!=3||swork.mode!=1||swork.subcsr||sys->cb_flg&0x1e0010||swork.statusflg&0x10||
       swork.listcsr_0<1||swork.listcsr_0>capacity+1||swork.listcsr_2<1||swork.listcsr_2>capacity+1||
       ((swork.pip[swork.listcsr_0]>>16)&255)>=156||swork.colorcount>2||swork.colorflg>1||
       !isfinite(swork.color00)||swork.color00<0.6f||swork.color00>1)
        return "Invalid original normal inventory cursor state";
    extern NJS_TEXANIM *testset[23],parts_15[8];
    if(testset[15]!=parts_15)return "Foreign inventory cursor animation";
    CursorSet(&swork);return NULL;
}
const char *rx_inventory_status_collect_name(RxMessageFrame *out)
{
    if(!out)return "Missing inventory item-name frame";
    const char *error=rx_inventory_status_check();if(error)return error;
    unsigned capacity=(sys->gm_flg&0x8000000)?10:8;
    if(phase!=3||swork.mode!=1||swork.subcsr||sys->cb_flg&0x1e0010||swork.listcsr_0<1||swork.listcsr_0>capacity+1)
        return "Item name requires a normal inventory selection";
    unsigned selected=(swork.pip[swork.listcsr_0]>>16)&255;
    if(selected>=156||swork.itemid!=((parts_09b[0].atr&0x20)?selected:0))return "Item-name selection has not reached the source cursor";
    if(!(swork.statusflg&0x100000)||!swork.itemid){memset(out,0,sizeof(*out));return NULL;}
    NJS_POINT2 pos;StatusItemNamePosition(&pos);
    return rx_message_collect_item_name(StatusItemNameId(&swork),pos.x,pos.y,out);
}
const char *rx_inventory_status_input_tick(void)
{
    const char *error=rx_inventory_status_check();if(error)return error;
    if(phase!=3||swork.sspb!=sys->memp||swork.subcsr||sys->cb_flg&0x1e0010||
       (swork.mode!=1&&swork.mode!=2)||swork.statusflg&0x10||swork.keywait>5||swork.boxwait>2)
        return "Unsupported original inventory input state";
    for(unsigned i=0;i<12;++i)for(unsigned j=0;j<6;++j)
        if(!isfinite(cen_pos[i][j])||fabsf(cen_pos[i][j])>1024)return "Invalid inventory menu transition layout";
    if(swork.mode==1){
        unsigned capacity=(sys->gm_flg&0x8000000)?10:8,wide=0,cells=0;int ordinary=0,empty=0;
        if(swork.testmode>1)return "Unsupported original item-selection phase";
        for(unsigned i=1;i<12;++i)if(((swork.pip[i]>>16)&255)>=156)return "Invalid navigation item ID";
        if(itemdata[(swork.pip[1]>>16)&255].type&0x100)return "Wide personal-slot navigation is not admitted";
        for(unsigned i=2;i<12;++i){unsigned id=(swork.pip[i]>>16)&255,type=itemdata[id].type;
            if(!id){empty=1;continue;}
            if(empty||((type&0x100)&&ordinary))return "Navigation requires original sorted item words";
            if(type&0x100){++wide;cells+=2;}else{ordinary=1;++cells;}
        }
        if(cells>capacity||StatusWideBoundary()!=wide+2)return "Navigation lost its original wide-item boundary";
        if(swork.testmode==1){unsigned physical=swork.listcsr_0,logical=swork.listcsr_2;
            if(physical<1||physical>capacity+1||logical<1||logical>capacity+1)return "Inventory cursor exceeds owned grid";
            if(physical==1){if(logical!=1)return "Invalid personal-slot cursor mapping";}
            else if(physical<wide+2){unsigned start=2*(physical-2)+2;if(logical!=start&&logical!=start+1)return "Invalid wide-item cursor mapping";}
            else if(logical!=physical+wide)return "Invalid ordinary-item cursor mapping";
        }
        extern NJS_TEXANIM *testset[23],parts_15[8];
        if(testset[15]!=parts_15)return "Foreign navigation cursor animation";
        CursorMove(&swork);
    }else{
        if(swork.maincsr>=4||swork.testmode>2||(unsigned)swork.number>=3||swork.wait<0||swork.wait>4)
            return "Invalid original top-menu phase";
        const unsigned help[]={177,178,179,180,199};
        for(unsigned i=0;i<5;++i){error=rx_message_system_check(help[i]);if(error)return error;}
        MainCommand(&swork);
    }
    return NULL;
}

const char *rx_inventory_status_visibility(void)
{
    const char *error=rx_inventory_status_check();if(error)return error;
    if(phase!=3||swork.sspb!=sys->memp||swork.subcsr||sys->cb_flg&0x1e0010||
       (swork.mode!=1&&swork.mode!=2))
        return "Visibility requires the owned normal inventory screen";
    /* check() proves the complete 128-word source box scan belongs to sys.
       context_check() proves the portrait sentinel before the original loop. */
    StatusSpriteVisibility(&swork);return NULL;
}
