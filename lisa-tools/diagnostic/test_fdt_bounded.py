#!/usr/bin/env python3
"""Exercise the bounded chosen parser with valid and truncated FDTs."""
import ctypes
import pathlib
import struct
import subprocess
import tempfile

here = pathlib.Path(__file__).resolve().parent
be = lambda n: struct.pack('>I', n)
align = lambda b: b + bytes((-len(b)) % 4)
names = b'linux,initrd-start\0linux,initrd-end\0'
tree = (be(1) + align(b'\0') + be(1) + align(b'chosen\0') +
        be(3) + be(8) + be(0) + struct.pack('>Q', 0xaeb4c000) +
        be(3) + be(4) + be(19) + be(0xafffe85b) +
        be(2) + be(2) + be(9))
offset = 40
strings = offset + len(tree)
blob = (b''.join(map(be, [0xd00dfeed, strings + len(names), offset, strings,
                           40, 17, 16, 0, len(names), len(tree)])) + tree + names)

with tempfile.TemporaryDirectory() as tmp:
    libpath = pathlib.Path(tmp) / 'fdt.dylib'
    subprocess.run(['clang', '-shared', '-Wall', '-Wextra', '-Werror',
                    str(here / 'fdt_bounded.c'), '-o', str(libpath)], check=True)
    lib = ctypes.CDLL(str(libpath))
    fn = lib.lisa_fdt_chosen_u64
    fn.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p,
                   ctypes.POINTER(ctypes.c_uint64)]
    def check(data, prop, expected=None, accessible=None):
        buf = ctypes.create_string_buffer(data)
        result = ctypes.c_uint64(0)
        ok = fn(buf, len(data) if accessible is None else accessible, prop, ctypes.byref(result))
        assert bool(ok) == (expected is not None), (len(data), prop, ok)
        if expected is not None:
            assert result.value == expected
    check(blob, b'linux,initrd-start', 0xaeb4c000)
    check(blob, b'linux,initrd-end', 0xafffe85b)
    check(blob, b'missing')
    for length in range(len(blob)):
        check(blob[:length], b'linux,initrd-start')
    bad = bytearray(blob)
    bad[12:16] = be(len(blob) + 1)
    check(bytes(bad), b'linux,initrd-start')
    bad = bytearray(blob)
    bad[40:44] = be(3)
    check(bytes(bad), b'linux,initrd-start')
print('bounded FDT parser: valid properties accepted; truncated and malformed inputs rejected')
