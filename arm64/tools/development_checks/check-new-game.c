#include "android_egl.h"
#include "boot_disc.h"
#include "main.h"
#include "opening.h"
#include "pad_input.h"
#include "title_menu.h"
#include <stdio.h>

static unsigned char pixels[640 * 480 * 4];

static int frame(RxPadInput *pad, RxTitleScreen *screen, unsigned key)
{
    pad->keys = key;
    rx_pad_poll(pad);
    return rx_title_menu_update(screen);
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
        if (difficulty == 0) {
            FILE *file = fopen(argv[2], "wb");
            if (!file || fwrite(pixels, 1, sizeof(pixels), file) != sizeof(pixels) || fclose(file)) return 9;
        }
        rx_opening_close_graphics(&opening);
        rx_opening_end(&opening);
    }
    rx_egl_close(&egl);
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    puts("PASS live disc data through original room setup and Quest GLES for all difficulties");
    return 0;
}
