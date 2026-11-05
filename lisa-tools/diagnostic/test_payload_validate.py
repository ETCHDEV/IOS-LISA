#!/usr/bin/env python3
"""Host tests of the C validator using an existing mkpayload.py output."""
import ctypes
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent


class Range(ctypes.Structure):
    _fields_ = [('base', ctypes.c_uint64), ('size', ctypes.c_uint64)]


class Layout(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint64) for n in ('entry', 'adt', 'bootargs', 'top')]


def main(path):
    original = pathlib.Path(path).read_bytes()
    phys, size = struct.unpack_from('<2Q', original, 0x38)
    # Synthetic addresses: do not represent or validate a device memory map.
    src = 0x90000000
    with tempfile.TemporaryDirectory() as tmp:
        lib = pathlib.Path(tmp) / 'validator.dylib'
        subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-dynamiclib',
                        str(HERE / 'payload_validate.c'), str(HERE / 'payload_tail.c'),
                        str(HERE / 'payload_preflight.c'), '-o', str(lib)], check=True)
        library = ctypes.CDLL(str(lib))
        fn = library.lisa_validate_payload
        fn.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint64, Range, Range,
                       ctypes.POINTER(Range), ctypes.c_size_t, ctypes.POINTER(Layout)]
        fn.restype = ctypes.c_int

        def check(data, expected, rd=None, ram=None, protected=()):
            buf = ctypes.create_string_buffer(data)
            refs = (Range * len(protected))(*protected)
            result = Layout()
            got = fn(buf, len(data), src, rd or Range(src, len(data)),
                     ram or Range(phys, size), refs, len(refs), ctypes.byref(result))
            assert (got == 0) == expected, f'expected {expected}, got {got}'
            return result

        layout = check(original, True)
        assert phys <= layout.entry < layout.adt < layout.bootargs < layout.top <= phys + size
        for off, fmt, value in [(8, '<I', 2), (12, '<I', 65),
                                (0x20, '<Q', len(original) + 8),
                                (0x18, '<Q', 0), (0x2a8 + 24, '<Q', 2**63),
                                (0x30, '<Q', 2**64 - 1)]:
            data = bytearray(original)
            struct.pack_into(fmt, data, off, value)
            check(bytes(data), False)
        first_va, first_size, first_off, first_file_size = struct.unpack_from('<4Q', original, 0x2a8)
        for off, value in [(0x2a8 + 32, first_va), (0x2a8 + 32 + 16, first_off),
                           (0x20, first_off), (0x18, first_va + 1),
                           (0x2a8 + 24, first_size + 1)]:
            data = bytearray(original)
            struct.pack_into('<Q', data, off, value)
            check(bytes(data), False)
        check(original[:0x2a8], False)
        check(original, False, rd=Range(src, len(original) - 1))
        check(original, False, rd=Range(src, 2**64 - 1))
        check(original, False, ram=Range(phys + 8, size))
        check(original, False, protected=[Range(phys + 4096, 4096)])
        check(original, False, protected=[Range(2**64 - 8, 16)])
        check(original, True, protected=[Range(0xe1000000, 0x2300000)])
        # Model a vendor ramdisk followed by an aligned framed generic ramdisk.
        # Python's buffer and src are both 8-byte aligned in this host test.
        vendor_size = 20213477
        prefix = bytes(vendor_size + (-vendor_size % 8))
        framed = prefix + original + b'LISAEND1' + struct.pack('<Q', len(original))
        initrd = ctypes.create_string_buffer(framed)
        find = library.lisa_find_payload
        find.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64)]
        find.restype = ctypes.c_void_p
        found_size = ctypes.c_uint64()
        found = find(initrd, len(framed), ctypes.byref(found_size))
        assert found == ctypes.addressof(initrd) + len(prefix) and found_size.value == len(original)
        found_layout = Layout()
        assert fn(found, found_size.value, src + len(prefix), Range(src, len(framed)),
                  Range(phys, size), None, 0, ctypes.byref(found_layout)) == 0
        preflight = library.lisa_payload_preflight
        preflight.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_size_t, Range,
                              ctypes.POINTER(Range), ctypes.c_size_t, ctypes.POINTER(Layout)]
        preflight.restype = ctypes.c_int
        assert preflight(initrd, src, len(framed), Range(phys, size), None, 0, ctypes.byref(found_layout)) == 0
        assert preflight(initrd, 2**64 - 8, len(framed), Range(phys, size), None, 0, ctypes.byref(found_layout)) != 0
        initrd[len(framed) - 16] = b'X'
        assert not find(initrd, len(framed), ctypes.byref(found_size))
        assert preflight(initrd, src, len(framed), Range(phys, size), None, 0, ctypes.byref(found_layout)) != 0
    print('C payload validation: valid payload accepted; malformed ranges, headers and overlaps rejected')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('usage: test_payload_validate.py PAYLOAD')
    main(sys.argv[1])
