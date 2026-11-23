#include "message_runtime.h"
#include "message.h"
#include "main.h"
#include "ps2_dummy.h"
#include "ps2_NaTextureFunction.h"
#include "effect_runtime.h"
#include "gs/gs.h"
#ifdef RECVX_NATIVE_INVENTORY_RENDER
#include "item_names.h"
#endif
#include <stdint.h>
#include <math.h>
#include <string.h>
extern const char *bhNativeMessageData(const unsigned char**,size_t*,unsigned*);
static RxMessageFrame frame,next;
static int collecting;
static const char *capture_error;
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static unsigned u32(const unsigned char *p){return u16(p)|u16(p+2)<<16;}
static const char *message_span(unsigned index,const unsigned char **begin,const unsigned char **end)
{
    const unsigned char *base;size_t bytes;unsigned count;
    const char *error=bhNativeMessageData(&base,&bytes,&count);if(error)return error;
    if(!count||count>256||index>=count||bytes<(size_t)(count+1)*4||u32(base)!=count)return "Invalid original message offset table";
    unsigned offset=u32(base+(index+1)*4);size_t limit=bytes;
    if(offset<(count+1)*4||offset%2||offset>=bytes)return "Original message offset exceeds owned bank";
    for(unsigned i=0;i<count;++i){unsigned at=u32(base+(i+1)*4);if(at>offset&&at<limit)limit=at;}
    if(limit%2)return "Unaligned original message extent";
    *begin=base+offset;*end=base+limit;return NULL;
}
static const char *scan(const unsigned char *p,const unsigned char *end)
{
    unsigned words=0,glyphs=0,lines=0,advance=0;
    while((size_t)(end-p)>=2&&++words<=2048){unsigned code=u16(p);p+=2;
        if(code==0xffff)return NULL;
        if(code==0xff02){if((size_t)(end-p)<2)return "Truncated original message timer";p+=2;continue;}
        if(code==0xff00){advance=0;if(++lines>24)return "Original message exceeds native line bounds";continue;}
        if(code==0xff01){advance+=14;if(advance>1000)return "Original message exceeds native line width";continue;}
        if(code==0xfe00||(code>=0xfe01&&code<=0xfe08))continue;
        if(code>=108)return "Original message item/choice/glyph control is not ported yet";
        if(++glyphs>=RX_MESSAGE_GLYPHS)return "Original message exceeds native glyph capacity";
        advance+=FontSz[code];if(advance>1000)return "Original message exceeds native line width";
    }
    return "Original message has no bounded terminator";
}
static int page_cursor(const unsigned char *begin,const unsigned char *end,uintptr_t cursor)
{
    if(cursor==(uintptr_t)begin)return 1;
    for(unsigned words=0;(size_t)(end-begin)>=2&&words++<2048;){
        unsigned code=u16(begin);begin+=2;if(code==0xffff)return 0;
        if(code==0xff02){if((size_t)(end-begin)<2)return 0;begin+=2;}
        if((code==0xff02||code==0xfe08)&&cursor==(uintptr_t)begin)return 1;
    }
    return 0;
}
const char *rx_message_check(unsigned index)
{
    const unsigned char *begin,*end;const char *error=message_span(index,&begin,&end);return error?error:scan(begin,end);
}
void rx_message_clear(void){frame.count=next.count=0;}
const RxMessageFrame *rx_message_frame(void){return &frame;}
int rx_message_collecting(void){return collecting;}
int rx_message_capture(uint64_t primitive,const void *data,unsigned count)
{
    if(!collecting)return -1;
    if(capture_error)return -1;
    if(primitive!=UINT64_C(0x2e800000000000)||count!=4||!data||Ps2_gs_save.ALPHA!=0x44||next.count>=RX_MESSAGE_GLYPHS){capture_error="Unsupported original font polygon";return -1;}
    uintptr_t texture=sys->ef_tex[0].texaddr;
    if(next.count&&(next.tex0!=gs.ctx[0].tex0||next.texa!=gs.texa||next.texture!=texture)){capture_error="Original font atlas changed during message drawing";return -1;}
    next.tex0=gs.ctx[0].tex0;next.texa=gs.texa;next.texture=texture;
    RxMessagePolygon polygon={0};const unsigned char *p=data;
    for(unsigned i=0;i<4;++i){memcpy(polygon.uv[i],p+i*48,8);memcpy(polygon.color[i],p+i*48+16,16);memcpy(polygon.position[i],p+i*48+32,12);
        for(unsigned j=0;j<3;++j)if(!isfinite(polygon.position[i][j])){capture_error="Nonfinite original font position";return -1;}
        for(unsigned j=0;j<2;++j)if(!isfinite(polygon.uv[i][j])||polygon.uv[i][j]<0||polygon.uv[i][j]>1||polygon.position[i][j]<0||polygon.position[i][j]>4095){capture_error="Original font polygon exceeds native GS bounds";return -1;}
        for(unsigned j=0;j<4;++j)if(polygon.color[i][j]>(j==3?128u:255u)){capture_error="Invalid original font color";return -1;}
    }
    next.polygons[next.count++]=polygon;return 0;
}
#ifdef RECVX_NATIVE_INVENTORY_RENDER
static const char *system_span(unsigned index,const unsigned char **begin,const unsigned char **end)
{
    const unsigned char *base;size_t bytes;unsigned count;
    const char *error=rx_system_messages_data(&base,&bytes,&count);if(error)return error;
    if(index>=count)return "System message index exceeds original bank";
    unsigned offset=u32(base+(index+1)*4);size_t limit=bytes;
    for(unsigned i=0;i<count;++i){unsigned at=u32(base+(i+1)*4);if(at>offset&&at<limit)limit=at;}
    *begin=base+offset;*end=base+limit;return NULL;
}
const char *rx_message_system_check(unsigned index)
{
    const unsigned char *begin,*end;const char *error=system_span(index,&begin,&end);
    return error?error:scan(begin,end);
}
const char *rx_message_collect_item_name(unsigned id,float x,float y,RxMessageFrame *out)
{
    if(!out||collecting)return "Invalid item-name collection request";
    const unsigned char *text;size_t bytes;
    const char *error=rx_item_names_span(id,&text,&bytes);if(error)return error;
    if(!isfinite(x)||!isfinite(y)||fabsf(x)>1024||fabsf(y)>1024||
       !isfinite(FontScaleX)||!isfinite(FontScaleY)||FontScaleX<=0||FontScaleX>2||FontScaleY<=0||FontScaleY>2)
        return "Invalid original item-name position/scale";
    float advance=0;
    for(size_t i=0;i+2<bytes;i+=2){unsigned code=u16(text+i);advance+=FontScaleX*(code==0xff01?14:FontSz[code]);}
    if(x+1728<0||y+1808<0||x+advance+28*FontScaleX+1728>4095||y+28*FontScaleY+1808>4095)
        return "Item-name geometry exceeds GS bounds";
    unsigned char bank;
    if(!Ps2_tex_buff||sys->ef_tlist.textures!=sys->ef_tex||!sys->ef_tlist.nbTexture||sys->ef_tlist.nbTexture>450||!njRoomTextureBank(sys->ef_tex,&bank))
        return "Item name requires its native common font atlas";
    error=rx_effects_sync_palette();if(error)return error;
    NJS_POINT2 pos={x,y};next.count=0;capture_error=NULL;collecting=1;
    bhDispItemName(&pos,(int)id,0,0,-1.0f);collecting=0;
    if(capture_error)return capture_error;
    *out=next;return NULL;
}
#endif

