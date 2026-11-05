#!/usr/bin/env python3
"""Create an experimental temporary boot image from a v3 boot backup and Image."""
import argparse
import pathlib
import struct

PAGE = 4096

def aligned(n):
    return (n + PAGE - 1) & -PAGE

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('boot_backup')
    p.add_argument('kernel_image')
    p.add_argument('output')
    args = p.parse_args()
    if pathlib.Path(args.output).resolve() in (pathlib.Path(args.boot_backup).resolve(), pathlib.Path(args.kernel_image).resolve()):
        p.error('output must differ from inputs')
    with open(args.boot_backup, 'rb') as f:
        header = bytearray(f.read(PAGE))
        if len(header) != PAGE or header[:8] != b'ANDROID!':
            p.error('not an Android boot image')
        old_kernel, ramdisk = struct.unpack_from('<II', header, 8)
        header_size, version = struct.unpack_from('<I', header, 20)[0], struct.unpack_from('<I', header, 40)[0]
        if version != 3 or header_size != 1580 or old_kernel == 0 or ramdisk == 0:
            p.error('expected v3 image with kernel and ramdisk')
        f.seek(PAGE + aligned(old_kernel))
        rd = f.read(ramdisk)
        if len(rd) != ramdisk:
            p.error('truncated ramdisk')
    kernel = pathlib.Path(args.kernel_image).read_bytes()
    if len(kernel) < 64 or kernel[56:60] != b'ARM\x64' or struct.unpack_from('<Q', kernel, 16)[0] != len(kernel):
        p.error('invalid ARM64 Image header or image size')
    struct.pack_into('<I', header, 8, len(kernel))
    with open(args.output, 'xb') as out:
        out.write(header)
        out.write(kernel)
        out.write(bytes(aligned(len(kernel)) - len(kernel)))
        out.write(rd)
        out.write(bytes(aligned(len(rd)) - len(rd)))
    print('v3 image prepared: kernel %d bytes, copied ramdisk %d bytes' % (len(kernel), len(rd)))

if __name__ == '__main__':
    main()
