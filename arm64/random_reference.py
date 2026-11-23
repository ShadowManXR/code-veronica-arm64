"""Private RNG vectors executed from the user's retail EE instructions."""
import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'port/pc/tools'))
from rdx import Iso

def generate(iso, output):
    lsn, size = iso.find('SLUS_201.84')
    elf = iso.read(lsn, (size + 2047) // 2048)[:size]
    ph = struct.unpack_from('<I', elf, 28)[0]
    width, count = struct.unpack_from('<HH', elf, 42)
    segments = [struct.unpack_from('<8I', elf, ph + i * width) for i in range(count)]
    def word(address):
        for kind, offset, base, _, length, *_ in segments:
            if kind == 1 and base <= address <= base + length - 4:
                return struct.unpack_from('<I', elf, offset + address - base)[0]
        raise ValueError(f'Unmapped EE address {address:x}')
    impure = word(0x3037f4)
    memory = {0x3037f4: impure, impure + 88: word(impure + 88)}
    assert memory[impure + 88] == 1, 'Unexpected retail default random state'
    def execute(pc, argument=0):
        r = [0] * 32
        r[4] = argument
        returning = False
        for _ in range(32):
            ins = word(pc); pc += 4
            op, rs, rt, rd = ins >> 26, ins >> 21 & 31, ins >> 16 & 31, ins >> 11 & 31
            imm = ins & 65535
            signed = imm if imm < 32768 else imm - 65536
            delay = returning
            if op == 15: r[rt] = imm << 16
            elif op == 13: r[rt] = r[rs] | imm
            elif op == 35: r[rt] = memory[(r[rs] + signed) & 0xffffffff]
            elif op == 43: memory[(r[rs] + signed) & 0xffffffff] = r[rt]
            elif op == 9: r[rt] = (r[rs] + signed) & 0xffffffff
            elif op == 0 and ins & 63 == 24: r[rd] = (r[rs] * r[rt]) & 0xffffffff  # EE mult rd,rs,rt
            elif op == 0 and ins & 63 == 36: r[rd] = r[rs] & r[rt]
            elif op == 0 and ins & 63 == 8 and rs == 31: returning = True
            else: raise ValueError(f'Unexpected retail RNG instruction {ins:08x}')
            r[0] = 0
            if delay: return r[2]
        raise ValueError('Retail RNG failed to return')
    def f32(x): return struct.unpack('<f', struct.pack('<f', x))[0]
    # Include seeds that produce every 0..100 bucket and the float-rounded 1.0 edge.
    inverse = pow(1103515245, -1, 1 << 32)
    seeds = [(x, 256) for x in (0, 1, 0xffffffff, 0x80000000, 0x12345678)]
    seeds += [(((min((i * (1 << 31)) // 100, 0x7fffffff) - 12345) * inverse) & 0xffffffff, 2) for i in range(101)]
    lines = [f'RNGREF 1 {len(seeds)}']
    for seed, calls in seeds:
        execute(0x125310, seed)
        lines.append(f'SEED {seed} {calls}')
        for _ in range(calls):
            value = execute(0x125320)
            # Preserve the original negative denominator, including -0.0 when
            # rand returns zero; RandTest exposes the float's raw bits.
            fraction = f32(f32(-value) / f32(-2147483600.0))
            bits = struct.unpack('<I', struct.pack('<f', fraction))[0]
            lines.append(f'{value} {bits} {int(f32(100.0 * fraction))}')
    Path(output).write_text('\n'.join(lines) + '\n', encoding='ascii')

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--iso', required=True)
    p.add_argument('--output', required=True)
    args = p.parse_args()
    generate(Iso(args.iso), args.output)