typedef struct {unsigned st,cb,sp,on,ps,old;unsigned int *dp;int ct,tim,fls,sel,seln;} State;
static State save(void){return (State){sys->st_flg,sys->cb_flg,sys->sp_flg,sys->pad_on,sys->pad_ps,sys->pad_old,sys->mes_dp,sys->mes_ct,sys->mes_tim,sys->mes_fls,sys->mes_sel,sys->mes_seln};}
static void restore(State s){sys->st_flg=s.st;sys->cb_flg=s.cb;sys->sp_flg=s.sp;sys->pad_on=s.on;sys->pad_ps=s.ps;sys->pad_old=s.old;sys->mes_dp=s.dp;sys->mes_ct=s.ct;sys->mes_tim=s.tim;sys->mes_fls=s.fls;sys->mes_sel=s.sel;sys->mes_seln=s.seln;}
const char *rx_message_tick(unsigned mode)
{
    if(!(sys->st_flg&0x200)){frame.count=0;return NULL;}
    if(collecting||mode>2||sys->mes_idx<1||sys->mes_ct<0||sys->mes_ct>1048576||sys->mes_tim<0||sys->mes_tim>65535||sys->mes_fls<0||sys->mes_fls>31||sys->st_flg&(0x8000|0x80000))return "Invalid or unsupported original message state";
    const unsigned char *begin,*end;const char *error;
#ifdef RECVX_NATIVE_INVENTORY_RENDER
    if(rx_system_message_current())error=system_span((unsigned)sys->mes_idx-1,&begin,&end);
    else
#endif
    error=message_span((unsigned)sys->mes_idx-1,&begin,&end);
    if(error)return error;
    uintptr_t pointer=(uintptr_t)sys->mes_dp;
    if(pointer<(uintptr_t)begin||pointer>=(uintptr_t)end||pointer%2||!page_cursor(begin,end,pointer))return "Message cursor exceeds its owned native page";
    error=scan((const unsigned char*)pointer,end);if(error)return error;
    if(!isfinite(FontScaleX)||!isfinite(FontScaleY)||!isfinite(FontScaleCR)||FontScaleX<=0||FontScaleX>2||FontScaleY<=0||FontScaleY>2||FontScaleCR<=0||FontScaleCR>2)return "Invalid original message font scale";
    unsigned char bank;
    if(sys->ef_tlist.textures!=sys->ef_tex||!sys->ef_tlist.nbTexture||sys->ef_tlist.nbTexture>450||!njRoomTextureBank(sys->ef_tex,&bank))return "Message requires its native common font atlas";
    error=rx_effects_sync_palette();if(error)return error;
    State state=save();next.count=0;capture_error=NULL;collecting=1;bhControlMessage((int)mode);collecting=0;
    if(capture_error){restore(state);return capture_error;}
    frame=next;return NULL;
}
