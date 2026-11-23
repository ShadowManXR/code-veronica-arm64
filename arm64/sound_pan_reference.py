"""Capture bounded pan-table reference bytes from the user's retail ELF."""
import argparse
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'port/pc/tools'))
from rdx import Iso


def generate(iso, output):
    lsn, size = iso.find('SLUS_201.84')
    elf = iso.read(lsn, (size + 2047) // 2048)[:size]
    source = (Path(__file__).resolve().parents[1] /
              'port/decomp/src/ps2/veronica/prog/sdfunc.c').read_text()
    lines = ['/* Generated from the user-owned retail executable; tests only. */']
    for name in ('PanTbl360', 'PanTbl360Vol'):
        body = re.search(r'char ' + name + r'\[[^]]+\][^{]*\{([^}]+)', source).group(1)
        pattern = bytes(int(v.strip(), 0) & 255 for v in body.split(',') if v.strip())
        if len(pattern) != 68:
            raise ValueError('Unexpected source pan table extent')
        at = elf.find(pattern)
        if at < 0 or elf.find(pattern, at + 1) >= 0:
            raise ValueError('Retail pan table must match uniquely')
        reference = elf[at:at + 72]
        if len(reference) != 72 or reference[68:] != bytes(4):
            raise ValueError('Unexpected retail pan-table padding')
        lines.append(f'static const unsigned char retail_{name}[72] = {{' +
                     ','.join(str(v) for v in reference) + '};')
        print(f'{name}: original 68 bytes plus four verified zero bytes at ELF offset {at:#x}')
    output.write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--iso', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    iso = Iso(args.iso)
    try:
        generate(iso, args.output)
    finally:
        iso.f.close()
