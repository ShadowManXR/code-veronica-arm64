#pragma once
#include <stdint.h>
const char *rx_event_audio_init(int device);
void rx_event_audio_end(void);
const char *rx_event_audio_check(unsigned slot);
const char *rx_event_audio_deliver(void);
void rx_event_audio_render(int16_t *pcm,int frames);
uint64_t rx_event_audio_frames(void);
int rx_sound_queue_room(void);
int rx_sound_queue_peek(void);
void rx_sound_queue_pop(void);
void rx_sound_queue_reset(void);

const char *rx_event_bgm_mount(void);
const char *rx_event_bgm_check(unsigned member,unsigned volume);
const char *rx_event_bgm_tick(void);

/* Host lifecycle suspension; separate from the retail ADXT_Pause no-op. */
void rx_event_audio_pause(int paused);

const char *rx_event_bgm_stop_check(unsigned fade_byte);

const char *rx_event_se_mount(unsigned key);
void rx_event_se_end(void);
int rx_event_se_mounted(void);
const char *rx_event_se_assets(unsigned sequence);
const char *rx_event_se_check(const unsigned char*);
const char *rx_event_se_tick(void);
int rx_sound_queue_free(void);
void rx_sound_queue_values(signed char*,signed char*,unsigned short*);

const char *rx_event_voice_mount(void);
const char *rx_event_voice_check(unsigned member,unsigned mode);
const char *rx_event_voice_stop_check(unsigned fade);
const char *rx_event_voice_monitor(void);

const char *rx_event_shot_assets(unsigned);
const char *rx_event_step_position_check(const void*);
