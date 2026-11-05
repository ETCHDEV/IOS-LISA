#!/usr/bin/env python3
"""Make an experimental RAM-size-only copy of Lisa's established ADT.

The caller must independently validate the range against the target boot's
memory ownership. This tool cannot establish hardware accessibility.
"""
import argparse
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / 'lisa-build/tools'))
import adt

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('input', type=pathlib.Path)
p.add_argument('output', type=pathlib.Path)
p.add_argument('--size', type=lambda value: int(value, 0), required=True)
a = p.parse_args()
if not 0x20000000 <= a.size <= 0x70000000 or a.size % 0x2000000:
    p.error('size must be 512 MiB to 1792 MiB, aligned to 32 MiB')
blob = a.input.read_bytes()
root, end = adt.parse(blob)
if end != len(blob):
    p.error('trailing ADT data')
chosen = next((node for node in root.children if node.name == 'chosen'), None)
if chosen is None or chosen.prop('dram-base') != struct.pack('<Q', 0x180000000):
    p.error('unexpected RAM base')
if chosen.prop('dram-size') != struct.pack('<Q', 0x20000000):
    p.error('unexpected source RAM size')
chosen.props = [(name, flags, struct.pack('<Q', a.size) if name == 'dram-size' else value)
                for name, flags, value in chosen.props]
with a.output.open('xb') as output:
    output.write(adt.serialize(root))
print(f'ADT declares {a.size // 1024**2} MiB at 0x180000000; runtime ownership unverified')
