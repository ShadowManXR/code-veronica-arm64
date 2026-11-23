#pragma once
/* Declarations for unretained PS2 platform branches; no native stubs. */
#include "ps2sdk_shim.h"
int sceSdRemoteInit(void);
int sceSSyn_SetOutputMode(int mode);
unsigned int isceSifSetDma(sceSifDmaData *data,int count);
