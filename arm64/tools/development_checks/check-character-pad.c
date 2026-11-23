#include "types.h"
#include "boot_disc.h"
#include "main.h"
#include "opening.h"
#include "pad_input.h"
#include <stdio.h>

static void sample(RxPadInput *pad, unsigned buttons)
{
    pad->keys = buttons;
    rx_pad_poll_game(pad);
    rx_opening_read_controls();
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    RxAndroidDisc disc = {0};
    RxBootDisc boot;
    RxOpening opening = {0};
    RxPadInput pad;
    const char *err = rx_android_mount_disc(&disc, argv[1]);
    if (!err) err = rx_android_start_disc(&disc, &boot);
    if (!err) err = rx_opening_start(&opening, 0);
    if (err) { puts(err); return 2; }
    rx_pad_init(&pad);

    const unsigned physical[] = {RX_PAD_CONFIRM, RX_PAD_CANCEL, RX_PAD_SQUARE, RX_PAD_TRIANGLE,
        RX_PAD_R1, RX_PAD_L1, RX_PAD_SELECT, RX_PAD_R3, RX_PAD_START};
    const unsigned mapped[3][9] = {
        {0xb00,0x1400,0xb00,0x2000,0x10,0x80,0x18000,0x4000,0x24000},
        {0xb00,0xb00,0x1400,0x2000,0x10,0x80,0x18000,0x4000,0x24000},
        {0x1400,0xb00,0xb00,0x2000,0x10,0x80,0x18000,0x4000,0x24000}
    };
    for (unsigned layout=0; layout<3; ++layout) {
        sys->keytype = layout;
        for (unsigned i=0; i<sizeof(physical)/sizeof(*physical); ++i) {
            sample(&pad, physical[i]);
            unsigned expect = mapped[layout][i];
            if (sys->pad_on != expect || sys->pad_ps != expect || sys->pad_rs ||
                sys->p1per->on != physical[i]) {
                printf("layout %u button %x: on %x press %x expect %x\n",
                       layout, physical[i],sys->pad_on,sys->pad_ps,expect);
                return 3;
            }
            sample(&pad, physical[i]);
            if (sys->pad_on != expect || sys->pad_ps || sys->pad_rs) return 4;
            sample(&pad, 0);
            if (sys->pad_on || sys->pad_ps || sys->pad_rs != expect) return 5;
        }
    }
    const unsigned directions[] = {0, RX_PAD_LEFT, RX_PAD_RIGHT, RX_PAD_UP,
        RX_PAD_UP|RX_PAD_LEFT, RX_PAD_UP|RX_PAD_RIGHT, RX_PAD_DOWN,
        RX_PAD_DOWN|RX_PAD_LEFT, RX_PAD_DOWN|RX_PAD_RIGHT};
    const unsigned modes[] = {0,2,1,3,5,4,9,11,10};
    for (unsigned i=0; i<sizeof(directions)/sizeof(*directions); ++i) {
        sample(&pad, directions[i]);
        if (plp->mode2 != modes[i]) return 6;
        sample(&pad, 0);
    }
    pad.axes[3] = 0;
    sample(&pad, 0);
    if ((sys->pad_on & 15) != 1 || plp->mode2 != 3 || sys->pad_ay >= -63) return 7;
    pad.axes[3] = 128;
    pad.axes[2] = 255;
    sample(&pad, 0);
    sample(&pad, 0);
    if ((sys->pad_on & 15) != 8 || plp->mode2 != 1 || sys->pad_ax <= 63) return 8;
    rx_pad_clear(&pad);
    rx_opening_clear_controls();
    sample(&pad, 0);
    if (sys->pad_on || sys->pad_ps || sys->pad_rs || sys->pad_dx || sys->pad_dy || plp->mode2) return 9;
    pad.pending = RX_PAD_CONFIRM;
    sample(&pad, 0);
    if (sys->pad_on != 0x1400 || sys->pad_ps != 0x1400 || pad.pending) return 10;
    sample(&pad, 0);
    sys->cb_flg |= 4;
    sample(&pad, RX_PAD_R1 | RX_PAD_UP);
    if (sys->pad_on & 0x10 || plp->mode2) return 11;
    rx_opening_end(&opening);
    rx_android_stop_disc();
    rx_android_unmount_disc(&disc);
    puts("PASS all three original key layouts, button press/hold/release and short taps");
    puts("PASS original 8-way tank locomotion selection, stick thresholds, cinematic mask and focus reset");
    return 0;
}

