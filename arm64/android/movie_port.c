#include "movie_port.h"

#include "movie/mpeg2.h"
#include "movie/pss.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { FRAME_RING = 10, PCM_RING = 262144, VIDEO_LIMIT = 4 * 1024 * 1024 };
static const double frames_per_second = 30000.0 / 1001.0;

struct RxMovie {
    int fd, thread_started;
    unsigned number, cancel_button;
    int x, y, width, height, fade, volume;
    uint64_t offset;
    uint32_t bytes;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t room;
    atomic_int quit, failed;
    int eof, cancelled, paused, started;
    unsigned decoded, taken, shown;
    unsigned image_width, image_height, plane_bytes;
    uint8_t *pictures, *current, *es;
    size_t es_bytes, es_capacity;
    int16_t *pcm;
    uint64_t pcm_in, pcm_out;
    int rate, pcm_done;
    AAudioStream *audio;
    int64_t written;
    double media_time, fade_started, tail_started, tail_offset, paused_at, last_progress;
    int stalled;
};

static const CdvdImage *disc_image;
static RxMovie *active_movie;

static double monotonic_now(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec / 1e9;
}

static void video_packet(void *context, const uint8_t *data, size_t bytes)
{
    RxMovie *movie = context;
    if (bytes > VIDEO_LIMIT - movie->es_bytes) {
        movie->failed = 1;
        return;
    }
    size_t required = movie->es_bytes + bytes;
    if (required > movie->es_capacity) {
        size_t capacity = required < VIDEO_LIMIT / 2 ? required * 2 : VIDEO_LIMIT;
        uint8_t *buffer = realloc(movie->es, capacity);
        if (!buffer) {
            movie->failed = 1;
            return;
        }
        movie->es = buffer;
        movie->es_capacity = capacity;
    }
    memcpy(movie->es + movie->es_bytes, data, bytes);
    movie->es_bytes = required;
}

static void audio_packet(void *context, const int16_t *samples, size_t frames, int rate)
{
    RxMovie *movie = context;
    pthread_mutex_lock(&movie->mutex);
    if (!movie->rate) movie->rate = rate;
    if (rate != movie->rate || frames > PCM_RING - (movie->pcm_in - movie->pcm_out)) {
        movie->failed = 1;
    } else {
        for (size_t i = 0; i < frames; ++i) {
            size_t index = ((movie->pcm_in + i) % PCM_RING) * 2;
            movie->pcm[index] = samples[i * 2];
            movie->pcm[index + 1] = samples[i * 2 + 1];
        }
        movie->pcm_in += frames;
    }
    pthread_mutex_unlock(&movie->mutex);
}

static void copy_plane(uint8_t *out, const uint8_t *in, unsigned width,
                       unsigned height, unsigned stride)
{
    for (unsigned y = 0; y < height; ++y)
        memcpy(out + (size_t)y * width, in + (size_t)y * stride, width);
}

static void video_frame(const Mpeg2Frame *frame, void *context)
{
    RxMovie *movie = context;
    if (frame->width <= 0 || frame->width > 720 || frame->height <= 0 || frame->height > 576 ||
        (frame->width & 1) || (frame->height & 1) || !frame->y || !frame->cb || !frame->cr) {
        movie->failed = 1;
        return;
    }

    pthread_mutex_lock(&movie->mutex);
    if (!movie->pictures) {
        movie->image_width = frame->width;
        movie->image_height = frame->height;
        movie->plane_bytes = movie->image_width * movie->image_height;
        size_t frame_bytes = movie->plane_bytes * 3 / 2;
        movie->pictures = malloc(frame_bytes * FRAME_RING);
        movie->current = malloc(frame_bytes);
    }
    if (!movie->pictures || !movie->current ||
        movie->image_width != (unsigned)frame->width || movie->image_height != (unsigned)frame->height) {
        movie->failed = 1;
    } else {
        while (movie->decoded - movie->taken == FRAME_RING && !movie->quit)
            pthread_cond_wait(&movie->room, &movie->mutex);
        if (!movie->quit) {
            size_t luma = movie->plane_bytes;
            uint8_t *slot = movie->pictures + (movie->decoded % FRAME_RING) * (luma * 3 / 2);
            copy_plane(slot, frame->y, frame->width, frame->height, frame->y_stride);
            copy_plane(slot + luma, frame->cb, frame->width / 2, frame->height / 2, frame->c_stride);
            copy_plane(slot + luma * 5 / 4, frame->cr, frame->width / 2, frame->height / 2, frame->c_stride);
            ++movie->decoded;
        }
    }
    pthread_mutex_unlock(&movie->mutex);
}

