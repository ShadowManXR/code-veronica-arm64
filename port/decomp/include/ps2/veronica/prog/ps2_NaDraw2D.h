#ifndef _PS2_NADRAW2D_H_
#define _PS2_NADRAW2D_H_

#include "types.h"

static inline void Ps2DepthCoefficients(float near, float far, float* a, float* b)
{
    *a = (-65536.0f * near) / (far - near);
    *b = (65536.0f * near * far) / (far - near);
}

void CalcPs2ZbuffAB();
void sceVu0ITOF12Vector(sceVu0FVECTOR v0, sceVu0IVECTOR v1);
void	njDrawLine2D(NJS_POINT2COL *p, Int n, Float pri, Uint32 attr);
void	njDrawPolygon2D(NJS_POINT2COL *p, Int n, Float pri, Uint32 attr);
void njDrawPolygon2DM(NJS_POINT2COL* p, int n, float pri, unsigned int attr);

extern float Ps2_zbuff_a;
extern float Ps2_zbuff_b;

#endif
