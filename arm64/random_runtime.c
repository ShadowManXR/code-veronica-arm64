#include "random_runtime.h"
#include <stdint.h>

/* Retail SLUS_201.84 srand/rand at 0x125310/0x125320: 32-bit multiply/add,
   retain the complete state, return its low 31 bits. _reent default is 1.
   Keep unsigned arithmetic to preserve EE wrapping on ARM64. */
static uint32_t state=1;
void rx_ps2_srand(unsigned seed) { state=(uint32_t)seed; }
int rx_ps2_rand(void)
{
    state=state*UINT32_C(1103515245)+UINT32_C(12345);
    return (int)(state&UINT32_C(0x7fffffff));
}
