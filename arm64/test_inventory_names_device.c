#include "opening.h"
#include "boot_disc.h"
#include "inventory_status.h"
#include "inventory_sprite.h"
#include "item_names.h"
#include "event_audio.h"
#include "particle_draw.h"
#include "android_egl.h"
#include "main.h"
#include "sub1.h"
#include "item.h"
#include "message.h"
#include "ps2_sg_maloc.h"
#include "gs/gs.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
extern S_WORK swork;
extern PARTS parts_09b[8];
extern NJS_TEXANIM parts_15[8],*testset[23];
static unsigned char *cursor;
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL inventory names: %s\n",why);exit(1);}}
static void good(const char *error){check(!error,error);}
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static unsigned u32(const unsigned char *p){return u16(p)|u16(p+2)<<16;}
static void ready(unsigned owner,unsigned extended){
    rx_inventory_status_reset();sys->memp=cursor;sys->ply_id=owner;sys->st_flg=8;sys->cb_flg=0;sys->ts_flg=0x1200;rx_sound_queue_reset();
    sys->mn_mode0=5;sys->mn_mode1=sys->mn_mode2=sys->mn_mode3=0;memset(sys->itm+owner*16,0,64);
    int entered;good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());
    if(extended)sys->gm_flg|=0x8000000;else sys->gm_flg&=~0x8000000;
}
static void reject_cursor(void){S_WORK before=swork;PARTS sprites[8];NJS_TEXANIM animations[8];memcpy(sprites,parts_09b,sizeof(sprites));memcpy(animations,parts_15,sizeof(animations));
    check(rx_inventory_status_cursor_tick()!=NULL&&!memcmp(&before,&swork,sizeof(before))&&!memcmp(sprites,parts_09b,sizeof(sprites))&&!memcmp(animations,parts_15,sizeof(animations)),"cursor rejection changed native state");}
