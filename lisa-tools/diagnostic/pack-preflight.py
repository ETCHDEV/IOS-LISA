#!/usr/bin/env python3
"""Make a temporary v3 image carrying only the no-jump preflight payload."""
import argparse
import pathlib
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('backup')
p.add_argument('image')
p.add_argument('framed_payload')
p.add_argument('output')
a = p.parse_args()
header = bytearray(pathlib.Path(a.backup).read_bytes()[:4096])
image = pathlib.Path(a.image).read_bytes()
payload = pathlib.Path(a.framed_payload).read_bytes()
if len(header) != 4096 or header[:8] != b'ANDROID!' or struct.unpack_from('<I', header, 40)[0] != 3:
    p.error('expected v3 boot header')
if len(image) < 64 or image[56:60] != b'ARM\x64' or struct.unpack_from('<Q', image, 16)[0] != len(image):
    p.error('invalid ARM64 Image')
if len(payload) < 16 or payload[-16:-8] != b'LISAEND1':
    p.error('invalid framed payload')
struct.pack_into('<II', header, 8, len(image), len(payload))
def padded(data):
    return data + bytes((-len(data)) % 4096)
with pathlib.Path(a.output).open('xb') as out:
    out.write(header)
    out.write(padded(image))
    out.write(padded(payload))
print('preflight image: kernel', len(image), 'generic ramdisk', len(payload))
