#include "types.h"
#include "android_egl.h"
#include "boot_disc.h"
#include "main.h"
#include "movie_port.h"
#include "pad_input.h"
#include "padman.h"
#include "title_draw.h"
#include "adv.h"
#include "sdfunc.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned char pixels[640*480*4], first[640*480*4];
static int render(RxTitleDraw *draw, RxTitleScreen *screen)
{
    glViewport(0,0,640,480);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!rx_title_present(draw,screen,0)) return 0;
    glReadPixels(0,0,640,480,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    return glGetError() == GL_NO_ERROR;
}
int main(int argc,char **argv)
{
    if(argc!=2)return 1;
    RxAndroidDisc disc={0};
    RxBootDisc boot={0};
    RxTitleDraw draw={0};
    RxEgl egl={0};
    RxPadInput pad;
    RxTitleScreen screen=RX_TITLE_PROMPT;
    const char *error=rx_android_mount_disc(&disc,argv[1]);
    if(!error)error=rx_android_start_disc(&disc,&boot);
    if(error){puts(error);return 2;}
    rx_pad_init(&pad);
    rx_title_menu_init();
    if(!rx_egl_open(&egl,NULL,640,480)||!rx_title_open(&draw,&boot))return 3;
    for(unsigned i=0;i<8;++i){rx_pad_poll(&pad);rx_title_menu_update(&screen);}
    if(AdvWork.FlushCount!=255 || AdvWork.FlushSpeed!=-4 || !render(&draw,&screen))return 4;
    memcpy(first,pixels,sizeof(first));
    for(unsigned i=0;i<32;++i){rx_pad_poll(&pad);rx_title_menu_update(&screen);}
    if(AdvWork.FlushCount!=128 || AdvWork.FlushSpeed!=4 || !render(&draw,&screen))return 5;
    unsigned changed=0,outside=0;
    for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x){
        unsigned offset=(y*640+x)*4;
        if(memcmp(first+offset,pixels+offset,3)){
            ++changed;
            if(x<64||x>=576||y<144||y>=176)++outside;
        }
    }
    printf("Prompt pulse changed %u pixels (%u outside text)\n",changed,outside);
    if(!changed||outside)return 6;
    for(unsigned i=0;i<32;++i){rx_pad_poll(&pad);rx_title_menu_update(&screen);}
    if(AdvWork.FlushCount!=255 || AdvWork.FlushSpeed!=-4)return 7;
    pad.pending=rx_title_pointer_button(screen,320,320);
    rx_pad_poll(&pad);
    if(Pad[0].press!=RX_PAD_START)return 8;
    rx_title_menu_update(&screen);
    if(screen!=RX_TITLE_MENU)return 9;
    rx_pad_poll(&pad);rx_title_menu_update(&screen);
    pad.pending=rx_title_pointer_button(screen,320,335);
    rx_pad_poll(&pad);rx_title_menu_update(&screen);
    if(screen!=RX_TITLE_DIFFICULTY)return 10;
    rx_pad_poll(&pad);rx_title_menu_update(&screen);
    pad.pending=rx_title_pointer_button(screen,320,350);
    rx_pad_poll(&pad);
    if(rx_title_menu_update(&screen)!=1)return 11;
    if(rx_title_pointer_button(RX_TITLE_DIFFICULTY,5,5))return 12;
    puts("PASS pointer Start, New Game and difficulty through original pad edges");
    rx_title_close(&draw);
    rx_egl_close(&egl);
    rx_game_movie_bind(&disc.image);
    if(AdvStartStartupMovie())return 13;
    pad.pending=RX_PAD_START;
    rx_pad_poll(&pad);
    int result=PlayMovieMain();
    if(result!=2||rx_game_movie_active())return 14;
    puts("PASS pointer-generated Start skips original opening movie");
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    return 0;
}
