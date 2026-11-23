#include "boot_disc.h"
#include "adxwrap.h"
#include "gdlib.h"
#include "ps2_sg_gd.h"
#include "ps2_sg_maloc.h"
#ifdef RECVX_NATIVE_MOVIE
#include "movie_port.h"
#include "sdfunc.h"
#endif

#include <string.h>

// Partition IDs from SoundAfsPatDef in sdfunc.c.
static char inventory_archive[]="ITEM1.AFS";
static AFS_PATINFO boot_partitions[] = {
    {"ADV.AFS", 3, 128, NULL},
    {"SYSTEM.AFS", 6, 256, NULL},
    {inventory_archive, 4, 512, NULL},
    {0}
};

static uint32_t read_le32(const unsigned char *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

const unsigned char *rx_boot_tim2(const unsigned char *data, size_t size, unsigned resource, size_t *remaining)
{
    size_t index = (size_t)resource * 4;
    if (index + 4 > size) return NULL;
    uint32_t offset = read_le32(data + index);
    if (offset < index + 4 || offset > size || size - offset < 8) return NULL;
    uint32_t texture = read_le32(data + offset + 4);
    if (texture < 8 || texture > size - offset - 4 ||
        memcmp(data + offset + texture, "TIM2", 4) != 0) return NULL;
    *remaining = size - offset - texture;
    return data + offset + texture;
}

unsigned char *rx_boot_read_member(unsigned partition, unsigned member, unsigned *size)
{
    int slot = OpenAfsInsideFile(partition, member);
    if (slot < 0) return 0;
    int length = GetAfsInsideFileSize(slot);
    if (length < 8) {
        CloseAfsInsideFile(slot);
        return 0;
    }

    // The original ADXF wrapper transfers whole sectors.
    size_t allocation = ((size_t)length + CDVD_SECTOR_SIZE - 1) & ~(size_t)(CDVD_SECTOR_SIZE - 1);
    unsigned char *data = syMalloc((unsigned)allocation);
    if (!data) {
        CloseAfsInsideFile(slot);
        return 0;
    }
    RequestReadAfsInsideFile(slot, data);
    if (!CheckReadEndAfsInsideFile(slot)) {
        CloseAfsInsideFile(slot);
        syFree(data);
        return NULL;
    }
    *size = (unsigned)length;
    return data;
}

unsigned char *rx_boot_read_file(const char *name,unsigned *size)
{
    if(!name||!size)return NULL;
    int length=GetFileSize((char*)name);
    if(length<8||length>64*1024*1024)return NULL;
    unsigned allocation=((unsigned)length+CDVD_SECTOR_SIZE-1)&~(CDVD_SECTOR_SIZE-1);
    unsigned char *data=syMalloc(allocation);if(!data)return NULL;
    if(ReadFileEx((char*)name,data)!=0){syFree(data);return NULL;}
    *size=(unsigned)length;return data;
}

static unsigned check_boot_member(unsigned partition, unsigned member)
{
    unsigned length;
    unsigned char *data = rx_boot_read_member(partition, member, &length);
    if (!data) return 0;
    int valid;
    if (partition == 6) {
        // bhSetFontTexture consumes the packed effect/font bank.
        uint32_t first_block = read_le32(data);
        valid = first_block && first_block <= length - 4;
    } else {
        size_t remaining;
        // The two textures selected by Adv_FirstWarningMessage.
        valid = rx_boot_tim2(data, length, 2, &remaining) && rx_boot_tim2(data, length, 3, &remaining);
    }
    syFree(data);
    return valid ? (unsigned)length : 0;
}

void rx_android_stop_disc(void)
{
#ifdef RECVX_NATIVE_MOVIE
    PlayStopMovieEx(0);rx_game_movie_bind(NULL);
#endif
    DeletePartitionEx(boot_partitions);
    ADXF_Finish();
    ExitGdSystem();
    syMallocFinish();
    cdvd_attach_image(NULL);
}

const char *rx_android_start_disc(const RxAndroidDisc *disc, RxBootDisc *boot)
{
    rx_android_stop_disc();
    *boot = (RxBootDisc){0};
    cdvd_attach_image(&disc->image);
#ifdef RECVX_NATIVE_MOVIE
    rx_game_movie_bind(&disc->image);
#endif
    // The host owns startup until sbInitSystem is linked as a whole.
    syMallocInit(NULL, 0);
    // Match the directory capacity used by the original sbInitSystem.
    if (InitGdSystemEx(256) != 0) {
        rx_android_stop_disc();
        return "Original GDFS disc initialization failed";
    }

    unsigned char sector[CDVD_SECTOR_SIZE];
    const char descriptor[] = "Game Disc Management System";
    int size = GetFileSize("DISCID.BIN");
    if (size != (int)(4 + sizeof(descriptor)) || ReadFileEx("DISCID.BIN", sector) != 0 ||
        memcmp(sector + 4, descriptor, sizeof(descriptor))) {
        rx_android_stop_disc();
        return "Original GDFS could not read a valid DISCID.BIN";
    }
    unsigned disc_id = sector[0];
    if (GetFileSize("SYSTEM.AFS") != (int)disc->system_size) {
        rx_android_stop_disc();
        return "Original GDFS system archive does not match the mounted disc";
    }

    GDFS room = gdFsOpen("RM_0000.RDX", NULL);
    Sint32 room_size;
    int room_ok = room && gdFsGetFileSize(room, &room_size) && room_size >= CDVD_SECTOR_SIZE &&
                  gdFsRead(room, 1, sector) == 0;
    if (room) gdFsClose(room);
    if (!room_ok) {
        rx_android_stop_disc();
        return "Original GDFS could not read the linked opening room";
    }
    // Original MountSoundAfs selects suffix 1/2 from the low disc bit.
    boot_partitions[2].AfsFileName[4]=(char)('1'+(disc_id&1));
    if (CreatePartitionEx(boot_partitions) != 0) {
        rx_android_stop_disc();
        return "Original ADXF could not mount the boot archives";
    }
    unsigned font = check_boot_member(6, 1);
    unsigned warning = check_boot_member(3, 0);
    unsigned title = 0;
    unsigned char *title_package = rx_boot_read_member(3, 2, &title);
    size_t remaining;
    if (!font || !warning || !title_package ||
        !rx_boot_tim2(title_package, title, 0, &remaining) ||
        !rx_boot_tim2(title_package, title, 2, &remaining)) {
        rx_android_stop_disc();
        if (!font) return "Could not read the original startup font bank";
        if (!warning) return "Could not read the original warning package";
        return "Could not read the original title package";
    }
    unsigned options = 0;
    unsigned char *options_package = rx_boot_read_member(3, 4, &options);
    if (!options_package || !rx_boot_tim2(options_package, options, 2, &remaining) ||
        !rx_boot_tim2(options_package, options, 3, &remaining) ||
        !rx_boot_tim2(options_package, options, 4, &remaining)) {
        rx_android_stop_disc();
        return "Could not read the original Options package";
    }
    *boot = (RxBootDisc){disc_id, (unsigned)room_size, font, warning, title, title_package,
                         options, options_package};
    return NULL;
}
