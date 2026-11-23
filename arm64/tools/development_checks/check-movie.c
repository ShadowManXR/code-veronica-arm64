#include "android_egl.h"
#include "boot_disc.h"
#include "movie_port.h"
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int PlayStartMovieEx(int number, int type, int pause);
int AdvStartStartupMovie(void);
int PlayMovieMain(void);
int CheckPlayEndMovie(void);
int GetTimeMoive(void);
void PlayStopMovieEx(int mode);

static unsigned char pixels[640*480*4];
static RxEgl egl;
static RxMovieDraw draw;

static void delay(unsigned ms)
{
    struct timespec interval = {ms / 1000, (long)(ms % 1000) * 1000000};
    nanosleep(&interval, NULL);
}

static int frame(const char *capture)
{
    RxMovie *movie = rx_game_movie_active();
    if (!movie) return 0;
    glViewport(0, 0, 640, 480);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!rx_game_movie_draw(movie, &draw) || !draw.uploaded) return 0;
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    if (glGetError()) return 0;
    unsigned colored = 0;
    for (unsigned y = 0; y < 480; ++y)
        for (unsigned x = 0; x < 640; ++x) {
            unsigned i = (640 * y + x) * 4;
            int color = pixels[i] > 10 || pixels[i+1] > 10 || pixels[i+2] > 10;
            if (color && (y < 80 || y >= 400)) return 0;
            colored += color;
        }
    if (colored < 1000) return 0;
    if (!capture) return 1;
    FILE *file = fopen(capture, "wb");
    return file && fwrite(pixels, 1, sizeof(pixels), file) == sizeof(pixels) && !fclose(file);
}

int main(int argc, char **argv)
{
    if (argc != 3) return 1;
    RxAndroidDisc disc = {0};
    const char *error = rx_android_mount_disc(&disc, argv[1]);
    if (error) { puts(error); return 2; }
    rx_game_movie_bind(&disc.image);
    if (!rx_egl_open(&egl, NULL, 640, 480)) return 3;
    if (PlayStartMovieEx(17, 0, 0) != 1 || CheckPlayEndMovie()) return 4;
    if (AdvStartStartupMovie() || !CheckPlayEndMovie() || PlayStartMovieEx(1, 0, 0) != 2) return 5;
    int saved = 0, paused = getenv("RECVX_NO_PAUSE") != NULL, finished = 0;
    unsigned previous_frame = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        int state = PlayMovieMain();
        if (state) {
            if (state != 2) { printf("startup ended early with status %d\n", state); return 6; }
            finished = 1;
            break;
        }
        RxMovie *movie = rx_game_movie_active();
        int centiseconds = GetTimeMoive();
        unsigned frame_number = rx_game_movie_frame(movie);
        if (frame_number < previous_frame) return 7;
        previous_frame = frame_number;
        if (!paused && centiseconds >= 70) {
            rx_game_movie_pause(movie, 1);
            rx_game_movie_close_graphics(&draw);
            rx_egl_close(&egl);
            delay(220);
            if (PlayMovieMain() || GetTimeMoive() != centiseconds || rx_game_movie_frame(movie) != frame_number) return 8;
            if (!rx_egl_open(&egl, NULL, 640, 480)) return 9;
            rx_game_movie_pause(movie, 0);
            paused = 1;
        }
        if (!saved && centiseconds >= 160) saved = frame(argv[2]);
        if (saved && centiseconds >= 210) (void)rx_game_movie_tick(movie, 0x800);
        delay(12);
    }
    if (!paused || !saved || !finished || previous_frame < 60) {
        printf("startup: pause=%d capture=%d ended=%d last=%u\n", paused, saved, finished, previous_frame);
        return 10;
    }
    printf("PASS original startup skipped after frame %u, with frozen audio clock/frame during pause and GLES restored\n",
           previous_frame);
    rx_game_movie_close_graphics(&draw);
    unsigned numbers[] = {0, 1};
    for (unsigned i = 0; i < sizeof(numbers)/sizeof(*numbers); ++i) {
        if (PlayStartMovieEx(numbers[i], 0, 0)) return 11;
        int success = 0;
        for (int j = 0; j < 560; ++j) {
            if (PlayMovieMain()) return 12;
            if (GetTimeMoive() >= 120 && rx_game_movie_draw(rx_game_movie_active(), &draw) && draw.uploaded) { success = 1; break; }
            delay(12);
        }
        if (!success) return 13;
        printf("PASS later-scene MV_%03u reached frame %u on its soundtrack clock\n",
               numbers[i], rx_game_movie_frame(rx_game_movie_active()));
        RxMovieState state = rx_game_movie_tick(rx_game_movie_active(), 0x100);
        if (state != RX_MOVIE_FINISHED || !rx_game_movie_cancelled(rx_game_movie_active())) return 14;
        PlayStopMovieEx(0);
        if (CheckPlayEndMovie()) return 15;
        rx_game_movie_close_graphics(&draw);
    }
    rx_game_movie_bind(NULL);
    rx_egl_close(&egl);
    rx_android_unmount_disc(&disc);
    return 0;
}


