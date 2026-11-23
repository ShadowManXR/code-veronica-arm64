/*
 * EE DMA controller (libdma API + direct CHCR kicks) for the channels the game
 * uses: VIF1 (ch1) and GIF (ch2). Transfers run to completion immediately.
 */
#include "ps2_platform.h"
#include <stdio.h>
#include <stdlib.h>

enum { TAG_REFE, TAG_CNT, TAG_NEXT, TAG_REF, TAG_REFS, TAG_CALL, TAG_RET, TAG_END };

#ifdef RECVX_TRANSLATED_MEMORY
#define DMA_PTR(address, size) ps2_dma_ptr_sized(address, size)
#define DMA_ADDRESS(pointer, size) ps2_dma_address(pointer, size)
#else
#define DMA_PTR(address, size) ps2_dma_ptr(address)
#define DMA_ADDRESS(pointer, size) ((uint32_t)(uintptr_t)(pointer))
#endif

static int channel_of(const void* chan)
{
    uint32_t a = (uint32_t)(uintptr_t)chan;
    switch (a & 0xFFFFFF00u) {
    case 0x10008000: return 0;
    case 0x10009000: return 1;
    case 0x1000a000: return 2;
    case 0x1000b000: return 3;
    case 0x1000b400: return 4;
    case 0x1000c000: return 5;
    case 0x1000c400: return 6;
    case 0x1000c800: return 7;
    case 0x1000d000: return 8;
    case 0x1000d400: return 9;
    }
    return -1;
}

static void deliver(int ch, const uint32_t* data, uint32_t qwc)
{
    if (!qwc) return;
#ifdef RECVX_TRANSLATED_MEMORY
    /* Validate the entire payload, including inline CNT/CALL/END data. */
    ps2_dma_address(data, (size_t)qwc * 16);
#endif
    if (ch == 1) ps2_vif1_transfer(data, qwc);
    else if (ch == 2) ps2_gif_transfer(3, data, qwc);
    else host_log("dma: transfer on unsupported channel %d (%u qw)\n", ch, qwc);
}

/* Source-chain mode. tte: transfer the tag's upper 64 bits to the peripheral (VIF only). */
static void run_chain(int ch, uint32_t tadr, int tte)
{
    uint32_t stack[2]; int sp = 0;
    static int trace = -1;
    if (trace < 0) trace = getenv("RECVX_TRACE_DMA") != NULL;
    for (int guard = 0; guard < 1 << 20; guard++) {
        if (tadr & 15) { host_log("dma: unaligned tag %08x\n", tadr); return; }
        const uint32_t* tag = (const uint32_t*)DMA_PTR(tadr, 16);
        uint32_t qwc = tag[0] & 0xFFFF;
        uint32_t id = (tag[0] >> 28) & 7;
        uint32_t addr = tag[1];
        uint32_t next = tadr + 16;
        const uint32_t* payload = qwc && (id == TAG_REF || id == TAG_REFS || id == TAG_REFE) ?
            (const uint32_t*)DMA_PTR(addr, (size_t)qwc * 16) : tag + 4;
#ifdef RECVX_TRANSLATED_MEMORY
        if (qwc) ps2_dma_address(payload, (size_t)qwc * 16);
#endif

        if (tte && ch == 1) ps2_vif1_transfer_words(&tag[2], 2);
        if (trace && ch == 2 && qwc) {
            static int n; if (n++ < 60) host_log("dma ch%d tadr=%08x tag=%08x %08x id=%u qwc=%u data0=%08x %08x %08x %08x\n", ch, tadr, tag[0], tag[1], id, qwc,
                payload[0], payload[1], payload[2], payload[3]);
        }

        switch (id) {
        case TAG_REFE:
            deliver(ch, payload, qwc);
            return;
        case TAG_CNT:
            deliver(ch, tag + 4, qwc);
            tadr = next + qwc * 16;
            break;
        case TAG_NEXT:
            deliver(ch, tag + 4, qwc);
            tadr = addr;
            break;
        case TAG_REF:
        case TAG_REFS:
            deliver(ch, payload, qwc);
            tadr = next;
            break;
        case TAG_CALL:
            deliver(ch, tag + 4, qwc);
            if (sp >= 2) { host_log("dma: call stack overflow\n"); return; }
            stack[sp++] = next + qwc * 16;
            tadr = addr;
            break;
        case TAG_RET:
            deliver(ch, tag + 4, qwc);
            if (sp == 0) return;
            tadr = stack[--sp];
            break;
        case TAG_END:
            deliver(ch, tag + 4, qwc);
            return;
        }
        /* keep SPR-relative addressing when the chain lives in the scratchpad */
        uint32_t tag_address = DMA_ADDRESS(tag, 16);
        if (tag_address >= PS2_SPR_BASE && tag_address < PS2_SPR_BASE + PS2_SPR_SIZE &&
            (id == TAG_CNT || id == TAG_REF || id == TAG_REFS))
            tadr = PS2_SPR_BASE | (tadr & (PS2_SPR_SIZE - 1));
    }
    host_log("dma: runaway chain on channel %d\n", ch);
}

void sceDmaReset(int mode) { (void)mode; }
void* sceDmaGetChan(int id) { static const uint32_t base[] = { 0x10008000, 0x10009000, 0x1000a000, 0x1000b000, 0x1000b400, 0x1000c000, 0x1000c400, 0x1000c800, 0x1000d000, 0x1000d400 }; return (id >= 0 && id < 10) ? (void*)(uintptr_t)base[id] : NULL; }

int sceDmaSend(void* chan, void* tag)
{
    int ch = channel_of(chan);
    run_chain(ch, DMA_ADDRESS(tag, 16), ch == 1);
    return 0;
}

int sceDmaSendN(void* chan, void* addr, int qwc)
{
    if (qwc <= 0) return qwc < 0 ? -1 : 0;
    deliver(channel_of(chan), DMA_PTR(DMA_ADDRESS(addr, (size_t)qwc * 16), (size_t)qwc * 16), (uint32_t)qwc);
    return 0;
}

int sceDmaSync(void* chan, int mode, int timeout) { (void)chan; (void)mode; (void)timeout; ps2_poll_interrupts(); return 0; }

/* direct register kick: CHCR with STR set */
void ps2_dma_put_chcr(int ch, uint32_t v)
{
    if (ch < 0 || ch >= 10) { host_log("dma: invalid channel %d\n", ch); return; }
    if (!(v & 0x100)) return;
    if (((v >> 2) & 3) == 1) run_chain(ch, ps2_regs.dma_tadr[ch], (v >> 6) & 1);
    else if (ps2_regs.dma_qwc[ch]) deliver(ch, DMA_PTR(ps2_regs.dma_madr[ch], (size_t)ps2_regs.dma_qwc[ch] * 16), ps2_regs.dma_qwc[ch]);
}
