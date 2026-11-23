#include "disc.h"
#include "movie/mpeg2.h"
#include "movie/pss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct {
    Mpeg2 *decoder;
    unsigned video_frames, peak, width, height;
    size_t audio_frames, pending;
    unsigned char es[1024*1024];
} check;

static void shown(const Mpeg2Frame *frame, void *unused)
{
    (void)unused;
    ++check.video_frames;
    check.width = frame->width;
    check.height = frame->height;
}

static void packet(void *unused, const unsigned char *data, size_t len)
{
    (void)unused;
    if (len > sizeof(check.es) - check.pending) exit(10);
    memcpy(check.es + check.pending, data, len);
    check.pending += len;
    size_t used = mpeg2_decode(check.decoder, check.es, check.pending, 0);
    check.pending -= used;
    memmove(check.es, check.es + used, check.pending);
}

static void audio(void *unused, const int16_t *pcm, size_t frames, int rate)
{
    (void)unused;
    if (rate != 48000) exit(11);
    for (size_t i = 0; i < frames * 2; ++i) {
        unsigned amplitude = abs(pcm[i]);
        if (amplitude > check.peak) check.peak = amplitude;
    }
    check.audio_frames += frames;
}

static int verify(const CdvdImage *image, unsigned number, unsigned height,
                  unsigned frames, size_t audio_frames)
{
    memset(&check, 0, sizeof(check));
    char name[24];
    snprintf(name, sizeof(name), "MOVIE/MV_%03u.PSS", number);
    uint32_t lsn, size;
    if (!cdvd_image_find(image, name, &lsn, &size)) return 0;
    PssSink sink = {packet, audio, NULL};
    Pss *pss = pss_create(&sink);
    check.decoder = mpeg2_create(shown,NULL);
    unsigned char chunk[32768];
    for (uint32_t off = 0; off < size;) {
        size_t bytes = size - off < sizeof(chunk) ? size - off : sizeof(chunk);
        if (!cdvd_image_read(image, (uint64_t)lsn * 2048 + off, chunk, bytes)) return 0;
        off += (uint32_t)bytes;
        if (!pss_feed(pss, chunk, bytes)) break;
    }
    mpeg2_decode(check.decoder, check.es, check.pending, 1);
    unsigned errors = mpeg2_slice_errors(check.decoder);
    printf("MV_%03u: %ux%u, %u video frames, %zu stereo samples, peak %u, %u slice errors\n",
           number, check.width, check.height, check.video_frames, check.audio_frames, check.peak, errors);
    fflush(stdout);
    mpeg2_destroy(check.decoder);
    pss_destroy(pss);
    return check.width == 320 && check.height == height && check.video_frames == frames &&
           check.audio_frames == audio_frames && check.peak > 100 && !errors;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    RxAndroidDisc disc = {0};
    if (rx_android_mount_disc(&disc, argv[1])) return 2;
    int success = verify(&disc.image,16,352,601,1078016);
    success &= verify(&disc.image,1,176,1229,1967872);
    success &= verify(&disc.image,0,352,6306,10100480);
    rx_android_unmount_disc(&disc);
    return success ? 0 : 3;
}
