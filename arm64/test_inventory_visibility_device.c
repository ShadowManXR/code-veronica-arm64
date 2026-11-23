#include "opening.h"
#include "boot_disc.h"
#include "inventory_status.h"
#include "inventory_sprite.h"
#include "event_audio.h"
#include "main.h"
#include "sub1.h"
#include "fileview.h"
#include "ps2_dummy.h"
#include "ps2_event.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern S_WORK swork;
extern SITEM sitem;
extern PARTS parts_07b[8],parts_15b[64];
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL inventory visibility: %s\n",why);exit(1);}}
static void good(const char *error){check(!error,error);}
static void reject_visibility(void){
    PARTS before[24],box[64];S_WORK work=swork;
    memcpy(before,parts_10b,sizeof(before));memcpy(box,parts_15b,sizeof(box));
    check(rx_inventory_status_visibility()!=NULL,"invalid visibility context admitted");
    check(!memcmp(before,parts_10b,sizeof(before))&&!memcmp(box,parts_15b,sizeof(box))&&!memcmp(&work,&swork,sizeof(work)),"visibility rejection changed state");
}
static void reject_fade(void){
    PARTS before[8];S_WORK work=swork;memcpy(before,parts_07b,sizeof(before));
    check(rx_inventory_sprite_fade_tick()!=NULL,"invalid fade admitted");
    check(!memcmp(before,parts_07b,sizeof(before))&&!memcmp(&work,&swork,sizeof(work)),"fade rejection partially changed state");
}
int main(int argc,char **argv){
    setvbuf(stdout,NULL,_IONBF,0);check(argc==2,"supply ISO directory");
    RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
    good(rx_android_mount_disc(&disc,argv[1]));good(rx_android_start_disc(&disc,&boot));good(rx_opening_start(&opening,0));
    unsigned char *cursor=sys->memp;
    for(unsigned owner=0;owner<4;++owner){
        rx_inventory_status_reset();sys->memp=cursor;rx_sound_queue_reset();
        sys->ply_id=owner;sys->st_flg=8;sys->cb_flg=0x800;sys->ts_flg=0x1200;
        sys->mn_mode0=5;sys->mn_mode1=sys->mn_mode2=sys->mn_mode3=0;
        memset(sys->itm,0,sizeof(sys->itm));
        ViewCnt=7;BackColorFlag=1;fvwork.afsmode=0xffffffff;
        Db.clear0.rgbaq=Db.clear1.rgbaq=0xffffffffffffffffULL;
        for(unsigned i=11;i<15;++i){parts_10b[i].col.r=.1f;parts_10b[i].col.g=.2f;parts_10b[i].col.b=.3f;parts_10b[i].col.a=.42f;}
        int entered;good(rx_inventory_status_begin(&entered));good(rx_inventory_status_initialize());
        check(!ViewCnt&&!BackColorFlag&&!fvwork.afsmode&&!(sys->cb_flg&0x800)&&!Db.clear0.rgbaq&&!Db.clear1.rgbaq,"original entry preface flags/background");
        for(unsigned i=11;i<15;++i)check(parts_10b[i].col.r==.7f&&parts_10b[i].col.g==.7f&&parts_10b[i].col.b==.7f&&parts_10b[i].col.a==.42f,"source portrait RGB reset/alpha preservation");
        reject_visibility();reject_fade();good(rx_inventory_status_finish_setup());
        unsigned cases=0;
        for(unsigned mode=1;mode<=2;++mode)for(unsigned flags=0;flags<8;++flags)for(unsigned special=0;special<2;++special)for(unsigned occupied=0;occupied<=128;++occupied){
            swork.mode=mode;swork.statusflg=(flags&1?0x80:0)|(flags&2?0x10:0)|(flags&4?0x20000000:0);
            sitem.mw.rdid=special?139:0;
            memset(swork.bxp,0,128*sizeof(*swork.bxp));if(occupied)swork.bxp[occupied-1]=0x12345678;
            PARTS expected[24],box[64];
            for(unsigned i=0;i<16;++i)parts_10b[i].atr=(unsigned short)(0xa51b^(i<<5));
            memcpy(expected,parts_10b,sizeof(expected));memcpy(box,parts_15b,sizeof(box));
            for(unsigned i=0;i<16;++i){
                int visible=i==15||((flags&1)?i<11:(i>=11&&(flags&2)&&!(flags&4)&&(!special||i==13||i==14)));
                expected[i].atr=(expected[i].atr&~0x20)|(visible?0x20:0);
            }
            box[18].anim=box[19].anim=occupied?17:16;
            S_WORK work=swork;SITEM item=sitem;unsigned bank[256];memcpy(bank,sys->itm,sizeof(bank));
            good(rx_inventory_status_visibility());
            check(!memcmp(expected,parts_10b,sizeof(expected))&&!memcmp(box,parts_15b,sizeof(box)),"original portrait/box visibility differs from reference");
            check(!memcmp(&work,&swork,sizeof(work))&&!memcmp(&item,&sitem,sizeof(item))&&!memcmp(bank,sys->itm,sizeof(bank)),"visibility changed item/work state");++cases;
        }
        printf("PASS player %u: entry preface and %u original portrait/box visibility states, including all 128 box words and empty box\n",owner,cases);
        swork.mode=1;swork.statusflg=0;
        for(unsigned flags=0;flags<4;++flags)for(unsigned sample=0;sample<15;++sample){
            float start=.08f+sample*.06f,expected=start;
            swork.statusflg=flags;for(unsigned i=0;i<6;++i)parts_07b[i].col=(NJS_ARGB){.a=.35f,.r=start,.g=start,.b=start};
            PARTS before[8];memcpy(before,parts_07b,sizeof(before));
            if(flags&1)expected+=.057142857f;if(flags&2)expected-=.057142857f;
            good(rx_inventory_sprite_fade_tick());
            for(unsigned i=0;i<6;++i){before[i].col.r=before[i].col.g=before[i].col.b=expected;}
            check(!memcmp(before,parts_07b,sizeof(before)),"original ordered RGB fade or unrelated fields differ");
        }
        /* Retain original cumulative float additions, without clamping. */
        swork.statusflg=1;float brightness=0;
        for(unsigned i=0;i<6;++i)parts_07b[i].col.r=parts_07b[i].col.g=parts_07b[i].col.b=0;
        for(unsigned tick=0;tick<8;++tick){good(rx_inventory_sprite_fade_tick());brightness+=.057142857f;check(parts_07b[0].col.r==brightness,"eight-frame cumulative source fade");}
        swork.statusflg=2;for(unsigned tick=0;tick<8;++tick){good(rx_inventory_sprite_fade_tick());brightness-=.057142857f;check(parts_07b[0].col.r==brightness,"reverse cumulative source fade");}
        swork.statusflg=1;parts_07b[5].col.b=NAN;reject_fade();parts_07b[5].col.b=1;reject_fade();
        parts_07b[5].col.b=0;swork.statusflg=2;reject_fade();swork.statusflg=0;parts_07b[5].col.b=-.1f;reject_fade();parts_07b[5].col.b=.5f;
        swork.mode=4;reject_visibility();reject_fade();swork.mode=1;
        swork.subcsr=1;reject_visibility();reject_fade();swork.subcsr=0;
        sys->cb_flg=0x40000;reject_visibility();reject_fade();sys->cb_flg=0;
        unsigned *box=swork.bxp;swork.bxp=(void*)1;reject_visibility();reject_fade();swork.bxp=box;
        char sentinel=parts_10b[16].anim;parts_10b[16].anim=0;reject_visibility();reject_fade();parts_10b[16].anim=sentinel;
        unsigned char *workspace=swork.sspb;swork.sspb=(void*)1;reject_visibility();reject_fade();swork.sspb=workspace;
    }
    puts("PASS original ordered background fades, float accumulation and malformed no-write guards; full composition/actions/close remain gated");
    rx_opening_end(&opening);rx_android_stop_disc();rx_android_unmount_disc(&disc);return 0;
}
