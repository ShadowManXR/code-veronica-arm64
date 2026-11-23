#pragma once
const char *rx_event_movie_bind(const void*);
void rx_event_movie_end(const void*);
const char *rx_event_movie_check(unsigned);
const char *rx_event_movie_status_check(void);
const char *rx_event_movie_tick(void);
int rx_event_movie_running(void);
