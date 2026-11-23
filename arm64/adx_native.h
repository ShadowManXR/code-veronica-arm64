#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned channels,rate,total,data_offset,loop,start_sample,start_byte,end_sample;
    int16_t coefficient[2];
} RxAdxHeader;
/* Bounded input for the original CRI ADX decoder; encoded data stays immutable. */
const char *rx_adx_header(const void*,size_t,RxAdxHeader*);
const char *rx_adx_prepare(unsigned partition,unsigned member);
void rx_adx_mix(int16_t*,size_t);
void rx_adx_reset(void);
int rx_adx_handle(const void*);

int rx_adx_slot_handle(unsigned slot,const void*);
const char *rx_adx_prepare_channels(unsigned partition,unsigned member,unsigned channels);