static void *decode_movie(void *context)
{
    RxMovie *movie = context;
    PssSink sink = {video_packet, audio_packet, movie};
    Pss *pss = pss_create(&sink);
    Mpeg2 *decoder = mpeg2_create(video_frame, movie);
    if (!pss || !decoder) movie->failed = 1;
    uint32_t cursor = 0;
    int end = 0;
    uint8_t chunk[32768];
    while (!movie->quit && !movie->failed) {
        pthread_mutex_lock(&movie->mutex);
        int video_room = movie->decoded - movie->taken <= FRAME_RING - 2;
        int read_ahead = !end && cursor < movie->bytes && movie->es_bytes < VIDEO_LIMIT / 2 &&
                         movie->pcm_in - movie->pcm_out < (unsigned)(movie->rate ? movie->rate / 2 : 24000);
        if (!video_room && !read_ahead) {
            if (!movie->quit) pthread_cond_wait(&movie->room, &movie->mutex);
            pthread_mutex_unlock(&movie->mutex);
            continue;
        }
        pthread_mutex_unlock(&movie->mutex);
        if (video_room && movie->es_bytes) {
            size_t used = mpeg2_decode_frame(decoder, movie->es, movie->es_bytes, end);
            if (used) {
                movie->es_bytes -= used;
                memmove(movie->es, movie->es + used, movie->es_bytes);
                continue;
            }
        }
        if (end || cursor == movie->bytes) {
            if (!end) {
                end = 1;
                continue;
            }
            break;
        }
        size_t size = movie->bytes - cursor;
        if (size > sizeof(chunk)) size = sizeof(chunk);
        ssize_t got = pread(movie->fd, chunk, size, (off_t)(movie->offset + cursor));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {
            movie->failed = 1;
            break;
        }
        cursor += (uint32_t)got;
        end = !pss_feed(pss, chunk, (size_t)got);
    }
    __android_log_print(ANDROID_LOG_INFO, "RECVX-MOVIE", "MV_%03u decoded %u frames, %u slice errors",
                        movie->number, movie->decoded, decoder ? mpeg2_slice_errors(decoder) : 0);
    pss_destroy(pss);
    mpeg2_destroy(decoder);
    pthread_mutex_lock(&movie->mutex);
    movie->eof = 1;
    if (!movie->decoded) movie->failed = 1;
    pthread_mutex_unlock(&movie->mutex);
    return NULL;
}

void rx_game_movie_stop(void)
{
    RxMovie *movie = active_movie;
    if (!movie) return;
    active_movie = NULL;
    pthread_mutex_lock(&movie->mutex);
    movie->quit = 1;
    pthread_cond_signal(&movie->room);
    pthread_mutex_unlock(&movie->mutex);
    if (movie->thread_started) pthread_join(movie->thread, NULL);
    if (movie->audio) AAudioStream_close(movie->audio);
    if (movie->fd >= 0) close(movie->fd);
    pthread_cond_destroy(&movie->room);
    pthread_mutex_destroy(&movie->mutex);
    free(movie->es);
    free(movie->pictures);
    free(movie->current);
    free(movie->pcm);
    free(movie);
}

void rx_game_movie_bind(const CdvdImage *disc)
{
    rx_game_movie_stop();
    disc_image = disc;
}

int rx_game_movie_start(unsigned number, int x, int y, int width, int height,
                        unsigned cancel_button, int fade, int volume)
{
    if (active_movie) return 2;
    if (!disc_image || !disc_image->stream || number > 999 || width <= 0 || height <= 0) return 1;
    char name[32];
    snprintf(name, sizeof(name), "MOVIE/MV_%03u.PSS", number);
    uint32_t sector, bytes;
    if (!cdvd_image_find(disc_image, name, &sector, &bytes) || !bytes) return 1;
    RxMovie *movie = calloc(1, sizeof(*movie));
    if (!movie) return 1;
    if (pthread_mutex_init(&movie->mutex, NULL)) {
        free(movie);
        return 1;
    }
    if (pthread_cond_init(&movie->room, NULL)) {
        pthread_mutex_destroy(&movie->mutex);
        free(movie);
        return 1;
    }
    atomic_init(&movie->quit, 0);
    atomic_init(&movie->failed, 0);
    active_movie = movie;
    movie->fd = dup(fileno(disc_image->stream));
    movie->number = number;
    movie->x = x;
    movie->y = y;
    movie->width = width;
    movie->height = height;
    movie->cancel_button = cancel_button;
    movie->fade = fade;
    movie->volume = volume;
    movie->offset = (uint64_t)sector * CDVD_SECTOR_SIZE;
    movie->bytes = bytes;
    movie->pcm = malloc(PCM_RING * 2 * sizeof(*movie->pcm));
    if (movie->fd < 0 || !movie->pcm || pthread_create(&movie->thread, NULL, decode_movie, movie)) {
        rx_game_movie_stop();
        return 1;
    }
    movie->thread_started = 1;
    __android_log_print(ANDROID_LOG_INFO, "RECVX-MOVIE", "Playing %s (%u bytes)", name, bytes);
    return 0;
}

