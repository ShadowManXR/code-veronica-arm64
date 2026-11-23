#pragma once
#include <stdlib.h>
int rx_ps2_rand(void);
void rx_ps2_srand(unsigned seed);
#ifdef RECVX_NATIVE_RANDOM
#define rand rx_ps2_rand
#define srand rx_ps2_srand
#endif
