#!/usr/bin/env python3
"""thumbs.py LOG OUTDIR: the scanout pictures (thumb lines, RGB332) of a log as PNGs, 3x enlarged."""
import re, sys, zlib, struct
from pathlib import Path
shots = {}
for line in open(sys.argv[1], 'rb'):
    m = re.search(rb'thumb (\d+) (\d+) ([0-9a-f]+)', line)
    if m: shots.setdefault(int(m[1]), {})[int(m[2])] = m[3].decode()
out = Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
Z = 3
for s, rows in sorted(shots.items()):
    h = max(rows) + 1; w = max(len(r) for r in rows.values()) // 2
    raw = bytearray()
    for y in range(h):
        r = rows.get(y, '').ljust(w * 2, '0')[:w * 2]; line = bytearray([0])
        for i in range(w):
            c = int(r[2 * i:2 * i + 2], 16)
            line += bytes([(c >> 5) * 36, ((c >> 2) & 7) * 36, (c & 3) * 85]) * Z
        raw += bytes(line) * Z
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    (out / f'{s}.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w * Z, h * Z, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))
    print(s, w, h, 'rows', len(rows))
# zoom lines: the top of the screen at 1/5 scale, brightness.
zooms = {}
for line in open(sys.argv[1], 'rb'):
    m = re.search(rb'zoom (\d+) (\d+) ([0-9a-f]+)', line)
    if m: zooms.setdefault(int(m[1]), {})[int(m[2])] = m[3].decode()
for s, rows in sorted(zooms.items()):
    h = max(rows) + 1; w = max(len(r) for r in rows.values())
    raw = bytearray()
    for y in range(h):
        r = rows.get(y, '').ljust(w, '0')[:w]; line = bytearray([0])
        for c in r: line += bytes([int(c, 16) * 17]) * 3
        raw += bytes(line)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    (out / f'zoom{s}.png').write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw))) + chunk(b'IEND', b''))
