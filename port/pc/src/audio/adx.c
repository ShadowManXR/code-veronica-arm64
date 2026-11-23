/*
 * CRI ADX middleware (ADXF file system + ADXT stream player) on the host.
 *
 * ADXF: AFS archive partitions and plain files, read from the disc image.
 * ADXT: stream state machine with the same status/time behaviour as the
 *       original (the game waits on ADXT_STAT_PLAYEND for voice lines), driven
 *       by the audio mixer when it plays the stream, or by wall-clock time.
 */
#include "../ps2/cdvd_image.h"
#ifdef RECVX_NATIVE_DISC
#include <android/log.h>
#define host_log(...) __android_log_print(ANDROID_LOG_WARN, "RECVX-ADXF", __VA_ARGS__)
#else
#include "../ps2_platform.h"
#include "audio.h"
#include <SDL3/SDL.h>
#endif
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define ADXF_STAT_STOP    1
#define ADXF_STAT_READING 2
#define ADXF_STAT_READEND 3
#define ADXF_STAT_ERROR   4

#define ADXT_STAT_STOP    0
#define ADXT_STAT_DECINFO 1
#define ADXT_STAT_PREP    2
#define ADXT_STAT_PLAYING 3
#define ADXT_STAT_DECEND  4
#define ADXT_STAT_PLAYEND 5
#define ADXT_STAT_ERROR   6

#define SECTOR 2048

/* ---------------- mods folder ----------------
   Files in $RECVX_MODS (default ./mods) replace disc files without touching the image
   (docs/modding/README.md "The mods folder"): <mods>/NAME for files opened by name (room files RM_SRRC.RDX, also names the
   disc does not have, and plain ISO files), <mods>/<AFS file>/<id> for entries of AFS archives
   (e.g. mods/MRY.AFS/12). */
#ifndef RECVX_NATIVE_DISC
static FILE* mod_open_path(const char* name, uint32_t* size, char* path, size_t npath)
{
    static const char* dir;
    FILE* f;
    if (!dir) dir = SDL_getenv("RECVX_MODS") ? SDL_getenv("RECVX_MODS") : "mods";
    while (*name == '\\' || *name == '/') name++;
    snprintf(path, npath, "%s/%s", dir, name);
    if (!(f = pc_fopen(path, "rb"))) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (uint32_t)ftell(f);
    host_log("mods: %s replaces the disc file (%u bytes)\n", path, *size);
    return f;
}
static FILE* mod_open(const char* name, uint32_t* size) { char path[512]; return mod_open_path(name, size, path, sizeof path); }

int pc_mod_file_size(const char* name)
{
    uint32_t size;
    FILE* f = mod_open(name, &size);
    if (!f) return -1;
    fclose(f);
    return (int)size;
}

/* Optional room-bank dependency, authored text only: RM_1150.SPQ.key contains
   a decimal retail key such as 60 (RM_0060). No recursive aliases. */
int pc_mod_room_sound_key(int key)
{
    const char* dir = SDL_getenv("RECVX_MODS");
    char path[512], text[32], *end;
    long donor;
    FILE* f;
    if (key < 0 || key > 9999) return key;
    if (snprintf(path, sizeof(path), "%s/RM_%04d.SPQ.key",
                 dir ? dir : "mods", key) >= (int)sizeof(path)) return key;
    f = pc_fopen(path, "r");
    if (!f) return key;
    if (!fgets(text, sizeof(text), f)) { fclose(f); return key; }
    fclose(f);
    donor = strtol(text, &end, 10);
    if (end == text || donor < 0 || donor > 9999) return key;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end) return key;
    host_log("mods: room sound key %04d uses %04ld\n", key, donor);
    return (int)donor;
}
#endif

/* ---------------- AFS partitions ---------------- */
#define MAX_PT 32
typedef struct { uint32_t offset, size; } AfsEntry;
typedef struct { uint32_t lsn, count; AfsEntry* entries; char name[32]; } Partition;
static Partition pt[MAX_PT];

