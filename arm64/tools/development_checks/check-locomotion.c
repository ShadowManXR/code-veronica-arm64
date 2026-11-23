#include "types.h"
#include "android_egl.h"
#include "boot_disc.h"
#include "main.h"
#include "opening.h"
#include "pad_input.h"
#include <math.h>
#include <stdio.h>

static unsigned char pixels[640*480*4];

static int step(RxOpening *opening, RxPadInput *pad, unsigned buttons)
{
    pad->keys = buttons;
    rx_pad_poll_game(pad);
    rx_opening_read_controls();
    const char *error = rx_opening_move_player(opening);
    if (error) puts(error);
    return !error;
}
static float delta(float x, float z) { return hypotf(plp->px - x, plp->pz - z); }

static int capture(RxOpening *opening, const char *path)
{
    glViewport(0, 0, 640, 480);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const char *error = rx_opening_present(opening);
    if (error) { puts(error); return 0; }
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (glGetError()) return 0;
    FILE *out = fopen(path,"wb");
    if (!out) return 0;
    int success = fwrite(pixels,1,sizeof(pixels),out) == sizeof(pixels);
    return fclose(out) == 0 && success;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    RxAndroidDisc disc = {0};
    RxBootDisc boot;
    RxOpening opening = {0};
    RxPadInput pad;
    RxEgl egl = {0};
    const char *error = rx_android_mount_disc(&disc, argv[1]);
    if (!error) error = rx_android_start_disc(&disc, &boot);
    if (!error) error = rx_opening_start(&opening, 0);
    if (error) { puts(error); return 2; }
    if (!rx_egl_open(&egl, NULL, 640, 480)) return 3;
    rx_pad_init(&pad);
    sys->keytype = 0;
    printf("spawn %.3f %.3f %.3f yaw %d cut %d walls %d\n", plp->px, plp->py, plp->pz, plp->ay, cam.ncut, rom->wal_n);
    float x = plp->px, z = plp->pz;
    unsigned yaw = plp->ay & 65535;
    if (!step(&opening, &pad, RX_PAD_RIGHT) || opening.pose_clip != RX_PLAYER_TURN ||
        plp->ay != ((yaw - 1310) & 65535) || delta(x,z)) return 4;
    if (!step(&opening, &pad, RX_PAD_LEFT) || plp->ay != yaw || delta(x,z)) return 5;
    for (int i = 0; i < 15; ++i)
        if (!step(&opening, &pad, RX_PAD_UP)) return 6;
    printf("walk %.3f %.3f distance %.3f frame %u cut %d hit %u\n",plp->px,plp->pz,delta(x,z),opening.pose_frame,cam.ncut,plp->stflg&1);
    if (opening.pose_clip != RX_PLAYER_WALK || delta(x,z) < 1.0f) return 7;
    if (!capture(&opening,"/data/local/tmp/recvx-walk.rgba")) return 8;
    float walk_x = plp->px, walk_z = plp->pz;
    for (int i = 0; i < 10; ++i)
        if (!step(&opening, &pad, RX_PAD_DOWN)) return 9;
    printf("reverse %.3f %.3f distance %.3f frame %u cut %d hit %u\n",plp->px,plp->pz,delta(walk_x,walk_z),opening.pose_frame,cam.ncut,plp->stflg&1);
    if (opening.pose_clip != RX_PLAYER_REVERSE || delta(walk_x,walk_z) < 1 || 
        (walk_x-x)*(plp->px-walk_x)+(walk_z-z)*(plp->pz-walk_z) >= 0) return 10;
    x = plp->px; z = plp->pz;
    if (!step(&opening, &pad, RX_PAD_UP|RX_PAD_CANCEL) || opening.pose_clip != RX_PLAYER_RUN ||
        fabsf(delta(x,z)-1.052632f) > 0.01f) return 11;
    unsigned hits=0, cuts=0;
    int last_cut=cam.ncut;
    for (int i=0;i<160;++i) {
        if (!step(&opening, &pad, RX_PAD_UP|RX_PAD_CANCEL)) return 12;
        hits += (plp->stflg&1)!=0;
        cuts += cam.ncut != last_cut;
        last_cut=cam.ncut;
    }
    printf("run to %.3f %.3f after %u collision frames and %u camera changes\n",plp->px,plp->pz,hits,cuts);
    if (!hits) return 13;
    x=plp->px; z=plp->pz;
    for (int i=0;i<25;++i)
        if (!step(&opening,&pad,RX_PAD_UP|RX_PAD_CANCEL)) return 14;
    if (!step(&opening, &pad, 0) || opening.pose_clip != RX_PLAYER_IDLE || delta(x,z) > 0.01f) return 15;
    if (!capture(&opening,"/data/local/tmp/recvx-wall.rgba")) return 16;
    for (int i=0;i<17;++i)
        if (!step(&opening,&pad,RX_PAD_LEFT)) return 17;
    x=plp->px; z=plp->pz;
    for (int i=0;i<18;++i)
        if (!step(&opening,&pad,RX_PAD_UP)) return 18;
    printf("away from wall: distance %.3f, cut %d, camera %.2f %.2f %.2f\n",
           delta(x,z),cam.ncut,cam.px,cam.py,cam.pz);
    if (delta(x,z) < 1.0f || !capture(&opening,"/data/local/tmp/recvx-camera.rgba")) return 19;
    rx_opening_close_graphics(&opening);
    rx_egl_close(&egl);
    rx_opening_end(&opening);
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    puts("PASS original walk/run/reverse motions and speeds, both turns, collision and stop, Quest GLES");
    return 0;
}

