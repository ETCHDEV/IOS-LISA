#!/usr/bin/env python3
"""Boot image for xnu-12377 on Lisa: base image's header and ramdisk prefix, a new loader Image, a new SWEETXNU payload.

usage: mk-lisa26-image.py BASE.img LOADER_Image KERNEL.macho ADT OUT.img --cmdline "..." [--phys P] [--mem M]
Payload layout is the one diagnostic/payload_validate.c checks.
"""
import argparse, pathlib, struct

PAGE, K16, TABLE = 4096, 0x4000, 0x448  # 1024-byte stored line (LISA_PAYLOAD_LINE)
LC_SEGMENT_64, LC_UNIXTHREAD = 0x19, 0x5
al = lambda v, n: (v + n - 1) & -n

p = argparse.ArgumentParser(description=__doc__)
for name in ('base', 'loader', 'kernel', 'adt', 'out'):
    p.add_argument(name)
p.add_argument('--cmdline', required=True)
p.add_argument('--data-seed', help='packed Data seed to place after the kernel, for the PackedRAMDisk placeholder')
p.add_argument('--trust-cache', help='bundle to place after the kernel; the ADT needs its memory-map/TrustCache placeholder')
p.add_argument('--gpu-firmware', help='packed LISAGPUF payload; the ADT needs its memory-map/LisaGPUFirmware placeholder')
p.add_argument('--phys', type=lambda v: int(v, 0), default=None)
p.add_argument('--mem', type=lambda v: int(v, 0), default=None)
a = p.parse_args()

k = pathlib.Path(a.kernel).read_bytes()
assert struct.unpack_from('<I', k, 0)[0] == 0xfeedfacf
ncmds = struct.unpack_from('<I', k, 16)[0]
off, segs, entry = 32, [], None
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', k, off)
    if cmd == LC_SEGMENT_64:
        va, vs, fo, fs = struct.unpack_from('<4Q', k, off + 24)
        if vs:
            segs.append((va, vs, k[fo:fo + fs]))
    elif cmd == LC_UNIXTHREAD:
        entry = struct.unpack_from('<Q', k, off + 16 + 32 * 8)[0]
    off += size
assert segs and entry is not None and len(segs) <= 62
virt = min(s[0] for s in segs)
# Like DarwinBoot's AllocateWindow: a 2 MiB boundary plus the kernel's own offset in its block; a 4 GiB window.
if a.phys is None:
    a.phys = 0x180000000 + (virt & 0x1fffff)
if a.mem is None:
    a.mem = 0x100000000 - (virt & 0x1fffff)
kend = al(max(s[0] + s[1] for s in segs), K16)
# zzz-lisa.patch's BOOTSTRAP_TABLE_SIZE: 24 pages; start.s does not check it.
assert a.phys + a.mem <= 0x280000000
# start.s maps the kernel with 2 MiB blocks: VA and PA must agree below 2 MiB (DarwinBoot's AllocateWindow does the same).
assert (a.phys - virt) % 0x200000 == 0 and virt % PAGE == 0

adt = bytearray(pathlib.Path(a.adt).read_bytes())
def place(path, name):
    # DarwinBoot's job: a physical range inside the window below topOfKernelData, the placeholder holding its (PA, size).
    global kend
    data = pathlib.Path(path).read_bytes()
    va = kend
    size = al(len(data), K16)
    segs.append((va, size, data))
    kend = va + size
    key = name.ljust(32, b'\0') + struct.pack('<I', 16)
    assert adt.count(key) == 1
    at = adt.index(key) + len(key)
    assert adt[at:at + 16] == bytes(16)
    struct.pack_into('<QQ', adt, at, a.phys + (va - virt), len(data))

if a.data_seed:
    place(a.data_seed, b'PackedRAMDisk')
if a.trust_cache:
    # DarwinBoot's job: a physical range inside the window below topOfKernelData, the placeholder holding its (PA, size).
    trust = pathlib.Path(a.trust_cache).read_bytes()
    va = kend
    size = al(len(trust), K16)
    segs.append((va, size, trust))
    kend = va + size
    key = b'TrustCache'.ljust(32, b'\0') + struct.pack('<I', 16)
    assert adt.count(key) == 1
    at = adt.index(key) + len(key)
    assert adt[at:at + 16] == bytes(16)
    struct.pack_into('<QQ', adt, at, a.phys + (va - virt), len(trust))
if a.gpu_firmware:
    place(a.gpu_firmware, b'LisaGPUFirmware')
cmdline = a.cmdline.encode()
assert len(cmdline) < TABLE - 0x48
blob = bytearray(TABLE + len(segs) * 32)
table = []
for va, vs, data in segs:
    blob.extend(bytes(al(len(blob), 8) - len(blob)))
    table.append((va, vs, len(blob), len(data)))
    blob.extend(data)
adt_off = al(len(blob), 8)
blob.extend(bytes(adt_off - len(blob)))
blob.extend(adt)
blob.extend(bytes(al(len(blob), 8) - len(blob)))
struct.pack_into('<8sII', blob, 0, b'SWEETXNU', 1, len(table))
struct.pack_into('<7Q', blob, 0x10, virt, entry, adt_off, len(adt), kend, a.phys, a.mem)
blob[0x48:0x48 + len(cmdline)] = cmdline
for i, entry_row in enumerate(table):
    struct.pack_into('<4Q', blob, TABLE + i * 32, *entry_row)

d = pathlib.Path(a.base).read_bytes()
assert d[:8] == b'ANDROID!' and struct.unpack_from('<I', d, 40)[0] == 3
ksize, rsize = struct.unpack_from('<II', d, 8)
roff = PAGE + al(ksize, PAGE)
rd = d[roff:roff + rsize]
assert rd[-16:-8] == b'LISAEND1'
old = struct.unpack_from('<Q', rd, len(rd) - 8)[0]
start = len(rd) - 16 - old
assert rd[start:start + 8] == b'SWEETXNU'
nrd = rd[:start] + bytes(blob) + b'LISAEND1' + struct.pack('<Q', len(blob))
image = pathlib.Path(a.loader).read_bytes()
assert image[56:60] == b'ARM\x64' and struct.unpack_from('<Q', image, 16)[0] == len(image)
hdr = bytearray(d[:PAGE])
struct.pack_into('<II', hdr, 8, len(image), len(nrd))
pad = lambda b: b + bytes(-len(b) % PAGE)
pathlib.Path(a.out).write_bytes(bytes(hdr) + pad(image) + pad(nrd))
pathlib.Path(a.out + '.window').write_text(f'{a.phys:#x} {a.mem:#x}\n')
print(f'{a.out}: virt {virt:#x} entry {entry:#x} end {kend:#x} phys {a.phys:#x} mem {a.mem:#x}, {len(table)} segments, '
      f'payload {len(blob)}, ramdisk {len(nrd)}, kernel field {len(image)}')