static uint32_t afs_le32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static int load_partition(int ptid, const char* fname)
{
    uint32_t lsn, size;
    uint8_t sec[SECTOR];
    if (ptid < 0 || ptid >= MAX_PT) return 0;
    if (!cdvd_find_file(fname, &lsn, &size)) { host_log("adxf: partition file %s not found\n", fname); return 0; }
    if (size < 8 || !cdvd_read_sectors(lsn, 1, sec) || memcmp(sec, "AFS", 3) != 0) {
        host_log("adxf: %s is not AFS\n", fname);
        return 0;
    }
    uint32_t count = afs_le32(sec + 4);
    if (!count || count > (size - 8) / 8 || size > INT_MAX - SECTOR) return 0;
    uint32_t toc_bytes = 8 + count * 8;
    uint32_t sectors = (toc_bytes + SECTOR - 1) / SECTOR;
    Partition next = {.lsn = lsn, .count = count};
    uint8_t* toc = malloc((size_t)sectors * SECTOR);
    if (!toc) return 0;
    if (cdvd_read_sectors(lsn, sectors, toc) == (int)sectors)
        next.entries = malloc((size_t)count * sizeof(*next.entries));
    if (!next.entries) {
        free(toc);
        return 0;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset = afs_le32(toc + 8 + i * 8);
        uint32_t length = afs_le32(toc + 12 + i * 8);
        if (offset % SECTOR || offset > size || length > size - offset ||
            (length && offset < toc_bytes) || offset / SECTOR > UINT32_MAX - lsn) {
            free(toc);
            free(next.entries);
            return 0;
        }
        next.entries[i] = (AfsEntry){offset, length};
    }
    free(toc);
    while (*fname == '\\') fname++;
    snprintf(next.name, sizeof(next.name), "%s", fname);
    free(pt[ptid].entries);
    pt[ptid] = next;
    return 1;
}

int adx_afs_locate(int ptid, int fid, uint32_t* lsn, uint32_t* size)
{
    if (ptid < 0 || ptid >= MAX_PT || !pt[ptid].entries || fid < 0 || (uint32_t)fid >= pt[ptid].count) return 0;
    const AfsEntry* entry = &pt[ptid].entries[fid];
    *lsn = pt[ptid].lsn + entry->offset / SECTOR;
    *size = entry->size;
    return 1;
}

/* ---------------- ADXF ---------------- */
typedef struct { int used; uint32_t lsn, size, pos_sct; int stat; FILE* host; } AdxfFile;
static AdxfFile files[32];
static int pt_stat[MAX_PT];

void ADXF_Init(void) {}
void ADXF_Finish(void)
{
    for (size_t i = 0; i < sizeof(files) / sizeof(*files); ++i)
        if (files[i].host) fclose(files[i].host);
    for (int i = 0; i < MAX_PT; ++i) free(pt[i].entries);
    memset(files, 0, sizeof(files));
    memset(pt, 0, sizeof(pt));
    memset(pt_stat, 0, sizeof(pt_stat));
}

int ADXF_LoadPartitionNw(int ptid, const char* fname, void* dir, void* ptinfo)
{
    (void)dir; (void)ptinfo;
    pt_stat[ptid & (MAX_PT - 1)] = load_partition(ptid, fname) ? ADXF_STAT_READEND : ADXF_STAT_ERROR;
    return 0;
}
int ADXF_GetPtStat(int ptid) { return pt_stat[ptid & (MAX_PT - 1)]; }

static AdxfFile* new_file(void)
{
    for (int i = 0; i < 32; i++) if (!files[i].used) { memset(&files[i], 0, sizeof(files[i])); files[i].used = 1; files[i].stat = ADXF_STAT_STOP; return &files[i]; }
    host_log("adxf: out of handles\n");
    return NULL;
}

#ifndef RECVX_NATIVE_DISC
static AdxfFile* open_host(FILE* hf, uint32_t size)
{
    AdxfFile* f = new_file();
    if (!f) { fclose(hf); return NULL; }
    f->host = hf; f->size = size;
    return f;
}

/* a file of the mods folder by name, NULL if there is none (adxwrap.c OpenAfsIsoFile) */
void* pc_mod_open(const char* name)
{
    uint32_t size;
    FILE* hf = mod_open(name, &size);
    return hf ? open_host(hf, size) : NULL;
}
#endif

void* ADXF_OpenAfs(int ptid, int flid)
{
    uint32_t lsn, size;
    AdxfFile* f;
#ifndef RECVX_NATIVE_DISC
    FILE* hf;
    char name[64];
    if (ptid >= 0 && ptid < MAX_PT && pt[ptid].name[0]) {
        snprintf(name, sizeof(name), "%s/%d", pt[ptid].name, flid);
        if ((hf = mod_open(name, &size))) return open_host(hf, size);
    }
#endif
    if (!adx_afs_locate(ptid, flid, &lsn, &size)) { host_log("adxf: no file %d in partition %d\n", flid, ptid); return NULL; }
    if (!(f = new_file())) return NULL;
    f->lsn = lsn; f->size = size;
    return f;
}

