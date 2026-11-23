#include "effect_callbacks.h"
#include "effect_runtime.h"
#include "message_runtime.h"
#include "fade_runtime.h"
#ifdef RECVX_NATIVE_INVENTORY_RENDER
#include "inventory_sprite.h"
#endif
#include "effsub1.h"
#include "main.h"
#include "ps2_dummy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static RxEffectCallbacks *active;
static size_t capacity;
static const char *transport_error;

void rx_effect_callbacks_destroy(RxEffectCallbacks *batch)
{
    if (!batch) return;
    free(batch->polygons);*batch=(RxEffectCallbacks){0};
}

int rx_primitive_2d(uint64_t primitive,const void *data,unsigned count)
{
#ifdef RECVX_NATIVE_INVENTORY_RENDER
    if(rx_inventory_sprite_collecting())return rx_inventory_sprite_capture(primitive,data,count);
#endif
    if(rx_fade_collecting())return rx_fade_capture(primitive,data,count);
    if(rx_message_collecting())return rx_message_capture(primitive,data,count);
    if (!active) { sys->error|=0x80000000u;return -1; }
    if (transport_error) return -1;
    if (primitive!=UINT64_C(0x26800000000000) || count!=4 || !data ||
        Ps2_gs_save.ALPHA!=0x44 || active->count>=capacity) {
        transport_error="Unsupported original callback polygon or full native transport";return -1;
    }
    RxScreenPolygon polygon={0};
    const unsigned char *source=data;
    for (unsigned i=0;i<4;++i) {
        memcpy(polygon.color[i],source+i*48+16,16);
        memcpy(polygon.position[i],source+i*48+32,12);
        for (unsigned j=0;j<3;++j)
            if (!isfinite(polygon.position[i][j])) {
                transport_error="Nonfinite original callback polygon";return -1;
            }
        if (polygon.position[i][0]<0 || polygon.position[i][0]>4095 ||
            polygon.position[i][1]<0 || polygon.position[i][1]>4095 || polygon.position[i][2]>-1) {
            transport_error="Original callback polygon exceeds native GS bounds";return -1;
        }
        for (unsigned j=0;j<4;++j)
            if (polygon.color[i][j]>(j==3?128u:255u)) {
                transport_error="Original callback color exceeds GS channel bounds";return -1;
            }
    }
    active->polygons[active->count++]=polygon;return 0;
}

const char *rx_effect_callbacks_collect(RxEffectCallbacks *out)
{
    if (!out || active || sys->ef_fncn<0 || sys->ef_fncn>512)
        return "Invalid original effect callback collection request";
    for (int i=0;i<sys->ef_fncn;++i) {
        O_WRK *op=sys->ef_fnc[i];uintptr_t address=(uintptr_t)op,start=(uintptr_t)eff;
        if (address<start || address-start>=sizeof(eff) || (address-start)%sizeof(*eff))
            return "Original callback queue references absent native work";
        const char *error=rx_effect_validate(op);if (error) return error;
        if (op->id!=21 || op->func!=bhDraw021)
            return "Unported original effect drawing callback";
    }
    RxEffectCallbacks next={0};capacity=(size_t)sys->ef_fncn*4;
    if (capacity) {
        next.polygons=calloc(capacity,sizeof(*next.polygons));
        if (!next.polygons) return "Cannot allocate original callback polygons";
    }
    transport_error=NULL;active=&next;
    for (int i=0;i<sys->ef_fncn && !transport_error;++i) sys->ef_fnc[i]->func(sys->ef_fnc[i]);
    active=NULL;
    if (transport_error) { rx_effect_callbacks_destroy(&next);return transport_error; }
    rx_effect_callbacks_destroy(out);*out=next;return NULL;
}
