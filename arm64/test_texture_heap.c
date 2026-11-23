#include "ps2_NaTextureFunction.h"
#include "ps2_texture.h"
#include "ps2_dummy.h"
#include "room.h"
#include "main.h"
#include "ps2_pxlconv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int ok, const char *reason)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", reason);
        exit(1);
    }
}

void check_texture_gpu(NJS_TEXLIST *list, unsigned index, const TIM2_PICTUREHEADER_EX *disc);
void check_room_gpu(const void *rdt,size_t bytes);

static uint32_t word(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void check_room_texture_load(const unsigned char *rdt, size_t bytes)
{
    static O_WRK props[32], items[32];
    ROM_WORK room = {0};
    unsigned char *prepared = malloc(bytes);
    check(prepared != NULL, "cannot allocate room texture fixture");
    memcpy(prepared, rdt, bytes);

    rom = &room;
    sys->obwp = props;
    sys->itwp = items;
    sys->rdtp = prepared;
    sys->rdtsz = (unsigned)bytes;
    sys->ef_tlist = (NJS_TEXLIST){sys->ef_tex, 0};
    sys->ef_ct = sys->ef_extn = 0;
    sys->ef_pbnk = 32;
    sys->error = 0;
    bhSetRDT();
    check(!(sys->error & 1) && rom->mdl.objP && sys->txr_n && sys->ef_ct,
          "original bhSetRDT did not load the opening room and effect textures");
    unsigned resident = sys->ef_tlist.nbTexture;
    for (int i = 0; i < sys->txr_n; ++i) {
        NJS_TEXLIST *list = sys->txlp[i];
        int loaded = bhSetMemPvpTexture(list, sys->txdp[i], sys->txloff[i]);
        check(loaded >= 0 && (unsigned)loaded == list->nbTexture,
              "original room texture queue did not fill the expected slots");
        resident += (unsigned)loaded;
    }
    check(ring_check() == 1 && resident >= 32, "opening room texture ring invalid");
    check_room_gpu(rdt,bytes);
    for (int i = 0; i < sys->txr_n; ++i) njReleaseTexture(sys->txlp[i]);
    njReleaseTexture(&sys->ef_tlist);
    check(Ps2TextureGarbageCollectionAll() == 1 && ring_check() == 1, "room texture retirement failed");
    bhInitRoom();
    check(!sys->lgtp && !sys->lg_pnt, "room teardown retained active lighting");
    rom = NULL;
    sys->rdtp = sys->memp = NULL;
    free(prepared);
    printf("PASS original bhSetRDT and opening room CPU texture queue (%u texture references)\n", resident);
}

void check_texture_heap(const unsigned char *rdt, size_t bytes)
{
    size_t directory = word(rdt + 32);
    size_t start = word(rdt + directory + 4);
    size_t end = word(rdt + directory + 8);
    check(start < end && end <= bytes, "missing opening PVP fixture");
    size_t block_size = end - start;
    unsigned char *pvp = malloc(block_size);
    unsigned char *heap = malloc(10 * 1024 * 1024);
    NJS_TEXMEMLIST *pool = calloc(128, sizeof(*pool));
    Ps2_PXLCONV = malloc(1024 * 1024);
    check(pvp && heap && pool && (uintptr_t)pool > UINT32_MAX &&
          (uintptr_t)heap > UINT32_MAX && Ps2_PXLCONV, "cannot initialize high-address texture work");
    memcpy(pvp, rdt + start, block_size);
    TIM2_PICTUREHEADER_EX *pictures[32] = {0};
    const TIM2_PICTUREHEADER_EX *original[32] = {0};
    size_t cursor = 0, count = 0;
    while (cursor + 4 <= block_size && word(pvp + cursor) != UINT32_MAX) {
        check(count < 32 && block_size - cursor >= 32 + sizeof(TIM2_PICTUREHEADER_EX) &&
              !memcmp(pvp + cursor, "TIM2", 4), "unexpected opening root PVP packet");
        size_t size = word(pvp + cursor + 4);
        check(size <= block_size - cursor - 32, "opening PVP packet exceeds fixture");
        TIM2_PICTUREHEADER_EX *picture = (TIM2_PICTUREHEADER_EX *)(pvp + cursor + 32);
        check(isVQ(picture->PictFormat) && Ps2GetTim2Size(picture) <= size,
              "opening PVP expects a different CPU test path");
        check(!picture->ClutChange, "expected original unconverted TIM2");
        original[count] = (const TIM2_PICTUREHEADER_EX *)(rdt + start + cursor + 32);
        pictures[count++] = picture;
        cursor += size + 32;
    }
    check(count == 32 && cursor + 4 <= block_size, "opening PVP texture count differs");

    Ps2_tex_buff = heap;
    njInitTexture(pool, 128);
    NJS_TEXNAME names[32] = {0};
    NJS_TEXLIST list = {names, 32};
    check(bhSetMemPvpTexture(&list, pvp, 0) == 32 && ring_check() == 1,
          "opening PVP did not load into the native texture heap");
    for (size_t i = 0; i < count; ++i) {
        const NJS_TEXMEMLIST *entry = (const NJS_TEXMEMLIST *)names[i].texaddr;
        unsigned char bank;
        check(njRoomTextureBank(&names[i], &bank) && bank == (unsigned char)entry->bank &&
              entry->globalIndex == pictures[i]->Gindex && entry->count != 0,
              "loaded texture reference or bank differs");
        const TIM2_PICTUREHEADER_EX *resident = (const TIM2_PICTUREHEADER_EX *)entry->texinfo.texsurface.pSurface;
        size_t size = (size_t)Ps2GetTim2Size(pictures[i]);
        check(resident->ClutChange == 0x8000 && (uintptr_t)resident > UINT32_MAX && resident->admin.size == size &&
              resident->ClampFlag == pictures[i]->ClampFlag &&
              !memcmp((const unsigned char *)resident + 256,
                      (const unsigned char *)pictures[i] + 256, size - 256),
              "TIM2 pixels changed or the resident header shifted");
    }
    check(memcmp((const unsigned char *)pictures[0] + 256,
                 (const unsigned char *)original[0] + 256, original[0]->ImageSize) != 0,
          "large texture did not execute original indexed conversion");
    for (size_t i = 0; i < count; ++i) check_texture_gpu(&list, (unsigned)i, original[i]);

    NJS_TEXLIST first = {names, 1};
    NJS_TEXLIST remaining = {names + 1, 31};
    NJS_TEXMEMLIST *second = (NJS_TEXMEMLIST *)names[1].texaddr;
    const unsigned char *before = (const unsigned char *)second->texinfo.texsurface.pSurface;
    check(njReleaseTexture(&first) == 1 && Ps2TextureGarbageCollectionAll() == 1 && ring_check() == 1 &&
          (const unsigned char *)second->texinfo.texsurface.pSurface == heap && before != heap,
          "texture heap did not compact into the released slot");

    NJS_TEXLIST single = {names + 1, 1};
    size_t save_size = second->texinfo.texsurface.TextureSize;
    char *saved = malloc(save_size);
    check(saved != NULL, "cannot allocate texture save fixture");
    check(bhCopyTexmem2Mainmem(&single, saved) == saved + save_size, "texture save width differs");
    bhCopyMainmem2Texmem(&single);
    second = (NJS_TEXMEMLIST *)names[1].texaddr;
    check((uintptr_t)second > UINT32_MAX && second->globalIndex == pictures[1]->Gindex &&
          !memcmp((const unsigned char *)second->texinfo.texsurface.pSurface + 256, saved + 256, save_size - 256),
          "restored texture lost its native pointer or pixels");
    check(njReleaseTexture(&remaining) == 1 && Ps2TextureGarbageCollectionAll() == 1 && ring_check() == 1,
          "texture heap release left an invalid ring");
    njInitTexture(pool, 128);
    check_room_texture_load(rdt, bytes);
    Ps2_tex_buff = NULL;
    free(saved);
    free(pool);
    free(heap);
    free(pvp);
    free(Ps2_PXLCONV);
    Ps2_PXLCONV = NULL;
    puts("PASS original CPU texture load, lookup, release, compaction and save/restore above 4 GiB");
}