int rx_game_movie_available(unsigned number)
{
    if(!disc_image||!disc_image->stream||number>999)return 0;
    char name[32];snprintf(name,sizeof(name),"MOVIE/MV_%03u.PSS",number);
    uint32_t sector,bytes;return cdvd_image_find(disc_image,name,&sector,&bytes)&&bytes>=4;
}

RxMovie *rx_game_movie_active(void) { return active_movie; }
unsigned rx_game_movie_frame(const RxMovie *movie) { return movie->shown ? movie->shown - 1 : 0; }
int rx_game_movie_time(const RxMovie *movie) { return (int)(movie->media_time * 100); }
int rx_game_movie_cancelled(const RxMovie *movie) { return movie->cancelled; }

static int open_audio(RxMovie *movie)
{
    AAudioStreamBuilder *builder = NULL;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) return 0;
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder, movie->rate);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setBufferCapacityInFrames(builder, movie->rate / 6);
    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &movie->audio);
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "RECVX-MOVIE", "AAudio open: %s", AAudio_convertResultToText(result));
        return 0;
    }
    if (AAudioStream_getSampleRate(movie->audio) != movie->rate ||
        AAudioStream_getChannelCount(movie->audio) != 2 ||
        AAudioStream_getFormat(movie->audio) != AAUDIO_FORMAT_PCM_I16) return 0;
    AAudioStream_setBufferSizeInFrames(movie->audio, movie->rate / 8);
    return AAudioStream_requestStart(movie->audio) == AAUDIO_OK;
}

static int write_audio(RxMovie *movie, double fade)
{
    int16_t buffer[4096 * 2];
    size_t frames = movie->pcm_in - movie->pcm_out;
    if (frames > 4096) frames = 4096;
    if (!frames) return 1;
    float gain = powf(10.0f, movie->volume / 200.0f) * (1.0 - fade);
    for (size_t i = 0; i < frames; ++i) {
        size_t offset = ((movie->pcm_out + i) % PCM_RING) * 2;
        buffer[i * 2] = (int16_t)(movie->pcm[offset] * gain);
        buffer[i * 2 + 1] = (int16_t)(movie->pcm[offset + 1] * gain);
    }
    aaudio_result_t written = AAudioStream_write(movie->audio, buffer, (int32_t)frames, 0);
    if (written < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "RECVX-MOVIE", "AAudio write: %s", AAudio_convertResultToText(written));
        return 0;
    }
    movie->pcm_out += written;
    movie->written += written;
    if (written) pthread_cond_signal(&movie->room);
    return 1;
}

static double audio_position(const RxMovie *movie, double now)
{
    int64_t frame, nanos;
    double position;
    if (AAudioStream_getTimestamp(movie->audio, CLOCK_MONOTONIC, &frame, &nanos) == AAUDIO_OK)
        position = frame + (now - nanos / 1e9) * movie->rate;
    else
        position = AAudioStream_getFramesRead(movie->audio);
    return fmin(fmax(0, position), movie->written) / movie->rate;
}

void rx_game_movie_pause(RxMovie *movie, int paused)
{
    if (!movie || movie->paused == paused) return;
    movie->paused = paused;
    double now = monotonic_now();
    if (paused) {
        movie->paused_at = now;
        if (movie->audio) AAudioStream_requestPause(movie->audio);
    } else {
        double duration = now - movie->paused_at;
        if (movie->tail_started) movie->tail_started += duration;
        if (movie->fade_started) movie->fade_started += duration;
        if (movie->audio && AAudioStream_requestStart(movie->audio) != AAUDIO_OK) movie->failed = 1;
    }
}

