#include "inventory_sprite.h"
#include "inventory_status.h"
#include "sub1.h"
#include "main.h"
#include "fileview.h"
#include "ps2_dummy.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_NinjaCnk.h"
#include "effect_runtime.h"
#include "gs/gs.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "inventory_animation_tables.inc"
extern S_WORK swork;
extern char ViewCnt;
static RxInventorySpriteFrame *active;
static const char *capture_error;
static unsigned atlas;
void rx_inventory_sprite_destroy(RxInventorySpriteFrame *frame)
{if(frame){for(unsigned i=0;i<frame->image_count;++i)free(frame->images[i].rgba);memset(frame,0,sizeof(*frame));}}
int rx_inventory_sprite_collecting(void){return active!=NULL;}
int rx_inventory_sprite_capture(uint64_t primitive,const void *data,unsigned count)
{
    if(!active||capture_error)return -1;
    if(primitive!=UINT64_C(0xae000000000000)||count!=4||!data||active->count>=RX_INVENTORY_SPRITE_LIMIT){capture_error="Unsupported original inventory sprite packet";return -1;}
    unsigned char bank;
    if(!njRoomTextureBank(swork.subtx+atlas,&bank)){capture_error="Lost original inventory atlas";return -1;}
    uint64_t tex0=gs.ctx[0].tex0,texa=gs.texa;
    unsigned width=1u<<((tex0>>26)&15),height=1u<<((tex0>>30)&15);
    if(width>512||height>512||!width||!height){capture_error="Inventory sprite atlas exceeds native bounds";return -1;}
    unsigned image=0;
    while(image<active->image_count&&(active->images[image].tex0!=tex0||active->images[image].texa!=texa||active->images[image].bank!=bank))++image;
    if(image==active->image_count){
        if(image>=RX_INVENTORY_SPRITE_LIMIT){capture_error="Inventory atlas snapshot capacity exceeded";return -1;}
        uint32_t *pixels=malloc((size_t)width*height*4);
        if(!pixels){capture_error="Could not snapshot original inventory atlas";return -1;}
        gs_decode_texture(tex0,texa,width,height,pixels);
        active->images[image]=(RxInventorySpriteImage){width,height,bank,tex0,texa,gs.ctx[0].tex1,gs.ctx[0].clamp,pixels};++active->image_count;
    }
    RxInventorySpritePolygon polygon={.image=image};const unsigned char *p=data;
    for(unsigned i=0;i<4;++i){
        int uv[2];memcpy(uv,p+i*48,8);memcpy(polygon.color[i],p+i*48+16,16);memcpy(polygon.position[i],p+i*48+32,12);
        polygon.uv[i][0]=(float)uv[0]/(16*width);polygon.uv[i][1]=(float)uv[1]/(16*height);
        for(unsigned j=0;j<3;++j)if(!isfinite(polygon.position[i][j])){capture_error="Nonfinite inventory sprite position";return -1;}
        for(unsigned j=0;j<2;++j)if(polygon.position[i][j]<0||polygon.position[i][j]>4095||abs(uv[j])>32768){capture_error="Inventory sprite exceeds GS coordinates";return -1;}
        for(unsigned j=0;j<4;++j)if(polygon.color[i][j]>128){capture_error="Inventory sprite exceeds material bounds";return -1;}
    }
    active->polygons[active->count++]=polygon;return 0;
}
const char *rx_inventory_sprite_collect(unsigned group,unsigned index,RxInventorySpriteFrame *out)
{
    if(!out||active)return "Invalid inventory sprite collection request";
    const char *error=rx_inventory_status_check();if(error)return error;
    if(swork.sspb!=sys->memp)return "Inventory sprite setup is incomplete";
    size_t count;const RxInventorySpriteGroup *groups=rx_inventory_status_groups(&count);
    if(group>=count||index>=groups[group].end)return "Inventory sprite exceeds original table";
    const PARTS *part=groups[group].parts+index;
    if(swork.subtx_list.nbTexture!=5||fvwork.tex_flg||sys->cb_flg&0x20000||swork.statusflg&0x800||swork.subscreenmode!=2||ViewCnt)
        return "Inventory sprite requires the owned normal status atlases";
    /* The original dispatch never reads animation/geometry for hidden parts. */
    if(!(part->atr&0x20)){rx_inventory_sprite_destroy(out);return NULL;}
    if(part->parts_num>=23||part->cen_no>=12||part->anim<0||
       testset[part->parts_num]!=animations[part->parts_num].data||(unsigned)part->anim>=animations[part->parts_num].count)
        return "Inventory sprite references foreign animation/layout data";
    const NJS_TEXANIM *animation=animations[part->parts_num].data+part->anim;
    if(animation->texid>=5||animation->sx>1024||animation->sy>1024)return "Invalid inventory atlas animation";
    if(!isfinite(part->move)||fabsf(part->move)>256)return "Invalid inventory sprite repetition";
    for(unsigned i=0;i<3;++i)if(!isfinite(part->pos[i])||fabsf(part->pos[i])>512)return "Invalid inventory sprite position";
    for(unsigned i=0;i<2;++i)if(!isfinite(cen_pos[part->cen_no][i])||fabsf(cen_pos[part->cen_no][i])>1024)return "Invalid inventory sprite center";
    const float color[]={part->col.a,part->col.r,part->col.g,part->col.b};
    for(unsigned i=0;i<4;++i)if(!isfinite(color[i])||color[i]<0||color[i]>1)return "Invalid inventory sprite material";
    RxInventorySpriteFrame next={0};
    error=rx_effects_sync_palette();if(error)return error;
    njSetTexture(&swork.subtx_list);njSetPaletteBankNum(0,5);
    capture_error=NULL;atlas=animation->texid;active=&next;
    StatusDrawSprite(&swork,part);active=NULL;
    if(capture_error){rx_inventory_sprite_destroy(&next);return capture_error;}
    rx_inventory_sprite_destroy(out);*out=next;return NULL;
}
const char *rx_inventory_sprite_tick(void)
{
    const char *error=rx_inventory_status_check();if(error)return error;
    if(swork.sspb!=sys->memp||swork.subtx_list.nbTexture!=5||swork.statusflg&(0x10|4|0x800|0x80000|0x1000000|0x8000000)||
       swork.open1<0||swork.open1>23||swork.open2<0||swork.open2>46)
        return "Unsupported original inventory display transition";
    for(unsigned i=0;i<12;++i)for(unsigned j=0;j<6;++j)
        if(!isfinite(cen_pos[i][j])||fabsf(cen_pos[i][j])>1024)return "Invalid inventory transition layout";
    SpriteH(&swork);SpriteUV(&swork);return NULL;
}

const char *rx_inventory_sprite_fade_tick(void)
{
    const char *error=rx_inventory_status_check();if(error)return error;
    if(swork.sspb!=sys->memp||swork.subcsr||sys->cb_flg&0x1e0010||
       (swork.mode!=1&&swork.mode!=2))
        return "Background fade requires normal inventory display";
    /* Source order is addition then subtraction, including when both flags
       are set. Check each intermediate before invoking either source loop. */
    const PARTS *background=rx_inventory_status_groups(NULL)[7].parts;
    for(unsigned i=0;i<6;++i){
        float colors[]={background[i].col.r,background[i].col.g,background[i].col.b};
        for(unsigned j=0;j<3;++j){
            float value=colors[j];
            if(!isfinite(value)||value<0||value>1)return "Invalid inventory background color";
            if(swork.statusflg&1)value+=0.057142857f;
            if(!isfinite(value)||value<0||value>1)return "Inventory background fade-in exceeds material bounds";
            if(swork.statusflg&2)value-=0.057142857f;
            if(!isfinite(value)||value<0||value>1)return "Inventory background fade-out exceeds material bounds";
        }
    }
    BGFadeIn();BGFadeOut();return NULL;
}
