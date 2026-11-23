/*
 * libcdvd on the host: reads the player's game disc image (ISO9660) in place.
 * Sector reads complete synchronously; the async API (sceCdRead + sceCdSync)
 * therefore always reports "done".
 */
#include "cdvd_image.h"
#ifdef RECVX_NATIVE_DISC
#include "ps2sdk_shim.h"
#else
#include "ps2_platform.h"
#endif
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

static CdvdImage iso;
static int cd_error;

#ifdef RECVX_NATIVE_DISC
void cdvd_attach_image(const CdvdImage *image)
{
    iso = image ? *image : (CdvdImage){0};
    cd_error = 0;
}
#else
int cdvd_open_image(const char* path)
{
    if (iso.stream) fclose(iso.stream);
    iso = (CdvdImage){0};
    FILE *stream = pc_fopen(path, "rb");
    if (!stream) return 0;
    if (!cdvd_image_mount(&iso, stream)) {
        fclose(stream);
        return 0;
    }
    return 1;
}
#endif

int cdvd_read_sectors(uint32_t lsn, uint32_t count, void* dst)
{
    if (!iso.stream || count > UINT32_MAX / CDVD_SECTOR_SIZE) return 0;
    unsigned bytes = count * CDVD_SECTOR_SIZE;
#ifndef RECVX_NATIVE_DISC
    { extern void pc_tex_invalidate(const void*, unsigned int); pc_tex_invalidate(dst, bytes); }
#endif
    return cdvd_image_read(&iso, (uint64_t)lsn * CDVD_SECTOR_SIZE, dst, bytes) ? (int)count : 0;
}

/* path like "\\PS2_DATA\\MODULES\\PADMAN.IRX;1" or "cdrom0:\\SYSTEM.AFS;1" */
int cdvd_find_file(const char* path, uint32_t* lsn, uint32_t* size)
{
    return cdvd_image_find(&iso, path, lsn, size);
}

/* ---------------- libcdvd API ---------------- */
int sceCdInit(int mode)
{
#ifdef RECVX_NATIVE_DISC
    return mode == SCECdEXIT || iso.stream != NULL;
#else
    (void)mode;
    ps2_poll_interrupts();
    return 1;
#endif
}
int sceCdMmode(int media) { (void)media; return 1; }
int sceCdDiskReady(int mode)
{
    (void)mode;
#ifdef RECVX_NATIVE_DISC
    if (!iso.stream) return 6; /* SCECdNotReady */
#endif
    return 2; /* SCECdComplete */
}
int sceCdSync(int mode)
{
    (void)mode;
#ifndef RECVX_NATIVE_DISC
    ps2_poll_interrupts();
#endif
    return 0;
}
int sceCdGetError(void)   { return cd_error; }
int sceCdBreak(void)      { return 1; }
int sceCdPause(void)      { return 1; }

int sceCdSearchFile(sceCdlFILE* fp, const char* name)
{
    uint32_t lsn, size;
    if (!cdvd_find_file(name, &lsn, &size)) {
#ifndef RECVX_NATIVE_DISC
        host_log("cdvd: file not found: %s\n", name);
#endif
        return 0;
    }
    memset(fp, 0, sizeof(*fp));
    fp->lsn = lsn; fp->size = size;
    {
        const char* base = strrchr(name, '\\');
        base = base ? base + 1 : name;
        size_t length = strlen(base);
        if (length >= sizeof(fp->name)) length = sizeof(fp->name) - 1;
        memcpy(fp->name, base, length);
    }
    return 1;
}

int sceCdRead(u_int lbn, u_int sectors, void* buf, sceCdRMode* mode)
{
    (void)mode;
    cd_error = cdvd_read_sectors(lbn, sectors, buf) ? 0 : 0x30 /* SCECdErREAD */;
#ifndef RECVX_NATIVE_DISC
    ps2_poll_interrupts();
#endif
    return 1;
}

int sceCdReadClock(sceCdCLOCK* rtc)
{
    time_t t = time(NULL);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
#define BCD(v) (u_char)((((v) / 10) << 4) | ((v) % 10))
    rtc->stat = 0;
    rtc->second = BCD(tmv.tm_sec);
    rtc->minute = BCD(tmv.tm_min);
    rtc->hour = BCD(tmv.tm_hour);
    rtc->pad = 0;
    rtc->day = BCD(tmv.tm_mday);
    rtc->month = BCD(tmv.tm_mon + 1);
    rtc->year = BCD(tmv.tm_year % 100);
#undef BCD
    return 1;
}

/* ---------------- streaming (sceCdSt*): sequential reads for movies ---------------- */
static uint32_t st_lsn;

int sceCdStInit(u_int bufmax, u_int bankmax, u_int iop_bufaddr) { (void)bufmax; (void)bankmax; (void)iop_bufaddr; return 1; }
int sceCdStStart(u_int lbn, sceCdRMode* mode) { (void)mode; st_lsn = lbn; return 1; }
int sceCdStStop(void) { return 1; }
int sceCdStRead(u_int size, u_int* buf, u_int mode, u_int* err)
{
    (void)mode;
    int n = cdvd_read_sectors(st_lsn, size, buf);
    st_lsn += (uint32_t)n;
    if (err) *err = n ? 0 : 0x32; /* SCECdErEOM */
    return n;
}
