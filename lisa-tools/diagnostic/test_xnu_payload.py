#!/usr/bin/env python3
"""Run the copy stage on the real payload in allocated host memory; never jump."""
import ctypes
import pathlib
import struct
import subprocess
import sys
import tempfile

here = pathlib.Path(__file__).resolve().parent
payload = bytearray(pathlib.Path(sys.argv[1]).read_bytes())
count = struct.unpack_from('<I', payload, 12)[0]
virt, entry, adtoff, adtsize, kend, _, mem = struct.unpack_from('<7Q', payload, 0x10)
assert mem == 0x20000000 and count > 0
ram = ctypes.create_string_buffer((kend - virt) + 0x10000 + 0x4000)
base = (ctypes.addressof(ram) + 0x3fff) & ~0x3fff
struct.pack_into('<Q', payload, 0x38, base)
adt = base + kend - virt
ba = (adt + adtsize + 0x3fff) & ~0x3fff
top = ba + 0x4000
assert top <= ctypes.addressof(ram) + len(ram)
class Layout(ctypes.Structure):
    _fields_ = [(x, ctypes.c_uint64) for x in ('entry', 'adt', 'ba', 'top')]
layout = Layout(base + entry - virt, adt, ba, top)
with tempfile.TemporaryDirectory() as tmp:
    stub = pathlib.Path(tmp) / 'stub.c'
    stub.write_text('#include <stdint.h>\nuint64_t got_entry, got_args;\nvoid lisa_test_enter(uint64_t e, uint64_t a) { got_entry=e; got_args=a; }\n')
    libfile = pathlib.Path(tmp) / 'loader.dylib'
    subprocess.run(['clang', '-DLISA_LOADER_HOST_TEST', '-dynamiclib', '-Wall', '-Wextra', '-Werror',
                    str(here / 'xnu_loader.c'), str(stub), '-o', str(libfile)], check=True)
    lib = ctypes.CDLL(str(libfile))
    lib.lisa_xnu_load_and_enter.argtypes = [ctypes.c_void_p, ctypes.POINTER(Layout)]
    src = (ctypes.c_ubyte * len(payload)).from_buffer(payload)
    lib.lisa_xnu_load_and_enter(src, ctypes.byref(layout))
    for i in range(count):
        va, vs, off, fs = struct.unpack_from('<4Q', payload, 0x2a8 + i * 32)
        dest = base + va - virt
        assert ctypes.string_at(dest, fs) == payload[off:off + fs]
        assert ctypes.string_at(dest + ((fs + 7) & ~7), vs - ((fs + 7) & ~7)) == bytes(vs - ((fs + 7) & ~7))
    assert ctypes.string_at(adt, adtsize) == payload[adtoff:adtoff + adtsize]
    assert ctypes.c_uint64.in_dll(lib, 'got_entry').value == layout.entry
    assert ctypes.c_uint64.in_dll(lib, 'got_args').value == ba
print('actual payload segments, zero-fill, ADT and entry handoff checked in host memory')
