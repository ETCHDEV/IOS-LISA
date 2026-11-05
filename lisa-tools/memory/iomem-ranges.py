#!/usr/bin/env python3
"""Summarize unreserved high RAM from a saved Linux /proc/iomem snapshot.

This is an offline aid, not proof that ranges are safe in another boot mode.
Indented reserved entries are subtracted from their System RAM parent.
"""
import argparse
import pathlib
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('snapshot', type=pathlib.Path)
args = parser.parse_args()
pattern = re.compile(r'^(\s*)([0-9a-fA-F]+)-([0-9a-fA-F]+)\s*:\s*(.*)$')
ram, reserved = [], []
for line in args.snapshot.read_text().splitlines():
    match = pattern.match(line)
    if not match:
        continue
    indent, start, end, name = match.groups()
    start, end = int(start, 16), int(end, 16) + 1
    if name == 'System RAM' and not indent:
        ram.append((start, end))
    elif name == 'reserved' and indent:
        reserved.append((start, end))

ram.sort()
merged = []
for start, end in ram:
    if merged and start <= merged[-1][1]:
        merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
    else:
        merged.append((start, end))
for start, end in merged:
    start = max(start, 0x100000000)
    if start >= end:
        continue
    pieces = [(start, end)]
    for lo, hi in reserved:
        new = []
        for a, b in pieces:
            if hi <= a or lo >= b:
                new.append((a, b))
            else:
                if a < lo: new.append((a, lo))
                if hi < b: new.append((hi, b))
        pieces = new
    for a, b in pieces:
        if b - a >= 0x100000:
            print(f'{a:#x}..{b:#x}  {(b-a) / 1024**2:.1f} MiB')
