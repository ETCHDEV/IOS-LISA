#!/usr/bin/env python3
"""Verify sparse structure and hash its fully expanded contents."""
import hashlib
import struct
import sys

digest = hashlib.sha256()
with open(sys.argv[1], 'rb') as f:
    magic, major, minor, fh, ch, block, total, chunks, checksum = struct.unpack('<IHHHHIIII', f.read(28))
    assert (magic, major, minor, fh, ch, block, checksum) == (0xed26ff3a, 1, 0, 28, 12, 4096, 0)
    blocks = 0
    for _ in range(chunks):
        kind, reserved, n, length = struct.unpack('<HHII', f.read(ch))
        assert not reserved and n and blocks + n <= total
        size = n * block
        if kind == 0xcac1:
            assert length == ch + size
            while size:
                data = f.read(min(size, 1024 * 1024))
                assert data
                digest.update(data)
                size -= len(data)
        elif kind == 0xcac2:
            assert length == ch + 4
            word = f.read(4)
            assert len(word) == 4
            data = word * (1024 * 1024 // 4)
            while size:
                amount = min(size, len(data))
                digest.update(data[:amount])
                size -= amount
        else:
            raise ValueError('unexpected sparse chunk; no skipped blocks are allowed')
        blocks += n
    assert blocks == total and not f.read(1)
print('Expanded SHA-256:', digest.hexdigest(), 'blocks:', blocks)
