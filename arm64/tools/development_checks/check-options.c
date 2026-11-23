#include "android_egl.h"
#include "boot_disc.h"
#include "pad_input.h"
#include "title_draw.h"
#include <stdio.h>
#include <string.h>
#include <zlib.h>

static unsigned char rgb[640 * 480 * 4];
static RxPadInput pad;
static RxTitleScreen screen;
static RxTitleDraw draw;

static int step(unsigned key)
{
    pad.keys = (uint16_t)key;
    rx_pad_poll(&pad);
    RxTitleScreen previous = screen;
    rx_title_menu_update(&screen);
    glClear(GL_COLOR_BUFFER_BIT);
    return rx_title_present(&draw, &screen, previous == RX_TITLE_OPTIONS);
}

static int tap(unsigned key)
{
    return step(key) && step(0);
}

static uint32_t capture(const char *directory, const char *name)
{
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, rgb);
    if (glGetError()) return 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb) || fclose(file)) return 0;
    return crc32(0, rgb, sizeof(rgb));
}

int main(int argc, char **argv)
{
    if (argc != 4) return 1;
    rx_pad_init(&pad);
    rx_title_menu_init();
    if (!strcmp(argv[3], "restore")) {
        rx_title_read_settings(argv[2]);
        return rx_title_write_settings(argv[2]) ? 0 : 2;
    }

    RxAndroidDisc disc = {0};
    RxBootDisc boot;
    RxEgl egl = {0};
    if (rx_android_mount_disc(&disc, argv[1]) || rx_android_start_disc(&disc, &boot) ||
        !rx_egl_open(&egl, NULL, 640, 480) || !rx_title_open(&draw, &boot)) return 3;
    glViewport(0, 0, 640, 480);
    if (!tap(RX_PAD_START) || !tap(RX_PAD_DOWN) || !tap(RX_PAD_CONFIRM) || screen != RX_TITLE_OPTIONS) return 4;
    uint32_t options = capture(argv[2], "options.rgba");
    if (!tap(RX_PAD_RIGHT) || !tap(RX_PAD_DOWN) || !tap(RX_PAD_CONFIRM)) return 5;
    uint32_t keys = capture(argv[2], "key-config.rgba");
    if (!tap(RX_PAD_RIGHT) || !tap(RX_PAD_RIGHT) || !tap(RX_PAD_CANCEL) ||
        !tap(RX_PAD_DOWN) || !tap(RX_PAD_CONFIRM) || !tap(RX_PAD_RIGHT) || !tap(RX_PAD_DOWN)) return 6;
    uint32_t adjust = capture(argv[2], "position.rgba");
    if (!tap(RX_PAD_CANCEL) || !tap(RX_PAD_DOWN) || !tap(RX_PAD_CONFIRM)) return 7;
    uint32_t bars = capture(argv[2], "color-bars.rgba");
    if (!options || !keys || !adjust || !bars || options == keys || keys == adjust || adjust == bars) return 8;
    if (!tap(RX_PAD_CANCEL) || !tap(RX_PAD_CANCEL) || screen != RX_TITLE_MENU ||
        !rx_title_write_settings(argv[2])) return 9;
    if (!tap(RX_PAD_UP) || !tap(RX_PAD_CONFIRM) || screen != RX_TITLE_DIFFICULTY) return 10;
    puts("PASS original Options, Key Config, Screen Adjust, and Color Bar");
    puts("PASS title navigation with changed controller type and settings write");
    rx_title_close(&draw);
    rx_egl_close(&egl);
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    return 0;
}
