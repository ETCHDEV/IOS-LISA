#!/usr/bin/env python3
"""Check Lisa's locally captured ELF32 MDT segments without loading firmware."""
from pathlib import Path
import struct
p = Path(__file__).resolve().parents[1] / 'lisa-firmware' / 'adsp'
blob = (p / 'adsp.mdt').read_bytes()
if len(blob) < 52 or blob[:6] != b'\x7fELF\x01\x01':
    raise SystemExit('invalid ELF32 little-endian metadata')
phoff = struct.unpack_from('<I', blob, 28)[0]
entsize, count = struct.unpack_from('<HH', blob, 42)
if phoff != 52 or entsize != 32 or not 3 <= count <= 48 or len(blob) > 16384 or phoff + count * entsize > len(blob):
    raise SystemExit('metadata incompatible with HackiOS ADSP reader')
hashes = 0
loaded = []
for i in range(count):
    typ, off, vaddr, addr, filesz, memsz, flags, align = struct.unpack_from('<8I', blob, phoff + i * entsize)
    if (flags >> 24) & 7 == 2:
        hashes = filesz
        continue
    if typ != 1 or memsz == 0:
        continue
    if addr < 0x86700000 or filesz > memsz or addr + memsz > 0x8a700000:
        raise SystemExit(f'segment {i}: outside Lisa ADSP carveout')
    if filesz:
        f = p / f'adsp.b{i:02}'
        if not f.is_file() or f.stat().st_size != filesz:
            raise SystemExit(f'segment {i}: missing or unexpected size')
    loaded.append((addr, addr + memsz))
if not loaded or min(a for a, _ in loaded) != 0x86700000 or phoff + count * entsize + hashes != len(blob):
    raise SystemExit('metadata layout does not match HackiOS ADSP loader assumptions')
print(f'PASS: {len(loaded)} segments within Lisa carveout; metadata and segment sizes match')