RxMovieState rx_game_movie_tick(RxMovie *movie, unsigned pressed)
{
    if (movie->failed) return RX_MOVIE_ERROR;
    if (movie->paused) return movie->started ? RX_MOVIE_PLAYING : RX_MOVIE_BUFFERING;
    double now = monotonic_now();
    if (pressed & movie->cancel_button) {
        movie->cancelled = 1;
        if (!movie->fade || !movie->shown) return RX_MOVIE_FINISHED;
        if (!movie->fade_started) movie->fade_started = now;
    }
    double fade = movie->fade_started ? now - movie->fade_started : 0;
    if (fade >= 1) return RX_MOVIE_FINISHED;
    double previous_time = movie->media_time;

    pthread_mutex_lock(&movie->mutex);
    if (!movie->started) {
        if (!movie->decoded || (!movie->eof && movie->rate && movie->pcm_in < (unsigned)movie->rate / 12) ||
            (!movie->eof && !movie->rate && movie->decoded < FRAME_RING)) {
            pthread_mutex_unlock(&movie->mutex);
            return RX_MOVIE_BUFFERING;
        }
        if (movie->rate && !open_audio(movie)) movie->failed = 1;
        movie->started = 1;
        movie->last_progress = now;
        if (!movie->audio) movie->tail_started = now;
    }
    if (movie->audio && !movie->failed) {
        if (!write_audio(movie, fade)) movie->failed = 1;
        double position = audio_position(movie, now);
        if (position > movie->media_time) movie->media_time = position;
        if (movie->eof && movie->pcm_in == movie->pcm_out &&
            position >= (double)movie->written / movie->rate) {
            if (!movie->tail_started) {
                movie->tail_offset = position;
                movie->tail_started = now;
            }
            movie->pcm_done = 1;
        }
    }
    if (movie->tail_started)
        movie->media_time = fmax(movie->media_time, movie->tail_offset + now - movie->tail_started);
    unsigned index = (unsigned)(movie->media_time * frames_per_second);
    if (index >= movie->decoded) index = movie->decoded - 1;
    if (index >= movie->taken) {
        size_t bytes = movie->plane_bytes * 3 / 2;
        memcpy(movie->current, movie->pictures + (index % FRAME_RING) * bytes, bytes);
        movie->shown = movie->taken = index + 1;
        pthread_cond_signal(&movie->room);
    }
    if (movie->media_time > previous_time) {
        movie->last_progress = now;
        movie->stalled = 0;
    }
    if (!movie->stalled && now - movie->last_progress > 2.0 && movie->audio) {
        movie->stalled = 1;
        __android_log_print(ANDROID_LOG_WARN, "RECVX-MOVIE",
                            "Stalled at %.3f s: %u frames decoded, %llu PCM queued, written %lld, read %lld, state %d",
                            movie->media_time, movie->decoded,
                            (unsigned long long)(movie->pcm_in - movie->pcm_out), (long long)movie->written,
                            (long long)AAudioStream_getFramesRead(movie->audio), AAudioStream_getState(movie->audio));
    }
    int finished = movie->eof && (!movie->audio || movie->pcm_done) &&
                   movie->media_time * frames_per_second >= movie->decoded;
    pthread_mutex_unlock(&movie->mutex);
    return movie->failed ? RX_MOVIE_ERROR : finished ? RX_MOVIE_FINISHED : RX_MOVIE_PLAYING;
}

// The GLES backend consumes the most recent frame from the Android thread.
const uint8_t *rx_movie_pixels(const RxMovie *movie, unsigned *serial, unsigned *width,
                              unsigned *height, float rectangle[4], float *brightness)
{
    if (!movie->shown) return NULL;
    *serial = movie->shown;
    *width = movie->image_width;
    *height = movie->image_height;
    rectangle[0] = 2.0f * movie->x / 640.0f - 1;
    rectangle[1] = 1 - 2.0f * movie->y / 480.0f;
    rectangle[2] = 2.0f * (movie->x + movie->width) / 640.0f - 1;
    rectangle[3] = 1 - 2.0f * (movie->y + movie->height) / 480.0f;
    double now = movie->paused ? movie->paused_at : monotonic_now();
    *brightness = movie->fade_started ? fmax(0, 1.0 - (now - movie->fade_started)) : 1;
    return movie->current;
}
