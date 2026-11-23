#pragma once

#include <android/input.h>
#include <stdint.h>

// Button bits after the original pad reader inverts the DualShock report.
enum {
    RX_PAD_L2 = 0x1, RX_PAD_R2 = 0x2, RX_PAD_L1 = 0x4, RX_PAD_R1 = 0x8,
    RX_PAD_TRIANGLE = 0x10, RX_PAD_CANCEL = 0x20, RX_PAD_CONFIRM = 0x40, RX_PAD_SQUARE = 0x80,
    RX_PAD_SELECT = 0x100, RX_PAD_L3 = 0x200, RX_PAD_R3 = 0x400, RX_PAD_START = 0x800,
    RX_PAD_UP = 0x1000, RX_PAD_RIGHT = 0x2000, RX_PAD_DOWN = 0x4000, RX_PAD_LEFT = 0x8000
};

typedef struct {
    uint16_t keys, hat, triggers, pending;
    int32_t device;
    unsigned char axes[4];
    unsigned char pressure[2];
} RxPadInput;

void rx_pad_init(RxPadInput *pad);
void rx_pad_clear(RxPadInput *pad);
int rx_pad_event(RxPadInput *pad, const AInputEvent *event);
void rx_pad_poll(RxPadInput *pad);
void rx_pad_poll_game(RxPadInput *pad);
