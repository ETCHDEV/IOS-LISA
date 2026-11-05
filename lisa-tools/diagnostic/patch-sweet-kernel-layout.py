#!/usr/bin/env python3
"""Put a rebuilt Mach-O into a known-good SWEETXNU payload without moving it.

The Lisa loader reserves a fixed payload layout containing an additional CTF
segment.  Preserve that layout, ADT, command line and CTF data byte-for-byte.
"""
import argparse
import pathlib
import struct
import importlib.util

spec = importlib.util.spec_from_file_location('repack', pathlib.Path(__file__).with_name('repack-sweet-kernel.py'))
repack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repack)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base_image')
    ap.add_argument('kernel')
    ap.add_argument('output')
    a = ap.parse_args()
    p = bytearray(repack.image_payload(a.base_image))
    count = struct.unpack_from('<I', p, 12)[0]
    table = [struct.unpack_from('<4Q', p, 0x2a8 + 32*i) for i in range(count)]
    segs, entry = repack.macho_segments(a.kernel)
    old = {va: (vs, off, fs) for va, vs, off, fs in table}
    copied = 0
    for va, vs, data in segs:
        if va not in old:
            raise ValueError(f'new segment {va:#x} is absent from the proven layout')
        old_vs, off, fs = old[va]
        if old_vs != vs or fs != len(data):
            raise ValueError(f'layout mismatch for {va:#x}: old {old_vs:#x}/{fs:#x}, new {vs:#x}/{len(data):#x}')
        p[off:off + fs] = data
        copied += 1
    if copied != len(segs):
        raise ValueError('not all Mach-O segments copied')
    old_entry = struct.unpack_from('<Q', p, 0x18)[0]
    if old_entry != entry:
        raise ValueError(f'entry mismatch: old {old_entry:#x}, new {entry:#x}')
    pathlib.Path(a.output).write_bytes(p)
    print(f'{a.output}: patched {copied} segments in fixed {len(p)} byte layout')


if __name__ == '__main__':
    main()
