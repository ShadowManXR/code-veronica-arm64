/*
 * PSS demuxer. Packs/PES packets are MPEG-2 program stream; stream 0xE0 is the video
 * elementary stream, private stream 1 (0xBD) carries audio behind a 4-byte sub-stream
 * header: an "SShd" chunk (type, rate, channels, interleave) then "SSbd" with the data,
 * channel-interleaved in blocks of `interleave` bytes. Only 16-bit PCM is used on this disc.
 */
#include "pss.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Pss {
    PssSink sink;
    uint8_t* buf; size_t len, cap;          /* unparsed stream bytes */
    uint8_t* abuf; size_t alen, acap;       /* audio payload not yet converted */
    int have_hdr, type, rate, channels, interleave;
    size_t hdr_skip;                        /* bytes of SShd/SSbd header still to drop */
    int failed;
};

Pss* pss_create(const PssSink* sink)
{
    Pss* p = calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->sink = *sink;
    return p;
}

void pss_destroy(Pss* p)
{
    if (!p) return;
    free(p->buf); free(p->abuf); free(p);
}

static int append(uint8_t** b, size_t* len, size_t* cap, const uint8_t* d, size_t n)
{
    if (n > SIZE_MAX - *len) return 0;
    size_t required = *len + n;
    if (required > *cap) {
        size_t capacity = required < (SIZE_MAX - 4096) / 2 ? required * 2 + 4096 : required;
        uint8_t* next = realloc(*b, capacity);
        if (!next) return 0;
        *b = next;
        *cap = capacity;
    }
    memcpy(*b + *len, d, n);
    *len += n;
    return 1;
}

static uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void audio_payload(Pss* p, const uint8_t* d, size_t n)
{
    if (!append(&p->abuf, &p->alen, &p->acap, d, n)) {
        p->failed = 1;
        return;
    }
    if (!p->have_hdr) {
        if (p->alen < 40) return;
        size_t header = le32(p->abuf + 4);
        if (header >= 16 && header <= 4096 && p->alen < header + 16) return;
        if (header < 16 || header > 4096 || memcmp(p->abuf, "SShd", 4) ||
            memcmp(p->abuf + 8 + header, "SSbd", 4)) {
            static int warned;
            if (!warned++) fprintf(stderr, "pss: unknown audio header\n");
            p->alen = 0;
            return;
        }
        p->type = (int)le32(p->abuf + 8);
        p->rate = (int)le32(p->abuf + 12);
        p->channels = (int)le32(p->abuf + 16);
        p->interleave = (int)le32(p->abuf + 20);
        p->hdr_skip = header + 16;
        p->have_hdr = 1;
        if (p->type != 1 || p->channels < 1 || p->channels > 2 || p->interleave <= 0 ||
            p->interleave > 4096 || p->interleave % 2 || p->rate < 8000 || p->rate > 96000) {
            fprintf(stderr, "pss: unsupported audio (type %d, %d ch, interleave %d)\n", p->type, p->channels, p->interleave);
            p->channels = 0;
        }
    }
    if (p->hdr_skip) {
        size_t k = p->hdr_skip < p->alen ? p->hdr_skip : p->alen;
        memmove(p->abuf, p->abuf + k, p->alen - k);
        p->alen -= k; p->hdr_skip -= k;
    }
    if (!p->channels) { p->alen = 0; return; }
    {
        size_t blk = (size_t)p->interleave * p->channels, frames = p->interleave / 2, done = 0;
        int16_t out[4096];
        while (p->alen - done >= blk && frames * 2 <= 4096) {
            const uint8_t* s = p->abuf + done;
            for (size_t i = 0; i < frames; i++) {
                int16_t l = (int16_t)(s[i * 2] | (s[i * 2 + 1] << 8));
                int16_t r = p->channels > 1 ? (int16_t)(s[p->interleave + i * 2] | (s[p->interleave + i * 2 + 1] << 8)) : l;
                out[i * 2] = l; out[i * 2 + 1] = r;
            }
            if (p->sink.audio) p->sink.audio(p->sink.user, out, frames, p->rate);
            done += blk;
        }
        memmove(p->abuf, p->abuf + done, p->alen - done);
        p->alen -= done;
    }
}

int pss_feed(Pss* p, const uint8_t* data, size_t n)
{
    size_t pos = 0;
    if (p->failed || !append(&p->buf, &p->len, &p->cap, data, n)) return 0;
    for (;;) {
        size_t need;
        int id;
        while (pos + 4 <= p->len && !(p->buf[pos] == 0 && p->buf[pos + 1] == 0 && p->buf[pos + 2] == 1)) pos++;
        if (pos + 4 > p->len) break;
        id = p->buf[pos + 3];
        if (id == 0xB9) { p->len = 0; return 0; }               /* program end */
        if (pos + 6 > p->len) break;
        if (id == 0xBA) {
            if (pos + 14 > p->len) break;
            need = (p->buf[pos + 4] & 0xC0) == 0x40 ? 14 + (p->buf[pos + 13] & 7) : 12;
        } else if (id >= 0xBB) {
            need = 6 + ((size_t)p->buf[pos + 4] << 8 | p->buf[pos + 5]);
        } else { pos += 3; continue; }                           /* stray start code */
        if (pos + need > p->len) break;
        if ((id == 0xE0 || id == 0xBD) && need >= 9) {
            const uint8_t* pk = p->buf + pos;
            size_t hdr = 9 + pk[8];
            if ((pk[6] & 0xC0) == 0x80 && hdr <= need) {
                if (id == 0xE0) { if (p->sink.video) p->sink.video(p->sink.user, pk + hdr, need - hdr); }
                else if (need >= hdr + 4) audio_payload(p, pk + hdr + 4, need - hdr - 4);
            }
        }
        pos += need;
    }
    memmove(p->buf, p->buf + pos, p->len - pos);
    p->len -= pos;
    return 1;
}
