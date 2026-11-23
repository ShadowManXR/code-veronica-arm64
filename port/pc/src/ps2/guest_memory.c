#include "guest_memory.h"
#include <sys/mman.h>
#include <string.h>

int ps2_guest_init(Ps2GuestMemory* m)
{
    if (!m || m->ram || m->scratch) return 0;
    void* ram = mmap(NULL, RECVX_EE_WINDOW_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ram == MAP_FAILED) return 0;
    void* scratch = mmap(NULL, RECVX_EE_SCRATCH_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (scratch == MAP_FAILED) { munmap(ram, RECVX_EE_WINDOW_SIZE); return 0; }
    m->ram = ram; m->scratch = scratch;
    return 1;
}
void ps2_guest_destroy(Ps2GuestMemory* m)
{
    if (!m) return;
    if (m->ram) munmap(m->ram, RECVX_EE_WINDOW_SIZE);
    if (m->scratch) munmap(m->scratch, RECVX_EE_SCRATCH_SIZE);
    memset(m, 0, sizeof(*m));
}
void* ps2_guest_resolve(const Ps2GuestMemory* m, Ps2Address address, size_t size)
{
    if (!m || !m->ram || !m->scratch) return NULL;
    uint32_t offset;
    /* A DMA tag's high bit selects SPR; low 14 bits are its offset. */
    if ((address & 0x80000000u) || (address & 0xffffc000u) == RECVX_EE_SCRATCH) {
        offset = address & (RECVX_EE_SCRATCH_SIZE - 1);
        return size <= RECVX_EE_SCRATCH_SIZE - offset ? m->scratch + offset : NULL;
    }
    /* Physical and the PC port's cached/uncached EE window aliases. */
    uint32_t region = address & 0xf0000000u;
    if (region != 0 && region != 0x20000000u && region != RECVX_EE_WINDOW) return NULL;
    offset = address & (RECVX_EE_WINDOW_SIZE - 1);
    return size <= RECVX_EE_WINDOW_SIZE - offset ? m->ram + offset : NULL;
}
int ps2_guest_address(const Ps2GuestMemory* m, const void* pointer, size_t size, Ps2Address* out)
{
    if (!m || !pointer || !out) return 0;
    uintptr_t p = (uintptr_t)pointer;
    if (m->ram && p >= (uintptr_t)m->ram && p - (uintptr_t)m->ram < RECVX_EE_WINDOW_SIZE) {
        size_t offset = p - (uintptr_t)m->ram;
        if (size > RECVX_EE_WINDOW_SIZE - offset) return 0;
        *out = RECVX_EE_WINDOW + (uint32_t)offset; return 1;
    }
    if (m->scratch && p >= (uintptr_t)m->scratch && p - (uintptr_t)m->scratch < RECVX_EE_SCRATCH_SIZE) {
        size_t offset = p - (uintptr_t)m->scratch;
        if (size > RECVX_EE_SCRATCH_SIZE - offset) return 0;
        *out = RECVX_EE_SCRATCH + (uint32_t)offset; return 1;
    }
    return 0;
}
Ps2Address ps2_guest_read_address(const void* slot)
{
    const uint8_t* b = slot;
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
void ps2_guest_write_address(void* slot, Ps2Address address)
{
    uint8_t* b = slot;
    for (int i = 0; i < 4; ++i) b[i] = (uint8_t)(address >> (8 * i));
}
int ps2_guest_relocate(const Ps2GuestMemory* m, Ps2Address base, size_t asset_size,
                       uint32_t offset, size_t object_size, Ps2Address* out)
{
    if (!out || offset > asset_size || object_size > asset_size - offset ||
        !ps2_guest_resolve(m, base, asset_size) || offset > UINT32_MAX - base) return 0;
    Ps2Address address = base + offset;
    if (!ps2_guest_resolve(m, address, object_size)) return 0;
    *out = address; return 1;
}
