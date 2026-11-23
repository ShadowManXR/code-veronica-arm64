#include "types.h"
#include "android_egl.h"
#include "android/opening_draw.c"
#include "boot_disc.h"
#include "opening.h"
#include "room_scripts.h"
#include "ps2_NaTextureFunction.h"
#include "ps2_sg_maloc.h"
#include "ps2_texture.h"
#include <stdio.h>
#include <string.h>

static unsigned char pixels[640*480*4];
static unsigned char room_before[sizeof(pixels)];
static uint64_t hash_pixels(void)
{
    uint64_t hash=1469598103934665603ull;
    for(unsigned i=0;i<sizeof(pixels);++i)hash=(hash^pixels[i])*1099511628211ull;
    return hash;
}
static int capture_room(RxOpening *opening,unsigned char *output)
{
    for(unsigned i=0;i<512;++i)if((eff[i].flg&1)&&eff[i].id==9&&eff[i].mode0)eff[i].ct0=0;
    glViewport(0,0,640,480);glDepthMask(GL_TRUE);glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const char *error=rx_opening_present(opening);
    if(error){puts(error);return 0;}
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,output);
    return glGetError()==GL_NO_ERROR;
}

static int request_weapon(RxOpening *opening,unsigned number)
{
    unsigned char data[]={8,0,0,0,12,0,0,0,0x27,(unsigned char)number,0,0,0,0};
    RxRoomScripts scripts;const char *error=rx_room_scripts_open(data,sizeof(data),&scripts);
    void *saved=rom->evtp;rom->evtp=&scripts;
    if(!error)error=rx_events_start_room();
    rx_events_reset();rom->evtp=saved;
    if(!error)error=rx_opening_sync_weapon(opening);
    if(error){printf("equipment %u: %s\n",number,error);return 0;}
    return opening->weapon_number==number && plp->wpnr_no==(int)number &&
           sys->ply_wno[0]==(int)number && sys->mn_mode0==3 &&
           !sys->mn_mode1&&!sys->mn_mode2&&!sys->mn_mode3;
}
static int bind_weapon(RxOpening *opening,RxEngineWeapon *next,unsigned number)
{
    rx_engine_weapon_destroy(next);
    return request_weapon(opening,number);
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
        OpeningDraw hand={opening,start,list->nbTexture,NULL};
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
    int initial_world_pose=rx_engine_player_world_pose(opening.player);
    sys->sp_flg&=~0x80u;
    for(unsigned warm=0;warm<3;++warm)if(!capture_room(&opening,room_before))return 22;
    uint64_t reference[21];
    unsigned all_motions=0;
    MN_WORK base[100],events[312];memcpy(base,plp->mnwP,sizeof(base));memcpy(events,plp->mnwP+200,sizeof(events));
    int left=plp->wpnl_no;
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
        reference[weapon]=hash_pixels();
        if(memcmp(base,plp->mnwP,sizeof(base))||memcmp(events,plp->mnwP+200,sizeof(events))||plp->wpnl_no!=left)return 13;
        if(opening.player_work.wpntp!=(int)(weapon>=10))return 14;
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
    for(unsigned group=0;group<2;++group)for(unsigned level=0;level<3;++level) {
        opening.player_work.dmlvl=(int)level;
        if(!request_weapon(&opening,group?10+level:level))return 15;
        for(unsigned clip=0;clip<RX_PLAYER_CLIPS;++clip) {
            error=rx_engine_player_pose(opening.player,plp,(RxPlayerClip)clip,0,0);
            if(error||plp->mtn_no!=PlMtnAct[group][level][clip]||!isfinite(rx_engine_player_speed(opening.player,(RxPlayerClip)clip,0)))return 16;
        }
        RxPlayerClipSource invalid[RX_PLAYER_CLIPS];
        for(unsigned clip=0;clip<RX_PLAYER_CLIPS;++clip)invalid[clip]=(RxPlayerClipSource){PlMtnAct[group][level][clip],-1};
        invalid[RX_PLAYER_REVERSE].motion_slot=512;
        unsigned frames[RX_PLAYER_CLIPS];for(unsigned clip=0;clip<RX_PLAYER_CLIPS;++clip)frames[clip]=rx_engine_player_frames(opening.player,(RxPlayerClip)clip);
        if(!rx_engine_player_clips(opening.player,invalid,1))return 17;
        for(unsigned clip=0;clip<RX_PLAYER_CLIPS;++clip)if(frames[clip]!=rx_engine_player_frames(opening.player,(RxPlayerClip)clip))return 18;
    }
    RxEngineWeapon *bound=opening.weapon;MN_WORK before[100];memcpy(before,plp->mnwP+100,sizeof(before));
    GLuint *cache=opening.textures;unsigned bound_number=opening.weapon_number;
    plp->wpnr_no=255;
    if(!rx_opening_sync_weapon(&opening)||opening.weapon!=bound||opening.textures!=cache||memcmp(before,plp->mnwP+100,sizeof(before)))return 19;
    plp->wpnr_no=(int)bound_number;opening.player_work.dmlvl=0;
    for(unsigned repeat=0;repeat<3;++repeat)for(unsigned number=0;number<=20;++number)
        if(!request_weapon(&opening,number)||!draw_hands(&opening,number)||hash_pixels()!=reference[number])return 20;
    if(!request_weapon(&opening,0))return 21;
    error=rx_engine_player_pose(opening.player,plp,RX_PLAYER_IDLE,0,0);
    if(!error&&initial_world_pose){
        /* Room initialization now publishes original world joints. Restore the
           same pose space before the exact equipment roundtrip comparison. */
        bhCalcModel(plp);error=rx_engine_player_publish(opening.player,plp);
    }
    if(error||!capture_room(&opening,pixels))return 23;
    if(memcmp(room_before,pixels,sizeof(pixels)))return 24;
    puts("PASS exact weapon GPU frames across repeated swaps and exact original room/player frame restored after equipment round trip");
    puts("PASS original event-driven equipment replacement, reserved motion ranges, both locomotion rows/all health levels, rejected clip/selector preservation and repeated GPU swaps");
    rx_opening_close_graphics(&opening);
    rx_egl_close(&egl);
    rx_opening_end(&opening);
    if(plp->mnwP||plp->mlwP||sys->obwp)return 12;
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    printf("PASS 21 original packages and %u weapon motions, bounded failure and native ownership\n",all_motions);
    return 0;
}

