#include "pad_input.h"
#include "adv.h"
#include "padman.h"
#include "ps2_sg_pad.h"

#include <android/keycodes.h>
#include <string.h>

static uint16_t key_button(int key)
{
    switch (key) {
    case AKEYCODE_MENU:
    case AKEYCODE_BUTTON_START:
    case AKEYCODE_ENTER: return RX_PAD_START;
    case AKEYCODE_BUTTON_SELECT:
    case AKEYCODE_DEL: return RX_PAD_SELECT;
    case AKEYCODE_BUTTON_A:
    case AKEYCODE_DPAD_CENTER: return RX_PAD_CONFIRM;
    case AKEYCODE_BUTTON_B:
    case AKEYCODE_BACK:
    case AKEYCODE_ESCAPE: return RX_PAD_CANCEL;
    case AKEYCODE_BUTTON_X: return RX_PAD_SQUARE;
    case AKEYCODE_BUTTON_Y: return RX_PAD_TRIANGLE;
    case AKEYCODE_BUTTON_L1: return RX_PAD_L1;
    case AKEYCODE_BUTTON_R1: return RX_PAD_R1;
    case AKEYCODE_BUTTON_L2: return RX_PAD_L2;
    case AKEYCODE_BUTTON_R2: return RX_PAD_R2;
    case AKEYCODE_BUTTON_THUMBL: return RX_PAD_L3;
    case AKEYCODE_BUTTON_THUMBR: return RX_PAD_R3;
    case AKEYCODE_DPAD_UP: return RX_PAD_UP;
    case AKEYCODE_DPAD_RIGHT: return RX_PAD_RIGHT;
    case AKEYCODE_DPAD_DOWN: return RX_PAD_DOWN;
    case AKEYCODE_DPAD_LEFT: return RX_PAD_LEFT;
    default: return 0;
    }
}

static unsigned char axis_byte(float value)
{
    if (value >= 1) return 255;
    if (value <= -1) return 0;
    if (!(value > -0.15f && value < 0.15f) && value == value)
        return (unsigned char)(128 + value * 127);
    return 128;
}

static void poll(RxPadInput *pad, int menu)
{
    uint16_t buttons = pad->keys | pad->hat | pad->triggers | pad->pending;
    pad->pending = 0;
    if (menu) {
        uint16_t confirm = buttons & RX_PAD_CONFIRM ? (AdvGetOkButton() & 0x40 ? 0x40 : 0x80) : 0;
        uint16_t cancel = buttons & RX_PAD_CANCEL ? AdvGetCancelButton() : 0;
        buttons = (buttons & (RX_PAD_SELECT | RX_PAD_START | RX_PAD_UP | RX_PAD_RIGHT | RX_PAD_DOWN | RX_PAD_LEFT)) |
                  confirm | cancel;
    }
    unsigned char report[32] = {0, 0x79, (unsigned char)~(buttons >> 8), (unsigned char)~buttons};
    memcpy(report + 4, pad->axes, sizeof(pad->axes));
    static const uint16_t pressure_bits[] = {RX_PAD_RIGHT, RX_PAD_LEFT, RX_PAD_UP, RX_PAD_DOWN,
        RX_PAD_TRIANGLE, RX_PAD_CANCEL, RX_PAD_CONFIRM, RX_PAD_SQUARE,
        RX_PAD_L1, RX_PAD_R1, RX_PAD_L2, RX_PAD_R2};
    for (unsigned i = 0; i < sizeof(pressure_bits) / sizeof(*pressure_bits); ++i)
        report[8 + i] = buttons & pressure_bits[i] ? 255 : 0;
    if (!menu) {
        if (!(pad->keys & RX_PAD_L2) && pad->pressure[0]) report[18] = pad->pressure[0];
        if (!(pad->keys & RX_PAD_R2) && pad->pressure[1]) report[19] = pad->pressure[1];
    }
    pdReadNativeReport(report);
}

void rx_pad_poll(RxPadInput *pad) { poll(pad, 1); }
void rx_pad_poll_game(RxPadInput *pad) { poll(pad, 0); }

void rx_pad_clear(RxPadInput *pad)
{
    *pad = (RxPadInput){.device = -1, .axes = {128, 128, 128, 128}};
    // The original reader uses the preceding sample for analog directions.
    rx_pad_poll(pad);
    rx_pad_poll(pad);
}

void rx_pad_init(RxPadInput *pad)
{
    InitReadKeyEx(1);
    for (unsigned i = 0; i < sizeof(Pad) / sizeof(*Pad); ++i) Pad[i].Calibrate = -1;
    rx_pad_clear(pad);
}

int rx_pad_event(RxPadInput *pad, const AInputEvent *event)
{
    int type = AInputEvent_getType(event);
    int source = AInputEvent_getSource(event);
    int device = AInputEvent_getDeviceId(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        uint16_t button = key_button(AKeyEvent_getKeyCode(event));
        if (!button) return 0;
        int action = AKeyEvent_getAction(event);
        if (action == AKEY_EVENT_ACTION_DOWN && !(AKeyEvent_getFlags(event) & AKEY_EVENT_FLAG_CANCELED)) {
            if (pad->device != device) rx_pad_clear(pad);
            pad->device = device;
            if (!AKeyEvent_getRepeatCount(event)) pad->pending |= button;
            pad->keys |= button;
        } else if (action == AKEY_EVENT_ACTION_UP && pad->device == device) {
            pad->keys &= ~button;
            if (AKeyEvent_getFlags(event) & AKEY_EVENT_FLAG_CANCELED) pad->pending &= ~button;
        }
        return 1;
    }
    if (type != AINPUT_EVENT_TYPE_MOTION || (source & AINPUT_SOURCE_JOYSTICK) != AINPUT_SOURCE_JOYSTICK)
        return 0;
    int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (action == AMOTION_EVENT_ACTION_CANCEL) {
        if (pad->device == device) rx_pad_clear(pad);
        return 1;
    }
    if (action != AMOTION_EVENT_ACTION_MOVE) return 0;
    if (pad->device != device) rx_pad_clear(pad);
    pad->device = device;
    pad->axes[0] = axis_byte(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0));
    pad->axes[1] = axis_byte(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0));
    pad->axes[2] = axis_byte(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0));
    pad->axes[3] = axis_byte(AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0));
    float x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
    float y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
    uint16_t hat = (x < -0.5f ? RX_PAD_LEFT : x > 0.5f ? RX_PAD_RIGHT : 0) |
                   (y < -0.5f ? RX_PAD_UP : y > 0.5f ? RX_PAD_DOWN : 0);
    pad->pending |= hat & ~pad->hat;
    pad->hat = hat;
    float left = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
    float right = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    float brake = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_BRAKE, 0);
    float gas = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_GAS, 0);
    if (brake > left) left = brake;
    if (gas > right) right = gas;
    pad->pressure[0] = left >= 1 ? 255 : left > 0 ? (unsigned char)(left * 255) : 0;
    pad->pressure[1] = right >= 1 ? 255 : right > 0 ? (unsigned char)(right * 255) : 0;
    uint16_t triggers = 0;
    if (pad->pressure[0] >= (pad->triggers & RX_PAD_L2 ? 96 : 128)) triggers |= RX_PAD_L2;
    if (pad->pressure[1] >= (pad->triggers & RX_PAD_R2 ? 96 : 128)) triggers |= RX_PAD_R2;
    pad->pending |= triggers & ~pad->triggers;
    pad->triggers = triggers;
    return 1;
}
