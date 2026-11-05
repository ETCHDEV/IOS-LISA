#!/usr/bin/env python3
"""Exercise the actual XNU partition parser against Lisa's saved primary GPT."""
import ctypes
import pathlib
import struct
import subprocess
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'lisa-tools/darwin-xnu-build/xnu/bsd/dev/sweet_ufs.c'
BACKUP = ROOT / 'ios-sweet/backup/lisa-sda-primary-1m-raw.bin'
source = SOURCE.read_text()
function = source[source.index('static int\nufs_find_partition('):source.index('\nstatic int\nufs_take_over_host(')]
preamble = r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <zlib.h>
#define UFS_BLOCK 4096u
#define BOUNCE_SIZE (1u << 20)
#define DMA_BOUNCE 0
static struct { uintptr_t dma; uint8_t lun; uint64_t first_block, nblocks, device_blocks; } su;
static uint8_t bounce[BOUNCE_SIZE], disk[BOUNCE_SIZE];
static int ufs_read_blocks(uint64_t lba, uint32_t count) {
    if (lba > sizeof(disk)/UFS_BLOCK || count > sizeof(disk)/UFS_BLOCK-lba) return EINVAL;
    memcpy(bounce, disk+lba*UFS_BLOCK, count*UFS_BLOCK); return 0;
}
'''
wrapper = r'''
int test(const uint8_t *image, const char *name, uint64_t *first, uint64_t *count) {
    memcpy(disk, image, sizeof(disk)); su.dma=(uintptr_t)bounce;
    su.first_block=su.nblocks=0; su.device_blocks=30617600; int ret=ufs_find_partition(name);
    *first=su.first_block; *count=su.nblocks; return ret;
}
'''
with tempfile.TemporaryDirectory() as temp:
    cfile = pathlib.Path(temp) / 'test.c'
    libfile = pathlib.Path(temp) / 'test.dylib'
    cfile.write_text(preamble + function + wrapper)
    subprocess.run(['clang', '-dynamiclib', '-Werror', str(cfile), '-lz', '-o', str(libfile)], check=True)
    lib = ctypes.CDLL(str(libfile))
    lib.test.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_uint64)]
    original = BACKUP.read_bytes()
    assert len(original) == 1 << 20
    def run(image):
        first, count = ctypes.c_uint64(), ctypes.c_uint64()
        result = lib.test(ctypes.create_string_buffer(bytes(image)), b'userdata', ctypes.byref(first), ctypes.byref(count))
        return result, first.value, count.value
    assert run(original) == (0, 2967552, 30617594 - 2967552 + 1)
    damaged = bytearray(original); damaged[4096 + 40] ^= 1
    assert run(damaged)[0] != 0
    damaged = bytearray(original); damaged[8192 + 100] ^= 1
    assert run(damaged)[0] != 0
    # Give another entry the same valid name, then recompute both checksums.
    damaged = bytearray(original)
    entry_count = struct.unpack_from('<I', damaged, 4096 + 80)[0]
    entry_start = struct.unpack_from('<Q', damaged, 4096 + 72)[0] * 4096
    userdata = entry_start + 32 * 128
    damaged[entry_start:entry_start + 128] = damaged[userdata:userdata + 128]
    struct.pack_into('<I', damaged, 4096 + 88, zlib.crc32(damaged[entry_start:entry_start + entry_count * 128]))
    struct.pack_into('<I', damaged, 4096 + 16, 0)
    struct.pack_into('<I', damaged, 4096 + 16, zlib.crc32(damaged[4096:4096 + 92]))
    assert run(damaged)[0] != 0
    damaged = bytearray(original)
    struct.pack_into('<I', damaged, 4096 + 80, 0x20000000)
    struct.pack_into('<I', damaged, 4096 + 16, 0)
    struct.pack_into('<I', damaged, 4096 + 16, zlib.crc32(damaged[4096:4096 + 92]))
    assert run(damaged)[0] != 0
    damaged = bytearray(original)
    struct.pack_into('<Q', damaged, 4096 + 32, 30617598)
    struct.pack_into('<I', damaged, 4096 + 16, 0)
    struct.pack_into('<I', damaged, 4096 + 16, zlib.crc32(damaged[4096:4096 + 92]))
    assert run(damaged)[0] != 0
    print('PASS: Lisa userdata boundaries and rejection of corrupt, duplicate, and overflowing GPT data')
