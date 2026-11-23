#include "opening.h"
#include "boot_disc.h"
#include "inventory_status.h"
#include "inventory_sprite.h"
#include "particle_draw.h"
#include "android_egl.h"
#include "sub1.h"
#include "main.h"
#include "gs/gs.h"
#include "ps2_sg_maloc.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
extern S_WORK swork;
extern NJS_TEXANIM *testset[23];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL inventory sprite: %s\n",why);exit(1);}}
static void good(const char *error){check(!error,error);}
static int address(int x,int size,int clamp){if(clamp)return x<0?0:x>=size?size-1:x;return (x%size+size)%size;}
static float texel(const RxInventorySpriteImage *im,int x,int y,unsigned ch){x=address(x,im->width,im->clamp&3);y=address(y,im->height,(im->clamp>>2)&3);return ((const unsigned char*)im->rgba)[((size_t)y*im->width+x)*4+ch];}
static float sample(const RxInventorySpriteImage *im,float u,float v,unsigned ch,int linear){
    float x=u*im->width-.5f,y=v*im->height-.5f;if(!linear)return texel(im,(int)floorf(x+.5f),(int)floorf(y+.5f),ch);
    int ix=(int)floorf(x),iy=(int)floorf(y);float fx=x-ix,fy=y-iy;
    return (texel(im,ix,iy,ch)*(1-fx)+texel(im,ix+1,iy,ch)*fx)*(1-fy)+(texel(im,ix,iy+1,ch)*(1-fx)+texel(im,ix+1,iy+1,ch)*fx)*fy;
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};RxEgl egl={0};
    good(rx_android_mount_disc(&disc,argv[1]));good(rx_android_start_disc(&disc,&boot));good(rx_opening_start(&opening,0));gs_reset();
    check(rx_egl_open(&egl,NULL,640,480),"Quest offscreen EGL");glViewport(0,0,640,480);
    unsigned bytes;unsigned char *data=rx_boot_read_member(4,145,&bytes);check(data!=NULL,"original status package");good(rx_inventory_status_load_resources(data,bytes));syFree(data);
    sys->st_flg|=8;sys->mn_mode0=5;sys->mn_mode1=sys->mn_mode2=sys->mn_mode3=0;sys->ts_flg|=0x200;
    int entered;good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());
    RxInventorySpriteFrame frame={0},again={0};check(rx_inventory_sprite_collect(0,0,&frame)!=NULL,"unfinished setup drew");good(rx_inventory_status_finish_setup());
    for(unsigned tick=1;tick<=16;++tick){good(rx_inventory_sprite_tick());
        for(unsigned i=0;i<12;++i)for(unsigned axis=0;axis<2;++axis){float expected=cen_pos99[i][axis]+cen_pos99[i][axis+4]*(tick<8?tick:8)/8.0f;if(i==3&&axis==0&&tick>8)expected-=4*(tick<13?tick-8:5);check(fabsf(cen_pos[i][axis]-expected)<.001f,"original eight-frame center transition");}
        int crop=tick<9?0:tick-8;check(swork.open1==(crop<6?23-4*crop:0)&&swork.open2==(crop<6?46-8*crop:0),"original crop opening sequence");
    }
    puts("PASS original SpriteH/SpriteUV opening: eight-frame layout trajectory, completion handoff and six-step crop sequence");
    size_t group_count;const RxInventorySpriteGroup *groups=rx_inventory_status_groups(&group_count);RxParticleDraw *draw=NULL;
    unsigned sprites=0,polygons=0,gpu=0;
    for(unsigned g=0;g<group_count;++g)for(unsigned n=0;n<groups[g].end;++n){
        PARTS *p=groups[g].parts+n;PARTS before=*p;S_WORK state=swork;
        const char *error=rx_inventory_sprite_collect(g,n,&frame);if(error){fprintf(stderr,"group=%u index=%u\n",g,n);good(error);}
        check(!memcmp(p,&before,sizeof(before))&&!memcmp(&swork,&state,sizeof(state)),"sprite dispatch mutated game work");
        unsigned horizontal=(p->setnum>>4)+1,vertical=(p->setnum&15)+1;
        check(frame.count==((p->atr&0x20)?(horizontal>vertical?horizontal:vertical):0),"original repeat count");
        ++sprites;polygons+=frame.count;
        good(rx_inventory_sprite_present(&draw,&frame));
        if(frame.count==1&&p->ang==0){
            const RxInventorySpritePolygon *q=frame.polygons;const RxInventorySpriteImage *im=frame.images+q->image;
            float x=q->position[0][0]-1728,y=q->position[0][1]-1808,w=q->position[1][0]-q->position[0][0],h=q->position[2][1]-q->position[0][1];
            int px=(int)floorf(x+w*.5f),py=(int)floorf(y+h*.5f);
            if(w>=4&&h>=4&&px>=0&&px<640&&py>=0&&py<480){
                glClearColor(23/255.0f,37/255.0f,59/255.0f,1);glClear(GL_COLOR_BUFFER_BIT);good(rx_inventory_sprite_present(&draw,&frame));
                unsigned char pixel[4];glReadPixels(px,479-py,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                float u=q->uv[0][0]+(q->uv[1][0]-q->uv[0][0])*(px+.5f-x)/w;
                float v=q->uv[0][1]+(q->uv[2][1]-q->uv[0][1])*(py+.5f-y)/h;
                float ratio=fmaxf(fabsf(q->uv[1][0]-q->uv[0][0])*im->width/w,fabsf(q->uv[2][1]-q->uv[0][1])*im->height/h);
                int linear=(im->tex1>>(ratio>1.0001f?6:5))&1;
                float alpha=fminf(1,floorf(sample(im,u,v,3,linear)*q->color[0][3]/128.0f+.0001f)/128.0f);const unsigned bg[]={23,37,59};
                for(unsigned ch=0;ch<3;++ch){int expected=(int)lrintf(floorf(sample(im,u,v,ch,linear)*q->color[0][ch]/128.0f+.0001f)*alpha+bg[ch]*(1-alpha));if(abs((int)pixel[ch]-expected)>3){fprintf(stderr,"GPU g%u n%u c%u got%u expected%d tex1=%llx ratio=%f\n",g,n,ch,pixel[ch],expected,(unsigned long long)im->tex1,ratio);check(0,"Quest pixel differs from independent atlas sampling/modulation/blend");}}
                check(pixel[3]==255,"inventory changed framebuffer alpha");++gpu;
            }
        }
    }
    printf("PASS %u original source sprites / %u repeated polygons; %u Quest GLES samples match independent atlas/filter/material/alpha calculations; original game work preserved\n",sprites,polygons,gpu);check(gpu>=20,"insufficient GPU samples");
    good(rx_inventory_sprite_collect(0,0,&frame));RxInventorySpriteFrame saved=frame;PARTS original=groups[0].parts[0];
    groups[0].parts[0].parts_num=23;check(rx_inventory_sprite_collect(0,0,&frame)!=NULL&&!memcmp(&saved,&frame,sizeof(frame)),"bad animation replaced output");groups[0].parts[0]=original;
    NJS_TEXANIM *anim=testset[original.parts_num];testset[original.parts_num]=(void*)(uintptr_t)1;check(rx_inventory_sprite_collect(0,0,&frame)!=NULL&&!memcmp(&saved,&frame,sizeof(frame)),"foreign animation root replaced output");testset[original.parts_num]=anim;
    groups[0].parts[0].move=NAN;check(rx_inventory_sprite_collect(0,0,&frame)!=NULL&&!memcmp(&saved,&frame,sizeof(frame)),"nonfinite repetition accepted");groups[0].parts[0]=original;
    check(rx_inventory_sprite_collect(99,0,&frame)!=NULL&&rx_inventory_sprite_collect(0,99,&frame)!=NULL&&rx_inventory_sprite_collect(0,0,NULL)!=NULL,"bad indices/output accepted");
    good(rx_inventory_sprite_collect(0,0,&again));check(frame.count==again.count&&frame.image_count==again.image_count&&!memcmp(frame.polygons,again.polygons,sizeof(frame.polygons)),"repeat collection changed sprite packets");
    for(unsigned i=0;i<frame.image_count;++i)check(!memcmp(frame.images[i].rgba,again.images[i].rgba,(size_t)frame.images[i].width*frame.images[i].height*4),"repeat collection changed decoded atlas");
    unsigned char *a=malloc(640*480*4),*b=malloc(640*480*4);check(a&&b,"framebuffer snapshots");
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);good(rx_inventory_sprite_present(&draw,&frame));glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,a);
    rx_inventory_status_reset();glClear(GL_COLOR_BUFFER_BIT);good(rx_inventory_sprite_present(&draw,&frame));glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,b);check(!memcmp(a,b,640*480*4),"owned sprite frame changed after resource retirement");
    puts("PASS malformed sprite/table/repetition guards preserve prior frame; repeat collection and GPU replay are exact, including after source atlas retirement; full StatusMain remains gated");
    free(a);free(b);rx_inventory_sprite_destroy(&frame);rx_inventory_sprite_destroy(&again);rx_particle_close(&draw);rx_opening_end(&opening);rx_egl_close(&egl);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
