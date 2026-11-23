#pragma once

#include "ps2/cdvd_image.h"

typedef struct RxMovie RxMovie;
typedef struct RxMovieDraw {
    unsigned program, planes[3];
    unsigned uploaded, width, height;
    int bounds, brightness;
} RxMovieDraw;

typedef enum {
    RX_MOVIE_BUFFERING, RX_MOVIE_PLAYING, RX_MOVIE_FINISHED, RX_MOVIE_ERROR
} RxMovieState;

void rx_game_movie_bind(const CdvdImage *disc);
int rx_game_movie_available(unsigned number);
int rx_game_movie_start(unsigned number, int x, int y, int width, int height,
                        unsigned cancel_button, int fade, int decibels_tenths);
void rx_game_movie_stop(void);
RxMovie *rx_game_movie_active(void);
RxMovieState rx_game_movie_tick(RxMovie *movie, unsigned pressed);
void rx_game_movie_pause(RxMovie *movie, int paused);
unsigned rx_game_movie_frame(const RxMovie *movie);
int rx_game_movie_time(const RxMovie *movie);
int rx_game_movie_cancelled(const RxMovie *movie);
int rx_game_movie_draw(const RxMovie *movie, RxMovieDraw *draw);
void rx_game_movie_close_graphics(RxMovieDraw *draw);
