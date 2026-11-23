#include "disc.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t le16(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8;
}

static uint32_t le32(const uint8_t *p)
{
    return le16(p) | le16(p + 2) << 16;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) ++text;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    return text;
}

static int find_boot(RxAndroidDisc *disc)
{
    uint32_t lsn, size;
    char config[CDVD_SECTOR_SIZE];
    if (!cdvd_image_find(&disc->image, "SYSTEM.CNF", &lsn, &size) ||
        size >= sizeof(config) || !cdvd_image_read(&disc->image, (uint64_t)lsn * CDVD_SECTOR_SIZE, config, size) ||
        memchr(config, 0, size))
        return 0;
    config[size] = 0;

    char *state;
    for (char *line = strtok_r(config, "\r\n", &state); line; line = strtok_r(NULL, "\r\n", &state)) {
        char *value = strchr(line, '=');
        if (!value) continue;
        *value++ = 0;
        if (strcasecmp(trim(line), "BOOT2")) continue;
        const char *path = trim(value);
        return !strcasecmp(path, "cdrom0:\\SLUS_201.84;1") &&
               cdvd_image_find(&disc->image, path, &disc->boot_lsn, &disc->boot_size);
    }
    return 0;
}

static int check_elf(RxAndroidDisc *disc)
{
    uint64_t offset = (uint64_t)disc->boot_lsn * CDVD_SECTOR_SIZE;
    uint8_t header[52];
    // ELF32, little-endian, MIPS executable.
    if (disc->boot_size < sizeof(header) || !cdvd_image_read(&disc->image, offset, header, sizeof(header)) ||
        memcmp(header, "\177ELF\1\1\1", 7) || le16(header + 16) != 2 || le16(header + 18) != 8 ||
        le32(header + 20) != 1 || le16(header + 40) != sizeof(header) || le16(header + 42) != 32)
        return 0;
    uint32_t entry = le32(header + 24), programs = le32(header + 28), count = le16(header + 44);
    if (programs < sizeof(header) || programs > disc->boot_size || !count || count > 128 ||
        count > (disc->boot_size - programs) / 32)
        return 0;

    int contains_entry = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t program[32];
        if (!cdvd_image_read(&disc->image, offset + programs + i * sizeof(program), program, sizeof(program))) return 0;
        if (le32(program) != 1) continue; // PT_LOAD
        uint32_t start = le32(program + 4), address = le32(program + 8);
        uint32_t file_size = le32(program + 16), memory_size = le32(program + 20);
        if (start > disc->boot_size || file_size > disc->boot_size - start ||
            file_size > memory_size || (uint64_t)address + memory_size > UINT32_MAX + UINT64_C(1))
            return 0;
        if ((le32(program + 24) & 1) && entry >= address && entry - address < file_size) contains_entry = 1;
    }
    if (!contains_entry) return 0;
    disc->boot_entry = entry;
    return 1;
}

static int find_archive(const CdvdImage *image, const char *path, uint32_t *lsn, uint32_t *size)
{
    uint8_t header[8];
    return cdvd_image_find(image, path, lsn, size) && *size >= sizeof(header) &&
           cdvd_image_read(image, (uint64_t)*lsn * CDVD_SECTOR_SIZE, header, sizeof(header)) &&
           !memcmp(header, "AFS\0", 4) && le32(header + 4) &&
           le32(header + 4) <= (*size - sizeof(header)) / 8;
}

static FILE *open_iso(DIR *directory, const char *name)
{
    int fd = openat(dirfd(directory), name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return NULL;
    struct stat info;
    FILE *stream = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) ? fdopen(fd, "rb") : NULL;
    if (!stream) close(fd);
    return stream;
}

const char *rx_android_mount_disc(RxAndroidDisc *disc, const char *obb_directory)
{
    rx_android_unmount_disc(disc);
    DIR *directory = obb_directory ? opendir(obb_directory) : NULL;
    if (!directory) return "App OBB directory is unavailable";
    const char *error = "Place the SLUS-20184 ISO in the app's OBB directory";
    for (;;) {
        errno = 0;
        struct dirent *file = readdir(directory);
        if (!file) {
            if (errno) error = "Could not enumerate the app OBB directory";
            break;
        }
        size_t name_length = strlen(file->d_name);
        if (name_length <= 4 || strcasecmp(file->d_name + name_length - 4, ".iso")) continue;
        RxAndroidDisc candidate = {0};
        FILE *stream = open_iso(directory, file->d_name);
        if (!stream || !cdvd_image_mount(&candidate.image, stream) || !find_boot(&candidate) ||
            !check_elf(&candidate) ||
            !find_archive(&candidate.image, "SYSTEM.AFS", &candidate.system_lsn, &candidate.system_size) ||
            !find_archive(&candidate.image, "RDX_LNK.AFS", &candidate.rooms_lsn, &candidate.rooms_size)) {
            if (stream) fclose(stream);
            if (!disc->image.stream) error = "No readable SLUS-20184 ISO in the app's OBB directory";
            continue;
        }
        if (disc->image.stream) {
            fclose(stream);
            error = "Keep one SLUS-20184 ISO in the app's OBB directory";
            break;
        }
        *disc = candidate;
        error = NULL;
    }
    closedir(directory);
    if (error) rx_android_unmount_disc(disc);
    return error;
}

void rx_android_unmount_disc(RxAndroidDisc *disc)
{
    if (disc->image.stream) fclose(disc->image.stream);
    *disc = (RxAndroidDisc){0};
}
