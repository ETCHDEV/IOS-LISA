#!/usr/bin/env python3
"""Convert a block-aligned raw image to Android sparse without skipping zeros."""
import argparse
import hashlib
import os
import struct

BLOCK = 4096
CHUNK = 4 * 1024 * 1024

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source')
p.add_argument('destination')
a = p.parse_args()
size = os.stat(a.source).st_size
if not size or size % BLOCK or size // BLOCK > 0xffffffff:
    p.error('source must be a nonempty 4096-byte-aligned image under 16 TiB')
digest = hashlib.sha256()
count = 0
with open(a.source, 'rb') as source, open(a.destination, 'xb') as dest:
    dest.write(bytes(28))
    while True:
        data = source.read(CHUNK)
        if not data:
            break
        if len(data) % BLOCK:
            raise ValueError('short unaligned image read')
        digest.update(data)
        blocks = len(data) // BLOCK
        if data.count(0) == len(data):
            dest.write(struct.pack('<HHII', 0xcac2, 0, blocks, 16))
            dest.write(bytes(4))
        else:
            dest.write(struct.pack('<HHII', 0xcac1, 0, blocks, 12 + len(data)))
            dest.write(data)
        count += 1
    if source.tell() != size:
        raise ValueError('source changed during conversion')
    dest.seek(0)
    dest.write(struct.pack('<IHHHHIIII', 0xed26ff3a, 1, 0, 28, 12,
                           BLOCK, size // BLOCK, count, 0))
print('Expanded SHA-256:', digest.hexdigest(), 'chunks:', count, 'bytes:', size)
