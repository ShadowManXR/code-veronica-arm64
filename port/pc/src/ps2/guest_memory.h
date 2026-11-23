/* ARM64 bridge for the PC port's 32-bit EE/DMA address model.
 * Guest addresses are integers, never truncated host pointers. This does not
 * change the layout of the decomp's pointer-bearing asset structures. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define RECVX_EE_WINDOW 0x30000000u
#define RECVX_EE_WINDOW_SIZE 0x10000000u
#define RECVX_EE_SCRATCH 0x70000000u
#define RECVX_EE_SCRATCH_SIZE 0x4000u
typedef uint32_t Ps2Address;
typedef struct { uint8_t* ram; uint8_t* scratch; } Ps2GuestMemory;
int ps2_guest_init(Ps2GuestMemory* memory);
void ps2_guest_destroy(Ps2GuestMemory* memory);
void* ps2_guest_resolve(const Ps2GuestMemory* memory, Ps2Address address, size_t size);
int ps2_guest_address(const Ps2GuestMemory* memory, const void* pointer, size_t size, Ps2Address* out);
/* Explicit little-endian pointer slots; alignment of disc data is not assumed. */
Ps2Address ps2_guest_read_address(const void* slot);
void ps2_guest_write_address(void* slot, Ps2Address address);
/* Checked conversion of an offset within an asset to an EE pointer slot. */
int ps2_guest_relocate(const Ps2GuestMemory* memory, Ps2Address base, size_t asset_size,
                       uint32_t offset, size_t object_size, Ps2Address* out);
#ifdef __cplusplus
}
#endif
