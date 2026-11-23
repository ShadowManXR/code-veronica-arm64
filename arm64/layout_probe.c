#include "ps2/guest_layout.h"

/* The SDK's typedefs use long for these names. Supply their intended widths
   so the comparison isolates the mismatch caused by native pointers. */
#define _TYPEDEF_Uint32
#define _TYPEDEF_Sint32
typedef uint32_t Uint32;
typedef int32_t Sint32;
#include <sg_xpt.h>
#include <ninjadef.h>
#include <ninjastr.h>
#include <ninjacnk.h>

_Static_assert(sizeof(NJS_CNK_MODEL) == 32, "Unexpected native Ninja model ABI");
_Static_assert(offsetof(NJS_CNK_MODEL, center) == 16, "Unexpected native model offset");
_Static_assert(sizeof(NJS_CNK_OBJECT) == 72, "Unexpected native Ninja object ABI");
_Static_assert(__builtin_offsetof(NJS_CNK_OBJECT, child) == 56, "Unexpected native object offset");
_Static_assert(sizeof(Ps2ChunkModel32) == 24 && sizeof(Ps2ChunkObject32) == 52,
               "Original disc records must keep their 32-bit layout");
