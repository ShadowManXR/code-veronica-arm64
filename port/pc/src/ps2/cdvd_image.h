#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { CDVD_SECTOR_SIZE = 2048 };

typedef struct {
    FILE *stream;
    uint64_t bytes;
    uint32_t root_lsn, root_size;
} CdvdImage;

/* The caller keeps ownership of the stream. */
int cdvd_image_mount(CdvdImage *image, FILE *stream);
int cdvd_image_read(const CdvdImage *image, uint64_t offset, void *data, size_t bytes);
int cdvd_image_find(const CdvdImage *image, const char *path, uint32_t *lsn, uint32_t *size);
int cdvd_find_file(const char *path, uint32_t *lsn, uint32_t *size);
int cdvd_read_sectors(uint32_t lsn, uint32_t count, void *dst);

#ifdef RECVX_NATIVE_DISC
/* Attach or detach the image borrowed by the native libcdvd calls. */
void cdvd_attach_image(const CdvdImage *image);
#endif