static void geometry(const unsigned char *bank,unsigned id,float x,float y,RxMessageFrame *frame){
    const unsigned char *p=bank+u32(bank+(id+1)*4);unsigned glyphs=0;
    while(u16(p)!=0xffff){unsigned code=u16(p);p+=2;if(code==0xff01){x+=14*FontScaleX;continue;}
        check(glyphs<frame->count,"missing original name glyph");const RxMessagePolygon *q=frame->polygons+glyphs++;
        float px[]={x,x+28*FontScaleX,x+28*FontScaleX,x},py[]={y,y,y+28*FontScaleY,y+28*FontScaleY};
        unsigned u=code%18*14,v=code/18*14;float us[]={(u*16+8)/4096.0f,((u+14)*16-8)/4096.0f,((u+14)*16-8)/4096.0f,(u*16+8)/4096.0f};
        float vs[]={(v*16+8)/4096.0f,(v*16+8)/4096.0f,((v+14)*16-8)/4096.0f,((v+14)*16-8)/4096.0f};
        for(unsigned j=0;j<4;++j)check(q->position[j][0]==1728+px[j]&&q->position[j][1]==1808+py[j]&&q->position[j][2]==-1&&q->uv[j][0]==us[j]&&q->uv[j][1]==vs[j]&&q->color[j][0]==127&&q->color[j][1]==127&&q->color[j][2]==127&&q->color[j][3]==128,"original name geometry/UV/material differs from disc reference");
        x+=FontScaleX*FontSz[code];
    }
    check(glyphs==frame->count,"extra name glyphs");
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
    good(rx_android_mount_disc(&disc,argv[1]));good(rx_android_start_disc(&disc,&boot));good(rx_opening_start(&opening,0));cursor=sys->memp;
    unsigned bytes;unsigned char *raw=rx_boot_read_file("SYSMES.ALD",&bytes);check(raw&&bytes>=5456&&u32(raw)==5452&&u32(raw+4)==183,"original system item-name bank");
    check((uintptr_t)sys->mes_ip>UINT32_MAX&&memcmp(raw+4,sys->mes_ip,5452)==0,"names must be owned natively above 4GiB");
    unsigned cases=0;
    for(unsigned owner=0;owner<4;++owner)for(unsigned extended=0;extended<2;++extended){ready(owner,extended);unsigned capacity=extended?10:8;
        for(unsigned id=0;id<156;++id)for(unsigned logical=1;logical<=capacity+1;++logical)for(unsigned secondary=0;secondary<2;++secondary)for(unsigned visible=0;visible<2;++visible){
            unsigned physical=1+(id+logical)%(capacity+1);swork.pip[physical]=id<<16|1;swork.listcsr_0=physical;swork.listcsr_2=logical;
            parts_09b[0].atr=(parts_09b[0].atr&~0x20)|(visible?0x20:0);parts_09b[1].atr=(parts_09b[1].atr&~0x20)|(secondary?0x20:0);
            swork.color00=(float[]){0.6f,0.8f,1}[id%3];swork.colorflg=id&1;swork.colorcount=(id+logical)%3;
            S_WORK expected=swork;float color=swork.color00;unsigned count=swork.colorcount,direction=swork.colorflg;
            if(!visible){color=.6f;count=0;direction=1;}else if(++count>2){count=0;color+=direction?(secondary?.07f:.05f):-(secondary?.07f:.05f);if(color>1){color=1;direction=0;}if(color<.6f){color=.6f;direction=1;}}
            expected.color00=color;expected.colorcount=count;expected.colorflg=direction;expected.itemid=visible?id:0;
            NJS_TEXANIM animations[8];memcpy(animations,parts_15,sizeof(animations));unsigned wide=(itemdata[id].type&0x100)!=0;
            if(secondary)for(unsigned i=1;i<4;++i)animations[i].sx=wide?136:68;else animations[0].sx=wide?144:76;
            unsigned words[16];memcpy(words,swork.pip,sizeof(words));good(rx_inventory_status_cursor_tick());
            check(!memcmp(&expected,&swork,sizeof(expected))&&!memcmp(words,swork.pip,sizeof(words))&&!memcmp(animations,parts_15,sizeof(animations)),"cursor changed native work/item words or wrong animation width");
            unsigned cell=wide?logical&~1u:logical;float x=logical>=2?20+(cell%2)*68:88,y=logical>=2?4+((int)(cell/2)-1)*48:-76;
            if(secondary){x+=4;y+=4;}check(parts_09b[0].pos[0]==x&&parts_09b[0].pos[1]==y,"original cursor placement/alignment");
            if(visible)check(parts_09b[0].col.r==color&&parts_09b[0].col.g==color&&parts_09b[0].col.b==color&&parts_09b[0].col.a==1&&parts_09b[1].col.r==.6f,"original cursor pulse/color");++cases;
        }
    }
    printf("PASS %u original CursorSet states: four native banks, 156 IDs, capacities/positions, wide alignment, both cursor variants, pulse endpoints and hidden reset; item words and unrelated work preserved\n",cases);
    ready(0,0);swork.listcsr_0=16;reject_cursor();swork.listcsr_0=2;swork.listcsr_2=0;reject_cursor();swork.listcsr_2=2;swork.pip[2]=255u<<16;reject_cursor();swork.pip[2]=20u<<16;
    swork.colorcount=3;reject_cursor();swork.colorcount=0;swork.colorflg=2;reject_cursor();swork.colorflg=1;swork.color00=NAN;reject_cursor();swork.color00=.6f;
    NJS_TEXANIM *root=testset[15];testset[15]=(void*)(uintptr_t)1;reject_cursor();testset[15]=root;swork.mode=2;reject_cursor();swork.mode=1;
    rx_inventory_status_reset();sys->memp=cursor;
    unsigned display_bytes;unsigned char *display=rx_boot_read_member(4,145,&display_bytes);check(display!=NULL,"display transition atlases");good(rx_inventory_status_load_resources(display,display_bytes));syFree(display);
    int display_entered;sys->ts_flg|=0x200;good(rx_inventory_status_begin(&display_entered));good(rx_inventory_status_initialize());good(rx_inventory_status_finish_setup());
    for(unsigned tick=0;tick<16;++tick){const char *error=rx_inventory_sprite_tick();if(error)fprintf(stderr,"display tick %u flags=%08x crop=%d/%d ssp=%p mem=%p textures=%u\n",tick,swork.statusflg,swork.open1,swork.open2,swork.sspb,sys->memp,swork.subtx_list.nbTexture);good(error);}
    sys->pad_ps=sys->pad_on=0;bhSetMessage(0,0);sys->mes_ct=1000;good(rx_message_tick(0));
    RxMessageFrame *frame=malloc(sizeof(*frame)),*saved=malloc(sizeof(*saved)),*dialogue=malloc(sizeof(*dialogue));check(frame&&saved&&dialogue,"font frame snapshots");*dialogue=*rx_message_frame();check(dialogue->count>0,"nonempty live dialogue fixture");
    unsigned names=0;for(unsigned scale=0;scale<3;++scale){bhFontScaleSet((float[]){.5f,1,2}[scale],(float[]){.5f,1,2}[scale],1);
        for(unsigned id=0;id<183;++id){good(rx_message_collect_item_name(id,44,342,frame));geometry(raw+4,id,44,342,frame);check(!memcmp(dialogue,rx_message_frame(),sizeof(*dialogue)),"item name replaced the live dialogue frame");++names;}}
    bhFontScaleSet(1,1,1);unsigned aliases[][3]={{72,0,72},{72,0x80000000u,158},{82,0,149},{82,1,82},{82,255,82},{82,256,149},{145,1,159}};
    parts_09b[0].atr|=0x20;parts_09b[1].atr&=~0x20;
    for(unsigned i=0;i<7;++i){swork.pip[2]=aliases[i][0]<<16|aliases[i][1];good(rx_inventory_status_cursor_tick());good(rx_inventory_status_collect_name(frame));geometry(raw+4,aliases[i][2],16+cen_pos[6][0],10+cen_pos[6][1],frame);}
    parts_09b[0].atr&=~0x20;good(rx_inventory_status_cursor_tick());good(rx_inventory_status_collect_name(frame));check(!frame->count,"hidden cursor displayed a name");parts_09b[0].atr|=0x20;
    printf("PASS %u disc-backed names at three font scales, seven original flag/low-byte aliases and hidden selection; independent glyph layout/UV/color and live dialogue frame preservation\n",names);
    good(rx_message_collect_item_name(20,44,342,frame));*saved=*frame;check(rx_message_collect_item_name(183,44,342,frame)!=NULL&&!memcmp(saved,frame,sizeof(*saved)),"bad name ID changed output");
    FontScaleX=NAN;check(rx_message_collect_item_name(20,44,342,frame)!=NULL&&!memcmp(saved,frame,sizeof(*saved)),"bad font scale changed output");FontScaleX=1;
    unsigned int *owned=sys->mes_ip;sys->mes_ip=(void*)(uintptr_t)1;check(rx_message_collect_item_name(20,44,342,frame)!=NULL&&!memcmp(saved,frame,sizeof(*saved)),"foreign name root changed output");sys->mes_ip=owned;
    unsigned offset=owned[1];owned[1]=UINT32_MAX;check(rx_message_collect_item_name(0,44,342,frame)!=NULL&&!memcmp(saved,frame,sizeof(*saved)),"bad owned offset changed output");owned[1]=offset;
    rx_item_names_clear();for(size_t n=0;n<5456;++n)check(rx_item_names_load(raw,n)!=NULL&&!sys->mes_ip,"truncated name bank published");
    unsigned char *bad=malloc(bytes);check(bad!=NULL,"malformed bank fixture");memcpy(bad,raw,bytes);bad[8]=0;bad[9]=bad[10]=bad[11]=0;check(rx_item_names_load(bad,bytes)!=NULL&&!sys->mes_ip,"bad name offset admitted");memcpy(bad,raw,bytes);unsigned first=u32(raw+8)+4;bad[first]=108;bad[first+1]=0;check(rx_item_names_load(bad,bytes)!=NULL&&!sys->mes_ip,"unported glyph admitted");free(bad);good(rx_item_names_load(raw,bytes));
    puts("PASS 5456 truncated bank prefixes, foreign root/offset, unsupported glyph and invalid cursor/name/scale guards preserve output and reject unsafe source reads");
    /* Verify text through the original inventory palette environment. */
    rx_inventory_status_reset();unsigned resource_bytes;unsigned char *resources=rx_boot_read_member(4,145,&resource_bytes);check(resources!=NULL,"status atlases");good(rx_inventory_status_load_resources(resources,resource_bytes));syFree(resources);
    RxEgl egl={0};check(rx_egl_open(&egl,NULL,640,480),"Quest font EGL");glViewport(0,0,640,480);RxParticleDraw *draw=NULL;unsigned char *pixels=malloc(640*480*4),*again=malloc(640*480*4);check(pixels&&again,"GPU snapshots");unsigned changed=0;
    for(unsigned id=0;id<183;++id){good(rx_message_collect_item_name(id,44,342,frame));glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);good(rx_message_frame_present(&draw,frame));glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);for(unsigned i=0;i<640*480;++i){changed+=pixels[i*4]!=0||pixels[i*4+1]!=0||pixels[i*4+2]!=0;check(pixels[i*4+3]==255,"item text changed framebuffer alpha");}}
    check(changed>10000&&glGetError()==GL_NO_ERROR,"original names did not draw through inventory palettes");*saved=*frame;
    glClear(GL_COLOR_BUFFER_BIT);good(rx_message_frame_present(&draw,saved));glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);rx_item_names_clear();glClear(GL_COLOR_BUFFER_BIT);good(rx_message_frame_present(&draw,saved));glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,again);check(!memcmp(pixels,again,640*480*4),"name geometry borrowed retired text bytes");
    check(!memcmp(dialogue,rx_message_frame(),sizeof(*dialogue)),"name GPU presentation replaced live dialogue");printf("PASS all 183 original names on Quest GLES under inventory palettes (%u lit pixels); exact frozen replay after text-bank retirement while font remains owned\n",changed);
    check(!opening.events_ready,"campaign unexpectedly enabled");rx_opening_end(&opening);check(!sys->mes_ip&&rx_message_frame_present(&draw,saved)!=NULL,"retired font context accepted independent name frame");
    rx_particle_close(&draw);rx_egl_close(&egl);free(frame);free(saved);free(dialogue);free(pixels);free(again);syFree(raw);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
