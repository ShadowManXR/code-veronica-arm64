#include "cdvd_image.h"

#include <string.h>

#ifdef _WIN32
#define CDVD_SEEK _fseeki64
#define CDVD_TELL _ftelli64
#else
#define CDVD_SEEK fseeko
#define CDVD_TELL ftello
#endif

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int contains(const CdvdImage *image, uint64_t offset, uint64_t bytes)
{
    return offset <= image->bytes && bytes <= image->bytes - offset;
}

int cdvd_image_read(const CdvdImage *image, uint64_t offset, void *data, size_t bytes)
{
    return image->stream && contains(image, offset, bytes) &&
           CDVD_SEEK(image->stream, (int64_t)offset, SEEK_SET) == 0 &&
           fread(data, 1, bytes, image->stream) == bytes;
}

int cdvd_image_mount(CdvdImage *image, FILE *stream)
{
    if (!stream || CDVD_SEEK(stream, 0, SEEK_END) != 0) return 0;
    int64_t size = CDVD_TELL(stream);
    if (size < 17 * CDVD_SECTOR_SIZE) return 0;
    CdvdImage next = {.stream = stream, .bytes = (uint64_t)size};
    uint8_t pvd[CDVD_SECTOR_SIZE];
    if (!cdvd_image_read(&next, 16 * CDVD_SECTOR_SIZE, pvd, sizeof(pvd)) ||
        pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) || pvd[6] != 1 ||
        pvd[128] != 0 || pvd[129] != 8 || pvd[156] < 34 || !(pvd[181] & 2))
        return 0;

    next.root_lsn = le32(pvd + 158);
    next.root_size = le32(pvd + 166);
    if (!next.root_size ||
        !contains(&next, (uint64_t)next.root_lsn * CDVD_SECTOR_SIZE, next.root_size))
        return 0;
    *image = next;
    return 1;
}

static unsigned ascii_upper(unsigned c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

static int match_name(const uint8_t *name, const char *component, size_t length)
{
    for (size_t i = 0; i < length; ++i)
        if (ascii_upper(name[i]) != ascii_upper((uint8_t)component[i])) return 0;
    return 1;
}

static int find_in_dir(const CdvdImage *image, uint32_t *lsn, uint32_t *size, int *is_dir,
                       const char *name, size_t name_length)
{
    uint64_t start = (uint64_t)*lsn * CDVD_SECTOR_SIZE;
    uint8_t sector[CDVD_SECTOR_SIZE];
    for (uint64_t off = 0; off < *size; off += CDVD_SECTOR_SIZE) {
        size_t remaining = *size - off;
        size_t bytes = remaining < CDVD_SECTOR_SIZE ? remaining : CDVD_SECTOR_SIZE;
        if (!cdvd_image_read(image, start + off, sector, bytes)) return 0;
        for (size_t p = 0; p < bytes && sector[p]; p += sector[p]) {
            const uint8_t *record = sector + p;
            size_t length = record[0];
            if (length < 34 || length > bytes - p || record[32] > length - 33) return 0;
            const uint8_t *id = record + 33;
            const uint8_t *version = memchr(id, ';', record[32]);
            size_t id_length = version ? (size_t)(version - id) : record[32];
            if (id_length != name_length || !match_name(id, name, id_length)) continue;

            uint32_t found_lsn = le32(record + 2);
            uint32_t found_size = le32(record + 10);
            if ((record[25] & 0x80) ||
                !contains(image, (uint64_t)found_lsn * CDVD_SECTOR_SIZE, found_size))
                return 0;
            *lsn = found_lsn;
            *size = found_size;
            *is_dir = (record[25] & 2) != 0;
            return 1;
        }
    }
    return 0;
}

int cdvd_image_find(const CdvdImage *image, const char *path, uint32_t *lsn, uint32_t *size)
{
    if (!image->stream || !path) return 0;
    uint32_t cur_lsn = image->root_lsn, cur_size = image->root_size;
    int is_dir = 1;
    const char *p = strchr(path, ':');
    p = p ? p + 1 : path;
    while (*p) {
        while (*p == '\\' || *p == '/') ++p;
        if (!*p) break;
        const char *component = p;
        p += strcspn(p, "\\/");
        const char *version = memchr(component, ';', (size_t)(p - component));
        size_t length = (size_t)((version ? version : p) - component);
        if (!length || !is_dir || !find_in_dir(image, &cur_lsn, &cur_size, &is_dir, component, length))
            return 0;
    }
    *lsn = cur_lsn;
    *size = cur_size;
    return 1;
}
