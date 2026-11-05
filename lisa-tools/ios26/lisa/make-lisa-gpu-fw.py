#!/usr/bin/env python3
"""Lisa's own GPU firmware for the ADT's /chosen zeta-gpu-*: gmu and sqe as they are; the zap shader's mdt and bNN files squashed into one ELF."""
import hashlib, shutil, struct
from pathlib import Path
T = Path(__file__).resolve().parents[2]
F = T / 'lisa-firmware/gpu'; O = Path(__file__).resolve().parent / 'out/gpu-fw'; O.mkdir(parents=True, exist_ok=True)
for n in ('a660_gmu.bin', 'a660_sqe.fw'): shutil.copy(F / n, O / n)
mdt = (F / 'a660_zap.mdt').read_bytes()
assert mdt[:5] == b'\x7fELF\x01'
phoff = struct.unpack_from('<I', mdt, 28)[0]; phent, phnum = struct.unpack_from('<HH', mdt, 42)
size = max(struct.unpack_from('<I', mdt, phoff + i * phent + 4)[0] + struct.unpack_from('<I', mdt, phoff + i * phent + 16)[0] for i in range(phnum))
elf = bytearray(max(size, len(mdt)))
elf[:len(mdt)] = mdt
for i in range(phnum):
    off, filesz = struct.unpack_from('<I', mdt, phoff + i * phent + 4)[0], struct.unpack_from('<I', mdt, phoff + i * phent + 16)[0]
    p = F / f'a660_zap.b{i:02d}'
    if filesz and p.exists():
        d = p.read_bytes(); assert len(d) == filesz, (i, len(d), filesz); elf[off:off + filesz] = d
(O / 'a660_zap.elf').write_bytes(elf)
for n in ('a660_gmu.bin', 'a660_sqe.fw', 'a660_zap.elf'):
    print(n, (O / n).stat().st_size, hashlib.sha256((O / n).read_bytes()).hexdigest())