void* ADXF_Open(const char* fname, void* atr)
{
    uint32_t lsn, size;
    AdxfFile* f;
    (void)atr;
#ifndef RECVX_NATIVE_DISC
    if ((f = pc_mod_open(fname))) return f;
#endif
    if (!cdvd_find_file(fname, &lsn, &size)) { host_log("adxf: cannot open %s\n", fname); return NULL; }
    if (!(f = new_file())) return NULL;
    f->lsn = lsn; f->size = size;
    return f;
}

void ADXF_Close(void* h)
{
    AdxfFile* f = h;
    if (!f) return;
    if (f->host) fclose(f->host);
    f->host = NULL;
    f->used = 0;
}
int ADXF_GetFsizeByte(void* h) { return h ? (int)((AdxfFile*)h)->size : 0; }
int ADXF_GetFsizeSct(void* h)
{
    uint32_t size = h ? ((AdxfFile*)h)->size : 0;
    return (int)(size / SECTOR + (size % SECTOR != 0));
}
int ADXF_GetStat(void* h) { return h ? ((AdxfFile*)h)->stat : ADXF_STAT_ERROR; }
int ADXF_Stop(void* h) { if (h) ((AdxfFile*)h)->stat = ADXF_STAT_STOP; return 0; }

int ADXF_ReadNw32(void* h, int nsct, void* buf)
{
    AdxfFile* f = h;
    if (!f || !f->used) return 0;
    uint32_t total_sectors = (uint32_t)ADXF_GetFsizeSct(f);
    if (nsct < 0 || (nsct && !buf) || f->pos_sct > total_sectors ||
        f->pos_sct > UINT32_MAX - f->lsn ||
        (uint32_t)nsct > total_sectors - f->pos_sct) {
        f->stat = ADXF_STAT_ERROR;
        return 0;
    }
    if (!nsct) {
        f->stat = ADXF_STAT_READEND;
        return 0;
    }
    if (f->host) {   /* whole sectors, as from the disc: the tail of the last one is zero */
        size_t want = (size_t)nsct * SECTOR, got;
        fseek(f->host, (long)f->pos_sct * SECTOR, SEEK_SET);
        got = fread(buf, 1, want, f->host);
        memset((char*)buf + got, 0, want - got);
        f->stat = ADXF_STAT_READEND;
    } else
    f->stat = cdvd_read_sectors(f->lsn + f->pos_sct, (uint32_t)nsct, buf) ? ADXF_STAT_READEND : ADXF_STAT_ERROR;
    if (f->stat == ADXF_STAT_ERROR) return 0;
    f->pos_sct += (uint32_t)nsct;
#ifndef RECVX_NATIVE_DISC
    ps2_poll_interrupts();
#endif
    return nsct;
}
int ADXF_ReadNw(void* h, int nsct, void* buf) { return ADXF_ReadNw32(h, nsct, buf); }

/* ---------------- ADXT ---------------- */
#ifndef RECVX_NATIVE_DISC
#define MAX_ADXT 8
typedef struct {
    int used, stat, paused, loop;
    int vol;                 /* 0.1 dB, 0 = max, -999 = mute */
    uint32_t rate, total, channels;
    uint64_t start_ns, pause_ns, paused_total_ns;
    int voice;               /* mixer voice, -1 if none */
} AdxTalk;
static AdxTalk talk[MAX_ADXT];
static int output_mono;

void ADXT_Init(void) {}
void ADXT_Finish(void) {}
void ADXT_SetupHostFs(void* prm) { (void)prm; }
void ADXT_SetupDvdFs(void* prm) { (void)prm; }
void ADXT_SetNumRetry(int n) { (void)n; }
void ADXT_SetOutputMono(int flag) { output_mono = flag; audio_set_mono(flag); }
void ADXPS2_SetupThrd(void* p) { (void)p; }
void ADXPS2_Lock(void) {}
void ADXPS2_Unlock(void) {}

