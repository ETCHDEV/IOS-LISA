#!/usr/bin/env python3
"""Replace the framed SWEETXNU payload at the end of a v3 boot image's ramdisk.

Keeps the Android header, kernel and the ramdisk prefix (so payload alignment is
unchanged); rewrites the ramdisk size. Usage: replace-payload.py BASE.img PAYLOAD.bin OUT.img
"""
import pathlib, struct, sys
PAGE = 4096
al = lambda n: (n + PAGE - 1) & -PAGE
base, payload, out = sys.argv[1:4]
d = pathlib.Path(base).read_bytes()
assert d[:8] == b'ANDROID!' and struct.unpack_from('<I', d, 40)[0] == 3
ksize, rsize = struct.unpack_from('<II', d, 8)
roff = PAGE + al(ksize)
rd = d[roff:roff + rsize]
assert rd[-16:-8] == b'LISAEND1'
old = struct.unpack_from('<Q', rd, len(rd) - 8)[0]
start = len(rd) - 16 - old
assert rd[start:start + 8] == b'SWEETXNU'
p = pathlib.Path(payload).read_bytes()
assert p[:8] == b'SWEETXNU'
nrd = rd[:start] + p + b'LISAEND1' + struct.pack('<Q', len(p))
hdr = bytearray(d[:PAGE]); struct.pack_into('<I', hdr, 12, len(nrd))
with open(out, 'xb') as f:
    f.write(hdr); f.write(d[PAGE:roff])
    f.write(nrd); f.write(bytes(al(len(nrd)) - len(nrd)))
print(f'{out}: payload {old} -> {len(p)} bytes, ramdisk {rsize} -> {len(nrd)}')
