/* Tests the real DMA walker with recording VIF/GIF peripherals, without game code. */
#include "ps2_platform.h"
#include "ps2/guest_memory.h"
#include "ps2/guest_layout.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__aarch64__) || !defined(__ANDROID__)
#error This check must be built for Android ARM64
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static jmp_buf fatal_jump;
static int expect_fatal;
static uint32_t delivered[256];
static size_t delivered_words;
static void record(const uint32_t* data, size_t words) {
    if (delivered_words + words > 256) abort();
    memcpy(delivered + delivered_words, data, words * 4); delivered_words += words;
}
void host_log(const char* fmt, ...) { (void)fmt; }
void host_fatal(const char* fmt, ...) {
    if (expect_fatal) longjmp(fatal_jump, 1);
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); abort();
}
void ps2_poll_interrupts(void) {}
void ps2_vif1_transfer(const uint32_t* data, uint32_t count) { record(data, (size_t)count * 4); }
void ps2_vif1_transfer_words(const uint32_t* data, uint32_t count) { record(data, count); }
void ps2_gif_transfer(int path, const uint32_t* data, uint32_t count) { if (path != 3) abort(); record(data, (size_t)count * 4); }
static uint32_t* ptr(uint32_t address, size_t size) { return ps2_dma_ptr_sized(address, size); }
static void tag(uint32_t address, unsigned id, unsigned qwc, uint32_t target) {
    uint32_t* p = ptr(address, 16); p[0] = id << 28 | qwc; p[1] = target; p[2] = p[3] = 0;
}
int main(void)
{
    CHECK(sizeof(void*) == 8 && sizeof(Ps2Address) == 4);
    Ps2GuestMemory memory = {0}; CHECK(ps2_guest_init(&memory));
    void* p = ps2_guest_resolve(&memory, 0x30001234, 4); CHECK(p);
    CHECK(p == ps2_guest_resolve(&memory, 0x1234, 4));
    CHECK(p == ps2_guest_resolve(&memory, 0x20001234, 4));
    Ps2Address address = 0; CHECK(ps2_guest_address(&memory, p, 4, &address) && address == 0x30001234);
    CHECK(!ps2_guest_address(&memory, &address, 4, &address));
    CHECK(!ps2_guest_resolve(&memory, 0x3fffffff, 2));
    CHECK(!ps2_guest_resolve(&memory, 0x40000000, 1));
    CHECK(!ps2_guest_resolve(&memory, 0x10008000, 4));
    CHECK(!ps2_guest_resolve(&memory, 0x70003ff8, 16));
    CHECK(ps2_guest_resolve(&memory, 0x70000020, 16) == ps2_guest_resolve(&memory, 0x80000020, 16));
    CHECK(ps2_guest_relocate(&memory, 0x30001000, 128, 32, 16, &address) && address == 0x30001020);
    CHECK(!ps2_guest_relocate(&memory, 0x30001000, 128, 120, 16, &address));
    Ps2ChunkModel32* model = ps2_guest_resolve(&memory, 0x30002000, sizeof(*model));
    model->vlist = 0x30003000; model->plist = 0x30004000; model->radius = 10;
    CHECK(ps2_guest_read_address(model) == 0x30003000);
    CHECK(ps2_guest_resolve(&memory, model->plist, 2) == memory.ram + 0x4000);
    uint8_t slot[6] = {0}; ps2_guest_write_address(slot + 1, 0xfedcba98);
    CHECK(ps2_guest_read_address(slot + 1) == 0xfedcba98 && slot[0] == 0 && slot[5] == 0);
    ps2_guest_destroy(&memory); CHECK(!ps2_guest_resolve(&memory, 0x100, 4));
    puts("PASS ARM64 EE aliases, scratchpad, relocation slots, ranges and host-pointer rejection");

    ps2_memory_init();
    void* channel = sceDmaGetChan(2);
    // CNT followed by END, with different payloads in the same translated arena.
    tag(0x30001000, 1, 1, 0); tag(0x30001020, 7, 1, 0);
    for (int i = 0; i < 4; ++i) { ptr(0x30001010, 16)[i] = 10 + i; ptr(0x30001030, 16)[i] = 20 + i; }
    CHECK(sceDmaSend(channel, ptr(0x30001000, 16)) == 0);
    CHECK(delivered_words == 8 && delivered[0] == 10 && delivered[7] == 23);
    // REF using the physical alias, followed by REFE using a scratchpad DMA bit.
    delivered_words = 0;
    tag(0x30002000, 3, 1, 0x1010); tag(0x30002010, 0, 1, 0x80000020);
    for (int i = 0; i < 4; ++i) ptr(0x70000020, 16)[i] = 30 + i;
    sceDmaSend(channel, ptr(0x30002000, 16));
    CHECK(delivered_words == 8 && delivered[0] == 10 && delivered[7] == 33);
    // CALL/RET returns to the caller's END tag; NEXT skips unused space.
    delivered_words = 0;
    tag(0x30003000, 5, 0, 0x30004000); tag(0x30003010, 7, 1, 0);
    tag(0x30004000, 2, 0, 0x30005000); tag(0x30005000, 6, 1, 0);
    for (int i = 0; i < 4; ++i) { ptr(0x30003020, 16)[i] = 40 + i; ptr(0x30005010, 16)[i] = 50 + i; }
    sceDmaSend(channel, ptr(0x30003000, 16));
    CHECK(delivered_words == 8 && delivered[0] == 50 && delivered[7] == 43);
    // A RET stored in SPR must restore the caller's RAM address, not wrap into SPR.
    delivered_words = 0;
    tag(0x30003000, 5, 0, 0x80000040); tag(0x70000040, 6, 1, 0);
    for (int i = 0; i < 4; ++i) ptr(0x70000050, 16)[i] = 60 + i;
    sceDmaSend(channel, ptr(0x30003000, 16));
    CHECK(delivered_words == 8 && delivered[0] == 60 && delivered[7] == 43);
    // The VIF path consumes the upper 64 tag bits before inline data.
    delivered_words = 0;
    tag(0x30006000, 7, 1, 0);
    ptr(0x30006000, 16)[2] = 0x12345678;
    for (int i = 0; i < 4; ++i) ptr(0x30006010, 16)[i] = 70 + i;
    sceDmaSend(sceDmaGetChan(1), ptr(0x30006000, 16));
    CHECK(delivered_words == 6 && delivered[0] == 0x12345678 && delivered[5] == 73);
    // Direct register kick and normal mode both resolve the translated source.
    delivered_words = 0;
    ps2_regs.dma_madr[2] = 0x30001010; ps2_regs.dma_qwc[2] = 1;
    ps2_dma_put_chcr(2, 0x100);
    CHECK(sceDmaSendN(channel, ptr(0x30001030, 16), 1) == 0);
    CHECK(delivered_words == 8 && delivered[0] == 10 && delivered[7] == 23);
    CHECK(sceDmaSendN(channel, p, -1) == -1);
    CHECK(sceDmaSendN(channel, NULL, 0) == 0);
    tag(0x30007000, 0, 0, 0x10008000);
    CHECK(sceDmaSend(channel, ptr(0x30007000, 16)) == 0 && delivered_words == 8);
    // The scratchpad's last tag cannot carry an inline qword past its end.
    tag(0x70003ff0, 7, 1, 0);
    expect_fatal = 1;
    if (!setjmp(fatal_jump)) { sceDmaSend(channel, ptr(0x70003ff0, 16)); CHECK(0); }
    expect_fatal = 0;
    puts("PASS upstream DMA CNT/END, REF/REFE, CALL/RET/NEXT, register and normal transfers; invalid payload rejected");
    return 0;
}