void* ADXT_Create(int maxnch, void* work, int worksize)
{
    (void)maxnch; (void)work; (void)worksize;
    for (int i = 0; i < MAX_ADXT; i++) if (!talk[i].used) {
        memset(&talk[i], 0, sizeof(talk[i]));
        talk[i].used = 1; talk[i].stat = ADXT_STAT_STOP; talk[i].voice = -1;
        return &talk[i];
    }
    return NULL;
}

void ADXT_Stop(void* h)
{
    AdxTalk* t = h;
    if (!t) return;
    if (t->voice >= 0) { audio_stream_stop(t->voice); t->voice = -1; }
    t->stat = ADXT_STAT_STOP;
}

void ADXT_Destroy(void* h) { AdxTalk* t = h; if (!t) return; ADXT_Stop(h); t->used = 0; }

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

void ADXT_StartAfs(void* h, int ptid, int fid)
{
    AdxTalk* t = h;
    uint32_t lsn = 0, size;
    uint8_t hdr[SECTOR] = { 0 };
    char name[64], path[512];
    FILE* hf = NULL;
    int ok, mod = 0;
    if (!t) return;
    ADXT_Stop(h);
    if (ptid >= 0 && ptid < MAX_PT && pt[ptid].name[0]) {   /* <mods>/<AFS file>/<id>, as ADXF_OpenAfs */
        snprintf(name, sizeof(name), "%s/%d", pt[ptid].name, fid);
        hf = mod_open_path(name, &size, path, sizeof path);
    }
    if (hf) { rewind(hf); ok = fread(hdr, 1, SECTOR, hf) >= 0x34; fclose(hf); mod = 1; }
    else ok = adx_afs_locate(ptid, fid, &lsn, &size) && cdvd_read_sectors(lsn, 1, hdr);
    if (!ok || hdr[0] != 0x80) {
        host_log("adxt: cannot start %d/%d\n", ptid, fid);
        t->stat = ADXT_STAT_ERROR;
        return;
    }
    t->channels = hdr[7];
    t->rate = be32(hdr + 8);
    t->total = be32(hdr + 12);
    /* loop flag: version 3 header at 0x18, version 4 at 0x24 */
    t->loop = (hdr[0x12] == 3) ? be32(hdr + 0x18) != 0 : (hdr[0x12] == 4) ? be32(hdr + 0x24) != 0 : 0;
    t->start_ns = SDL_GetTicksNS();
    t->paused_total_ns = 0;
    t->paused = 0;
    t->stat = ADXT_STAT_PLAYING;
    t->voice = audio_stream_start_adx(mod ? path : g_config.iso_path, mod ? 0 : (uint64_t)lsn * SECTOR, size, t->vol);
    host_log("adxt: start %d/%d (%u Hz, %u ch, %u samples, loop %d) voice %d\n", ptid, fid, t->rate, t->channels, t->total, t->loop, t->voice);
}

static uint64_t played_samples(AdxTalk* t)
{
    uint64_t now = t->paused ? t->pause_ns : SDL_GetTicksNS();
    if (t->voice >= 0) return audio_stream_position(t->voice);
    return (now - t->start_ns - t->paused_total_ns) * t->rate / 1000000000ull;
}

int ADXT_GetStat(void* h)
{
    AdxTalk* t = h;
    if (!t) return ADXT_STAT_ERROR;
    if (t->stat == ADXT_STAT_PLAYING && !t->loop && t->rate) {
        int done = t->voice >= 0 ? audio_stream_finished(t->voice) : played_samples(t) >= t->total;
        if (done) t->stat = ADXT_STAT_PLAYEND;
    }
    return t->stat;
}

void ADXT_GetTime(void* h, int* ncount, int* tscale)
{
    AdxTalk* t = h;
    if (!t || t->stat < ADXT_STAT_PLAYING) { *ncount = 0; *tscale = 1; return; }
    *ncount = (int)played_samples(t);
    *tscale = (int)(t->rate ? t->rate : 1);
}

/* ADXT_Pause is provided by the decomp (ps2_cri_adxt.c): it is empty on the retail PS2 build. */

void ADXT_SetOutVol(void* h, int vol)
{
    AdxTalk* t = h;
    if (!t) return;
    t->vol = vol;
    if (t->voice >= 0) audio_stream_volume(t->voice, vol);
}

void ADXT_SetSvrFreq(void* h, int f) { (void)h; (void)f; }
void ADXT_SetReloadSct(void* h, int n) { (void)h; (void)n; }
void ADXT_SetAutoRcvr(void* h, int m) { (void)h; (void)m; }
#endif
