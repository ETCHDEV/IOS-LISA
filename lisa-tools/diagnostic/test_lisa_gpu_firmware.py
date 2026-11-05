#!/usr/bin/env python3
"""ABI test for the payload GPU firmware container used before XNU starts."""
import pathlib, struct, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
PACK = ROOT / 'lisa-tools/diagnostic/mk_lisa_gpu_firmware.py'
HEADER = struct.Struct('<8sII')
ENTRY = struct.Struct('<I64sQ4x')

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    a, b, out = tmp/'a.bin', tmp/'b.bin', tmp/'fw.bin'
    a.write_bytes(b'a' * 13); b.write_bytes(b'b' * 0x4001)
    subprocess.run(['python3', str(PACK), str(out), str(a), str(b)], check=True)
    blob = out.read_bytes()
    magic, version, count = HEADER.unpack_from(blob)
    assert (magic, version, count) == (b'LISAGPUF', 1, 2)
    assert ENTRY.size == 80
    first = ENTRY.unpack_from(blob, HEADER.size)
    second = ENTRY.unpack_from(blob, HEADER.size + ENTRY.size)
    assert first[0] == len('a.bin') and first[1][:first[0]] == b'a.bin' and first[2] == 13
    assert second[0] == len('b.bin') and second[1][:second[0]] == b'b.bin' and second[2] == 0x4001
    data = (HEADER.size + 2 * ENTRY.size + 0x3fff) & ~0x3fff
    assert blob[data:data + 13] == b'a' * 13
    data = (data + 13 + 0x3fff) & ~0x3fff
    assert blob[data:data + 0x4001] == b'b' * 0x4001
print('Lisa GPU firmware ABI test passed')
