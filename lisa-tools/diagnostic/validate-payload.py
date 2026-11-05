#!/usr/bin/env python3
"""Offline structural checks only; this does not establish safe device RAM."""
import argparse
import struct

LIMIT = 1 << 64
TABLE = 0x2a8
PAGE = 0x4000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(data):
    require(len(data) >= TABLE, 'truncated header')
    magic, version, count = struct.unpack_from('<8sII', data)
    require(magic == b'SWEETXNU' and version == 1, 'bad magic or version')
    require(0 < count <= 64 and TABLE + count * 32 <= len(data), 'invalid segment table')
    virt, entry, adt_off, adt_size, kend, phys, size = struct.unpack_from('<7Q', data, 0x10)
    require(size > 0 and phys + size <= LIMIT, 'invalid physical range')
    require(kend >= virt and kend < LIMIT and (phys + kend - virt) % PAGE == 0, 'invalid kernel end')
    require(adt_size > 0 and adt_size <= 0xffffffff and adt_off % 8 == 0, 'invalid ADT')
    require(adt_off >= TABLE + count * 32 and adt_off + ((adt_size + 7) & ~7) <= len(data), 'ADT outside payload')
    require(b'\0' in data[0x48:TABLE], 'unterminated command line')
    ranges = []
    sources = []
    for i in range(count):
        va, vs, off, fs = struct.unpack_from('<4Q', data, TABLE + 32 * i)
        require(vs > 0 and va >= virt and va + vs <= LIMIT and va + vs <= kend, 'invalid segment address')
        require(va % 8 == 0 and vs % 8 == 0 and off % 8 == 0, 'unaligned segment')
        require(fs <= vs and (fs + 7) // 8 * 8 <= vs, 'segment copy exceeds destination')
        require(off >= TABLE + count * 32 and off + ((fs + 7) & ~7) <= len(data), 'segment outside payload')
        dest = phys + va - virt
        require(dest >= phys and dest + vs <= phys + size, 'segment outside target RAM')
        ranges.append((va, va + vs))
        if fs:
            sources.append((off, off + ((fs + 7) & ~7)))
    for spans in (ranges, sources + [(adt_off, adt_off + ((adt_size + 7) & ~7))]):
        spans.sort()
        require(all(a[1] <= b[0] for a, b in zip(spans, spans[1:])), 'overlapping ranges')
    require(any(a <= entry < b for a, b in ranges), 'entry outside segments')
    adt_pa = phys + kend - virt
    top = ((adt_pa + adt_size + PAGE - 1) & ~(PAGE - 1)) + PAGE
    require(phys <= adt_pa and top <= phys + size and top <= LIMIT, 'ADT or boot arguments outside target RAM')
    return count, phys, size, top


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('payload')
    args = ap.parse_args()
    try:
        with open(args.payload, 'rb') as f:
            result = validate(f.read())
    except (ValueError, struct.error) as exc:
        ap.exit(1, f'invalid payload: {exc}\n')
    print(f'structurally valid: {result[0]} segments, RAM {result[1]:#x}+{result[2]:#x}, top {result[3]:#x}')
    print('Offline check only: device RAM, live initrd, runtime loader and hardware safety remain unverified.')
