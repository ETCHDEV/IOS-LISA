#!/usr/bin/env python3
"""Replace the Mach-O kernel carried by a framed SWEETXNU boot image.

The payload also carries Lisa's live device tree and platform additions.  Those
are copied verbatim; only Mach-O loadable segments and the entry address are
rebuilt from the supplied kernel.
"""
import argparse
import pathlib
import struct

PAGE = 0x4000
TABLE = 0x2A8
LC_SEGMENT_64 = 0x19
LC_MAIN = 0x80000028
LC_UNIXTHREAD = 0x5


def align(v, n):
    return (v + n - 1) & -n


def image_payload(image):
    data = pathlib.Path(image).read_bytes()
    kernel_size, ramdisk_size = struct.unpack_from('<II', data, 8)
    ramdisk_off = 4096 + align(kernel_size, 4096)
    ramdisk = data[ramdisk_off:ramdisk_off + ramdisk_size]
    if ramdisk[-16:-8] != b'LISAEND1':
        raise ValueError('image does not contain a framed payload')
    size = struct.unpack_from('<Q', ramdisk, len(ramdisk) - 8)[0]
    payload = ramdisk[len(ramdisk) - 16 - size:len(ramdisk) - 16]
    if payload[:8] != b'SWEETXNU':
        raise ValueError('framed data is not SWEETXNU')
    return payload


def macho_segments(kernel):
    b = pathlib.Path(kernel).read_bytes()
    magic, _, _, _, ncmds, sizeofcmds, _, _ = struct.unpack_from('<IiiIIIII', b, 0)
    if magic != 0xfeedfacf:
        raise ValueError('kernel is not a 64-bit Mach-O')
    off, end = 32, 32 + sizeofcmds
    segs, entryoff = [], None
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', b, off)
        if cmdsize < 8 or off + cmdsize > end:
            raise ValueError('invalid Mach-O command')
        if cmd == LC_SEGMENT_64:
            _, _, rawname, vmaddr, vmsize, fileoff, filesize, _, _, _, _ = struct.unpack_from('<II16sQQQQiiII', b, off)
            name = rawname.split(b'\0', 1)[0]
            if name != b'__PAGEZERO' and vmsize:
                if fileoff + filesize > len(b):
                    raise ValueError('segment outside Mach-O')
                segs.append((vmaddr, vmsize, b[fileoff:fileoff + filesize]))
        elif cmd == LC_MAIN:
            entryoff = struct.unpack_from('<QQ', b, off)[0]
        elif cmd == LC_UNIXTHREAD and cmdsize >= 280:
            # arm_thread_state64 starts after cmd/flavor/count: x0..x28,
            # fp, lr, sp, pc.  XNU links the kernel with this form rather
            # than LC_MAIN.
            entry = struct.unpack_from('<Q', b, off + 16 + 32 * 8)[0]
        off += cmdsize
    if not segs or (entryoff is None and entry is None):
        raise ValueError('missing segments or entry command')
    # LC_MAIN is a file offset. Convert it through the corresponding segment.
    if entry is None:
        off = 32
        for _ in range(ncmds):
            cmd, cmdsize = struct.unpack_from('<II', b, off)
            if cmd == LC_SEGMENT_64:
                _, _, rawname, vmaddr, _, fileoff, filesize, _, _, _, _ = struct.unpack_from('<II16sQQQQiiII', b, off)
                if fileoff <= entryoff < fileoff + filesize:
                    entry = vmaddr + entryoff - fileoff
                    break
            off += cmdsize
    if entry is None:
        raise ValueError('LC_MAIN is outside a mapped segment')
    return segs, entry


def main():
    p = argparse.ArgumentParser()
    p.add_argument('base_image')
    p.add_argument('kernel')
    p.add_argument('payload_out')
    a = p.parse_args()
    old = image_payload(a.base_image)
    magic, version, old_count = struct.unpack_from('<8sII', old, 0)
    if magic != b'SWEETXNU' or version != 1:
        raise ValueError('invalid old SWEETXNU payload')
    old_hdr = list(struct.unpack_from('<7Q', old, 0x10))
    old_adt_off, old_adt_size = old_hdr[2], old_hdr[3]
    adt = old[old_adt_off:old_adt_off + old_adt_size]
    cmdline = old[0x48:TABLE].split(b'\0', 1)[0]
    segs, entry = macho_segments(a.kernel)
    # The proven Lisa payload carries firmware outside the Mach-O segments.
    # A kernel-only repack must retain it: Adreno step 12 reads LISAGPUF there.
    for i in range(old_count):
        va, vs, off, fs = struct.unpack_from('<4Q', old, TABLE + i * 32)
        if old[off:off + 8] != b'LISAGPUF':
            continue
        if any(va < other_va + other_vs and other_va < va + vs for other_va, other_vs, _ in segs):
            raise ValueError('firmware segment overlaps rebuilt kernel')
        segs.append((va, vs, old[off:off + fs]))
    if len(segs) > 64:
        raise ValueError('too many Mach-O segments')
    table_end = TABLE + len(segs) * 32
    blob = bytearray(b'\0' * table_end)
    cursor = align(table_end, 8)
    table = []
    for va, vs, data in segs:
        cursor = align(cursor, 8)
        data_off = cursor
        blob.extend(b'\0' * (data_off - len(blob)))
        blob.extend(data)
        cursor = len(blob)
        table.append((va, vs, data_off, len(data)))
    adt_off = align(len(blob), 8)
    blob.extend(b'\0' * (adt_off - len(blob)))
    blob.extend(adt)
    kend = max(va + vs for va, vs, _ in segs)
    virt = old_hdr[0]
    phys, mem_size = old_hdr[5], old_hdr[6]
    struct.pack_into('<8sII', blob, 0, b'SWEETXNU', 1, len(table))
    struct.pack_into('<7Q', blob, 0x10, virt, entry, adt_off, len(adt), kend, phys, mem_size)
    blob[0x48:TABLE] = b'\0' * (TABLE - 0x48)
    blob[0x48:0x48 + len(cmdline)] = cmdline
    for i, ent in enumerate(table):
        struct.pack_into('<4Q', blob, TABLE + i * 32, *ent)
    # Keep the DTB at the advertised offset even if an earlier fixed-size
    # header write happened to trim a bytearray tail on a host Python build.
    need = adt_off + align(len(adt), 8)
    if len(blob) < need:
        blob.extend(b'\0' * (need - len(blob)))
    blob[adt_off:adt_off + len(adt)] = adt
    pathlib.Path(a.payload_out).write_bytes(blob)
    print(f'{a.payload_out}: {len(blob)} bytes, {len(table)} segments, entry {entry:#x}, end {kend:#x}')


if __name__ == '__main__':
    main()
