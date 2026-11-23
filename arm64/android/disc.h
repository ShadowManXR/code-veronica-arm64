#pragma once

#include "ps2/cdvd_image.h"

typedef struct {
    CdvdImage image;
    uint32_t boot_lsn, boot_size, boot_entry;
    uint32_t system_lsn, system_size, rooms_lsn, rooms_size;
} RxAndroidDisc;

/* On success the caller owns the mounted disc. Failures leave it empty. */
const char *rx_android_mount_disc(RxAndroidDisc *disc, const char *obb_directory);
void rx_android_unmount_disc(RxAndroidDisc *disc);
