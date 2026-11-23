#include "android_egl.h"
#include "boot_disc.h"
#include "pad_input.h"
#include "title_draw.h"
#include <stdio.h>
#include <zlib.h>
static unsigned char pixels[640 * 480 * 4];

static int step(RxPadInput *p, RxTitleScreen *s, uint16_t key)
{
    p->keys = key;
    rx_pad_poll(p);
    return rx_title_menu_update(s);
}
static uint32_t capture(RxTitleDraw *draw, RxTitleScreen s)
{
    glViewport(0, 0, 640, 480);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!rx_title_present(draw, s)) return 0;
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    return glGetError() ? 0 : crc32(0, pixels, sizeof(pixels));
}
int main(int argc, char **argv)
{
    RxTitleScreen s = RX_TITLE_PROMPT;
    RxAndroidDisc disc = {0};
    RxBootDisc boot;
    RxTitleDraw draw = {0};
    RxEgl egl = {0};
    RxPadInput pad;
    if (argc != 3 || rx_android_mount_disc(&disc, argv[1]) || rx_android_start_disc(&disc, &boot)) return 1;
    rx_pad_init(&pad);
    rx_title_menu_init();
    if (!rx_egl_open(&egl, NULL, 640, 480) || !rx_title_open(&draw, boot.title_package, boot.title_size)) return 2;
    uint32_t prompt = capture(&draw, s);
    step(&pad, &s, RX_PAD_START);
    step(&pad, &s, RX_PAD_START);
    if (s != RX_TITLE_MENU) return 3;
    step(&pad, &s, 0);
    uint32_t menu = capture(&draw, s);
    step(&pad, &s, RX_PAD_UP);
    step(&pad, &s, 0);
    step(&pad, &s, RX_PAD_CONFIRM);
    if (s != RX_TITLE_DIFFICULTY) return 4;
    step(&pad, &s, 0);
    uint32_t difficulty = capture(&draw, s);
    if (!prompt || !menu || !difficulty || prompt == menu || menu == difficulty || prompt == difficulty) return 5;
    for (int i = 0; i < 12; ++i) step(&pad, &s, RX_PAD_DOWN);
    step(&pad, &s, 0);
    int selection = step(&pad, &s, RX_PAD_CONFIRM);
    if (selection != 2) {
        fprintf(stderr, "Held down expected original difficulty 2, got %d\n", selection);
        return 6;
    }
    step(&pad, &s, 0);
    rx_title_close(&draw);
    rx_egl_close(&egl);
    if (!rx_egl_open(&egl, NULL, 640, 480) || !rx_title_open(&draw, boot.title_package, boot.title_size) ||
        !capture(&draw, s)) return 7;
    FILE *file = fopen(argv[2], "wb");
    if (!file || fwrite(pixels, 1, sizeof(pixels), file) != sizeof(pixels) || fclose(file)) return 8;
    step(&pad, &s, RX_PAD_CANCEL);
    step(&pad, &s, RX_PAD_CANCEL);
    if (s != RX_TITLE_MENU) return 9;
    step(&pad, &s, 0);
    pad.pending = RX_PAD_CANCEL;
    step(&pad, &s, 0);
    if (s != RX_TITLE_PROMPT) return 10;
    rx_title_close(&draw);
    rx_egl_close(&egl);
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    puts("PASS pad edges, 12-frame repeat, short presses, all difficulty rows, and Back");
    puts("PASS prompt, main menu and difficulty rendered on Quest GLES and restored after EGL recreation");
    return 0;
}
