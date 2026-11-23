#include "opening.h"
#include "boot_disc.h"
#include "face_control_runtime.h"
#include "event_runtime.h"
#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void rx_check_face_state(size_t,size_t);
static void check(int ok,const char *why){if(!ok){fprintf(stderr,"FAIL face state device: %s\n",why);exit(1);}}
static unsigned word(const void *p){unsigned n;memcpy(&n,p,4);return n;}
int main(int argc,char **argv)
{
    check(argc==2,"supply ISO OBB directory");RxAndroidDisc disc={0};RxBootDisc boot={0};RxOpening opening={0};
    const char *error=rx_android_mount_disc(&disc,argv[1]);check(!error,error);
    error=rx_android_start_disc(&disc,&boot);check(!error,error);
    error=rx_opening_start(&opening,0);check(!error,error);
    RxEventFault fault={0};error=rx_opening_check_events(&fault);
    check(error&&!opening.events_ready&&fault.opcode==RX_OPENING_LIVE_RUNTIME&&fault.offset==0,"full preflight did not advance beyond facial requests");
    printf("Opening readiness opcode %02x offset %zu: %s\n",fault.opcode,fault.offset,error);
    const unsigned char *raw=sys->rdtp;const unsigned char *owned=(const unsigned char *)rom->mdl.datP-word(raw+word(raw+20));
    check(sys->mspp>=owned&&sys->lspp>=owned,"native room bank identity");
    size_t mouth=sys->rdtsz-(size_t)(sys->mspp-owned),lip=sys->rdtsz-(size_t)(sys->lspp-owned);
    rx_check_face_state(mouth,lip);
    BH_PWORK *a=&ene[rom->enep[0].wrk_no],*b=&ene[rom->enep[1].wrk_no];rx_opening_end(&opening);
    check(!a->exp0&&!a->exp1&&!a->exp2&&!b->exp0&&!b->exp1&&!b->exp2&&rx_face_control_ready(a)&&rx_face_control_ready(b),"native face teardown retained owned actor references");
    rx_android_stop_disc();rx_android_unmount_disc(&disc);puts("PASS native facial context teardown; campaign progression remains gated");return 0;
}
