#pragma once

#include "disc.h"

typedef struct {
    unsigned disc_id;
    unsigned opening_room_size;
    unsigned font_size;
    unsigned warning_size;
    unsigned title_size;
    const unsigned char *title_package;
    unsigned options_size;
    const unsigned char *options_package;
} RxBootDisc;

const char *rx_android_start_disc(const RxAndroidDisc *disc, RxBootDisc *boot);
void rx_android_stop_disc(void);
/* The caller releases the archive buffer with syFree. */
unsigned char *rx_boot_read_member(unsigned partition, unsigned member, unsigned *size);
unsigned char *rx_boot_read_file(const char *name,unsigned *size);
const unsigned char *rx_boot_tim2(const unsigned char *data, size_t size, unsigned resource, size_t *remaining);
