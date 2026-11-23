#include "ps2_platform.h"
#include "ps2/guest_memory.h"
static Ps2GuestMemory memory;
Ps2FakeRegs ps2_regs;
void ps2_memory_init(void) {
    if (!memory.ram && !ps2_guest_init(&memory)) host_fatal("Cannot reserve translated EE memory");
}
void* ps2_dma_ptr_sized(uint32_t address, size_t size) {
    void* p = ps2_guest_resolve(&memory, address, size);
    if (!p) host_fatal("Invalid EE address range: %08x + %zu", address, size);
    return p;
}
void* ps2_dma_ptr(uint32_t address) { return ps2_dma_ptr_sized(address, 1); }
uint32_t ps2_dma_address(const void* p, size_t size) {
    Ps2Address address;
    if (!ps2_guest_address(&memory, p, size, &address)) host_fatal("DMA pointer is outside translated EE memory");
    return address;
}
