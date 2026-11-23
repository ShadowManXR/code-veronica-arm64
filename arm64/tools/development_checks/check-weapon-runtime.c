#include "types.h"
#include "android_egl.h"
#include "../android/opening_draw.c"
#include "boot_disc.h"
#include "opening.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_sg_maloc.h"
#include "ps2_texture.h"
#include <stdio.h>
#include <string.h>

static unsigned char pixels[640*480*4];

static int bind_weapon(RxOpening *opening,RxEngineWeapon *next,unsigned number)
{
    rx_opening_close_graphics(opening);
    for(unsigned side=0;side<2;++side){
        NJS_TEXLIST *list=rx_engine_weapon_textures(opening->weapon,side,NULL);
        if(list)njReleaseTexture(list);
    }
    bhGarbageTexture(NULL,0);
    memset(sys->obwp,0,2*sizeof(*sys->obwp));
    memset(plp->mnwP+100,0,100*sizeof(MN_WORK));
    rx_engine_weapon_destroy(opening->weapon);
    opening->weapon=next;
    opening->texture_count=opening->room_texture_count+
        rx_engine_player_textures(opening->player,NULL)->nbTexture;
    for(unsigned side=0;side<2;++side){
        unsigned char *data;
        NJS_TEXLIST *list=rx_engine_weapon_textures(next,side,&data);
        if(list){
            int count=bhSetMemPvpTexture(list,data,0);
            if(count<0||(unsigned)count!=list->nbTexture)return 0;
            opening->texture_count+=list->nbTexture;
        }
    }
    plp->wpnr_no=plp->wpnl_no=number;
    for(unsigned side=0;side<2;++side)bhSetWeapon(&sys->obwp[side],number,side);
    rx_engine_weapon_bind(next,plp,sys->obwp);
    free(opening->textures);
    opening->textures=calloc(opening->texture_count,sizeof(*opening->textures));
    return opening->textures!=NULL;
}

static int draw_hands(RxOpening *opening,unsigned number)
{
    if(!open_graphics(opening))return 0;
    glViewport(0,0,640,480);
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glUseProgram(opening->program);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(opening->vao);
    glBindBuffer(GL_ARRAY_BUFFER,opening->vertices);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    NJS_CNK_VIEW view={2,20000,600,320,240,1,1};
    float range=view.far_z-view.near_z;
    glUniform4f(opening->projection_uniform,600.0f/320,600.0f/240,
                (view.far_z+view.near_z)/range,-2*view.far_z*view.near_z/range);
    njCnkSetEasyMultiAmbient(1,1,1);
    unsigned start=opening->room_texture_count+
        rx_engine_player_textures(opening->player,NULL)->nbTexture;
    for(unsigned side=0;side<2;++side){
        NJS_TEXLIST *list=rx_engine_weapon_textures(opening->weapon,side,NULL);
        if(!list)continue;
        if(!njSetTexture(list))return 0;
        njUnitMatrix(NULL);
        njTranslate(NULL,side?5:-5,0,30);
        OpeningDraw hand={opening,start,list->nbTexture};
        const char *error=rx_engine_weapon_draw(opening->weapon,side,&view,draw_strip,&hand);
        if(error){printf("weapon %u side %u draw %s\n",number,side,error);return 0;}
        start+=list->nbTexture;
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    if(glGetError())return 0;
    unsigned colored=0;
    for(unsigned i=0;i<sizeof(pixels);i+=4)colored+=(pixels[i]|pixels[i+1]|pixels[i+2])!=0;
    if(colored<20){printf("weapon %u empty draw %u\n",number,colored);return 0;}
    if(number==0||number==2||number==6||number==20){
        char path[128];
        snprintf(path,sizeof(path),"/data/local/tmp/recvx-weapon-%u.rgba",number);
        FILE *file=fopen(path,"wb");
        if(!file||fwrite(pixels,1,sizeof(pixels),file)!=sizeof(pixels)||fclose(file))return 0;
    }
    return 1;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 1;
    RxAndroidDisc disc={0};
    RxBootDisc boot={0};
    RxOpening opening={0};
    RxEgl egl={0};
    const char *error=rx_android_mount_disc(&disc,argv[1]);
    if(!error)error=rx_android_start_disc(&disc,&boot);
    if(!error)error=rx_opening_start(&opening,0);
    if(error){puts(error);return 2;}
    if(!opening.weapon||plp->wpnr_no!=0||sys->obwp[0].lkono!=9||sys->obwp[1].lkono!=13)return 3;
    if(!rx_egl_open(&egl,NULL,640,480))return 4;
    unsigned all_motions=0;
    for(unsigned weapon=0;weapon<=20;++weapon){
        unsigned bytes;
        unsigned char *data=rx_boot_read_member(6,20+weapon,&bytes);
        if(!data)return 5;
        RxEngineWeapon *next=NULL,*bad=NULL;
        error=rx_engine_weapon_load(data,bytes,22,&next);
        if(error){printf("weapon %u load %s\n",weapon,error);return 6;}
        if(!rx_engine_weapon_load(data,7,22,&bad)||bad)return 7;
        syFree(data);
        if(!bind_weapon(&opening,next,weapon)||!draw_hands(&opening,weapon))return 8;
        unsigned motion_count=0;
        for(unsigned slot=100;slot<200;++slot){
            MN_WORK *motion=&plp->mnwP[slot];
            if(!motion->datP)continue;
            for(unsigned key=0;key<3;++key){
                plp->mtn_no=slot;
                plp->frm_no=(key==0?0:key==1?motion->frm_num/2:motion->frm_num-1)*65536;
                plp->mtn_add=65536;
                plp->hokan_count=0;
                error=rx_engine_player_motion(opening.player,plp,0);
                if(error){printf("weapon %u motion %u %s\n",weapon,slot,error);return 9;}
            }
            ++motion_count;
        }
        all_motions+=motion_count;
        for(unsigned side=0;side<2;++side){
            O_WRK *object=&sys->obwp[side];
            bhObjWpn((BH_PWORK*)object);
            bhCalcModel((BH_PWORK*)object);
            if(object->lkwkp!=(unsigned char*)plp||object->lkono!=(side?13:9))return 10;
            float *parent=plp->mlwP->owP[object->lkono].mtx;
            if(fabsf((*object->mtx)[12]-parent[12])>.001f||
               fabsf((*object->mtx)[13]-parent[13])>.001f||
               fabsf((*object->mtx)[14]-parent[14])>.001f)return 11;
        }
        printf("PASS original weapon %u: right %u left %u nodes, %u player motions, GLES and hand sockets\n",
               weapon,sys->obwp[0].mlwP->obj_num,sys->obwp[1].mlwP->obj_num,motion_count);
    }
    rx_opening_close_graphics(&opening);
    rx_egl_close(&egl);
    rx_opening_end(&opening);
    if(plp->mnwP||plp->mlwP||sys->obwp)return 12;
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    printf("PASS 21 original packages and %u weapon motions, bounded failure and native ownership\n",all_motions);
    return 0;
}

