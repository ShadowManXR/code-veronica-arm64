#ifndef _SYNC_H_
#define _SYNC_H_

#include "types.h"

void bhInitVSync();
void bhClearVSync();
void bhControlVSync();
void bhApplyCameraFog(void);
void bhControlEOR();
void bhCheckPadPort();
void bhCheckSoftReset();

#endif
