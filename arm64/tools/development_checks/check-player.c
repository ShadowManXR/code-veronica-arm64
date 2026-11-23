#include "types.h"
#include "android_egl.h"
#include "boot_disc.h"
#include "main.h"
#include "opening.h"
#include "room_engine.h"
#include <math.h>
#include "pad_input.h"
#include "title_menu.h"
#include <stdio.h>
#include <string.h>

static unsigned char pixels[640 * 480 * 4];

static int frame(RxPadInput *pad, RxTitleScreen *screen, unsigned key)
{
    pad->keys = key;
    rx_pad_poll(pad);
    return rx_title_menu_update(screen);
}

typedef struct {
    uint32_t hash;
    unsigned vertices;
} PoseGeometry;

static int pose_strip(const RxRoomStrip *strip, void *context)
{
    PoseGeometry *pose = context;
    for (unsigned i = 0; i < strip->count; ++i) {
        for (unsigned k = 0; k < 3; ++k) {
            float position = strip->vertices[i].position[k];
            if (!isfinite(position)) return 0;
            uint32_t bits;
            memcpy(&bits, &position, sizeof(bits));
            pose->hash = (pose->hash ^ bits) * 16777619u;
        }
    }
    pose->vertices += strip->count;
    return 1;
}

static int verify_pose(RxOpening *opening)
{
    const NJS_CNK_VIEW view = {2, 20000, 320.0f / tanf((cam.pers / 2) * 0.0000958738f),
                               320, 240, BHD_ASPECT_X, BHD_ASPECT_Y};
    PoseGeometry geometry[3] = {0};
    const unsigned ticks[] = {0, 15, 45};
    for (unsigned i = 0; i < 3; ++i) {
        const char *error = rx_engine_player_pose(opening->player, ticks[i]);
        if (!error) error = rx_engine_player_draw(opening->player, &view, pose_strip, &geometry[i]);
        if (error || !geometry[i].vertices) return 0;
    }
    printf("Claire geometry %u vertices, hashes %08x %08x %08x\n",
           geometry[0].vertices, geometry[0].hash, geometry[1].hash, geometry[2].hash);
    return geometry[0].hash != geometry[1].hash &&
           geometry[0].hash == geometry[2].hash;
}

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    RxAndroidDisc disc = {0};
    RxBootDisc boot;
    const char *error = rx_android_mount_disc(&disc, argv[1]);
    if (!error) error = rx_android_start_disc(&disc, &boot);
    if (error) { puts(error); return 2; }

    RxOpening opening = {0};
    RxEgl egl = {0};
    RxPadInput pad;
    RxTitleScreen screen = RX_TITLE_PROMPT;
    rx_pad_init(&pad);
    rx_title_menu_init();
    frame(&pad, &screen, RX_PAD_START);
    frame(&pad, &screen, 0);
    frame(&pad, &screen, RX_PAD_CONFIRM);
    frame(&pad, &screen, 0);
    if (screen != RX_TITLE_DIFFICULTY) return 3;
    int choice = frame(&pad, &screen, RX_PAD_CONFIRM);
    if (choice != 0 || !rx_egl_open(&egl, NULL, 640, 480)) return 4;

    for (unsigned difficulty = 0; difficulty < 3; ++difficulty) {
        error = rx_opening_start(&opening, difficulty);
        if (error) { puts(error); return 5; }
        if (sys->ply_id || sys->stg_no || sys->rom_no || sys->rcase ||
            sys->gm_mode != difficulty || sys->ply_hp[0] != (difficulty == 2 ? 320 : 160)) return 6;
        printf("PASS original New Game mode %u: Claire HP %d, expanded %u bytes, %u room textures, cut %d\n",
               difficulty, sys->ply_hp[0], sys->rdtsz, opening.texture_count, cam.ncut);
        glViewport(0, 0, 640, 480);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        error = rx_opening_present(&opening);
        if (error) { printf("Draw: %s\n", error); return 7; }
        glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        unsigned colored = 0;
        for (unsigned i = 0; i < sizeof(pixels); i += 4)
            colored += (pixels[i] | pixels[i+1] | pixels[i+2]) != 0;
        if (glGetError() || colored < 1000) { printf("colored: %u\n", colored); return 8; }
        // Compare two warmed frames of the same pose before testing animation.
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        error = rx_opening_present(&opening);
        if (error) { puts(error); return 12; }
        glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (difficulty == 0) {
            FILE *file = fopen(argv[2], "wb");
            if (!file || fwrite(pixels, 1, sizeof(pixels), file) != sizeof(pixels) || fclose(file)) return 9;
        }
                unsigned char first[sizeof(pixels)];
        memcpy(first, pixels, sizeof(first));
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        error = rx_opening_present(&opening);
        if (error) { puts(error); return 13; }
        glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        unsigned drift = 0;
        for (unsigned i = 0; i < sizeof(pixels); i += 4)
            drift += memcmp(pixels + i, first + i, 3) != 0;
        printf("Unchanged pose: %u RGB pixels changed; spawn %.2f %.2f %.2f\n", drift, plp->px, plp->py, plp->pz);
        if (difficulty == 0) {
            FILE *file = fopen("/data/local/tmp/recvx-player-repeat.rgba", "wb");
            if (!file || fwrite(pixels, 1, sizeof(pixels), file) != sizeof(pixels) || fclose(file)) return 14;
        }
        memcpy(first, pixels, sizeof(first));
        opening.pose_tick = 15;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        error = rx_opening_present(&opening);
        if (error) { puts(error); return 10; }
        glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        unsigned changed = 0;
        for (unsigned i = 0; i < sizeof(pixels); i += 4)
            changed += memcmp(pixels + i, first + i, 3) != 0;
        printf("Idle animation changed %u pixels\n", changed);
        if (glGetError() || changed < 10) return 11;
        if (!verify_pose(&opening)) return 15;
        rx_opening_close_graphics(&opening);
        rx_opening_end(&opening);
    }
    rx_egl_close(&egl);
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    puts("PASS live disc data through original room setup and Quest GLES for all difficulties");
    return 0;
}




