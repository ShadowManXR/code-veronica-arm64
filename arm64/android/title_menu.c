#include "title_menu.h"
#include "pad_input.h"
#include "adv.h"
#include "main.h"
#include "padman.h"
#include "sdc.h"
#include "sdfunc.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

// Options are app-private and carry only settings consumed by the Android port.
enum { SETTINGS_SIZE = 8 };

static int settings_path(char *path, size_t size, const char *directory, const char *name)
{
    if (!directory) return 0;
    int count = snprintf(path, size, "%s/%s", directory, name);
    return count > 0 && (size_t)count < size;
}

void rx_title_read_settings(const char *directory)
{
    char path[4096];
    if (!settings_path(path, sizeof(path), directory, "options.bin")) return;
    FILE *file = fopen(path, "rb");
    if (!file) return;
    unsigned char data[SETTINGS_SIZE];
    int valid = fread(data, 1, sizeof(data), file) == sizeof(data) && fgetc(file) == EOF && !ferror(file);
    fclose(file);
    if (!valid || memcmp(data, "RXO\1", 4) || data[4] > 1 || data[5] > 2 || data[6] > 20 || data[7] > 20)
        return;
    SetSoundModeEx(data[4], 0);
    sys->keytype = data[5];
    RequestAdjustDisplay(data[6] - 10, data[7] - 10);
}

int rx_title_write_settings(const char *directory)
{
    char path[4096], temporary[4096];
    if (!settings_path(path, sizeof(path), directory, "options.bin") ||
        !settings_path(temporary, sizeof(temporary), directory, "options.tmp")) return 0;
    unsigned char data[SETTINGS_SIZE] = {'R', 'X', 'O', 1, (unsigned char)GetSoundMode(), sys->keytype,
                                       (unsigned char)(sys->adjust_x + 10), (unsigned char)(sys->adjust_y + 10)};
    FILE *file = fopen(temporary, "wb");
    if (!file) return 0;
    int written = fwrite(data, 1, sizeof(data), file) == sizeof(data) && fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0) written = 0;
    if (written && rename(temporary, path) == 0) return 1;
    unlink(temporary);
    return 0;
}

void rx_title_menu_init(void)
{
    AdvWork = (ADV_WORK){0};
    AdvWork.CursorMax[0] = 3;
    AdvWork.CursorMax[1] = 3;
    // Start on New Game, as the original does with no loadable memory card.
    AdvWork.Cursor[0] = 1;
    for (int level = 0; level < 2; ++level)
        for (int row = 0; row < 3; ++row) AdvWork.CursorFlag[level][row] = 1;
    AdvWork.FontBaseColor = 0xffffff;
    AdvWork.FlushCount = 0;
    AdvWork.FlushSpeed = 32.0f;
}

static void advance_prompt(void)
{
    // Adv_BioCvTitle mode 6 ramps in, then pulses between 128 and 255.
    AdvWork.FlushCount += AdvWork.FlushSpeed;
    if (AdvWork.FlushSpeed < 0 && AdvWork.FlushCount <= 128.0f) {
        AdvWork.FlushCount = 128.0f;
        AdvWork.FlushSpeed = 4.0f;
    } else if (AdvWork.FlushSpeed > 0 && AdvWork.FlushCount >= 255.0f) {
        AdvWork.FlushCount = 255.0f;
        AdvWork.FlushSpeed = -4.0f;
    }
}

unsigned rx_title_pointer_button(RxTitleScreen screen, float x, float y)
{
    if (x < 0 || x >= 640 || y < 0 || y >= 480) return 0;
    if (screen == RX_TITLE_PROMPT) return RX_PAD_START;
    if (screen == RX_TITLE_MENU && x >= 64 && x < 577) {
        float first_row = AdvWork.ExtraFlag ? 272.0f : 288.0f;
        int row = (int)((y - first_row) / 32.0f);
        if (y >= first_row && row < AdvWork.CursorMax[0] && AdvWork.CursorFlag[0][row]) {
            AdvWork.Cursor[0] = row;
            return RX_PAD_CONFIRM;
        }
    }
    if (screen == RX_TITLE_DIFFICULTY && x >= 224 && x < 417 && y >= 304 && y < 400) {
        AdvWork.Cursor[1] = (int)((y - 304) / 32.0f);
        return RX_PAD_CONFIRM;
    }
    return 0;
}

int rx_title_menu_update(RxTitleScreen *screen)
{
    if (*screen == RX_TITLE_OPTIONS) return -1;
    unsigned press = Pad[0].press;
    if (*screen == RX_TITLE_PROMPT) {
        advance_prompt();
        if (press & (RX_PAD_START | AdvGetOkButton())) *screen = RX_TITLE_MENU;
        return -1;
    }

    int difficulty = *screen == RX_TITLE_DIFFICULTY;
    AdvMoveTitleCursor(difficulty, 0, difficulty);
    if (press & AdvGetCancelButton()) {
        *screen = difficulty ? RX_TITLE_MENU : RX_TITLE_PROMPT;
        if (*screen == RX_TITLE_PROMPT) {
            AdvWork.FlushCount = 0;
            AdvWork.FlushSpeed = 32.0f;
        }
    } else if (press & (RX_PAD_START | AdvGetOkButton())) {
        if (difficulty) {
            sys->gm_mode = AdvWork.Cursor[1];
            return sys->gm_mode;
        }
        if (AdvWork.Cursor[0] == 1) *screen = RX_TITLE_DIFFICULTY;
        else if (AdvWork.Cursor[0] == 2) {
            ResetOptionMenuParam(0);
            AdvWork.OptIndex = 0;
            *screen = RX_TITLE_OPTIONS;
        }
    }
    return -1;
}

int rx_title_options_draw(int interactive)
{
    int level = AdvWork.OptLevel;
    DisplayOptionBg(level, 1);
    int next = DisplayOptionPlate(0, level, interactive);
    if (!interactive || next == level) return 0;
    if (next < 0) return 1;
    ResetOptionMenuParam(1);
    AdvWork.OptLevel = next;
    return 0;
}
