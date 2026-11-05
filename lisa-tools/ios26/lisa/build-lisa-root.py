#!/usr/bin/env python3
"""Wrap unchanged raw APFS containers and sign their read-only UFS windows.

Creates local images and public-key material only. Never operates on a phone.
Private keys remain in the device checkout's ignored .cache directory.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import uuid
import zlib

MIB = 1 << 20
CHUNK = 8 * MIB
DEVICE = Path(__file__).resolve().parent

def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(CHUNK), b''):
            h.update(data)
    return h.hexdigest()

def align(value, boundary=MIB):
    return (value + boundary - 1) // boundary * boundary

def gpt_header(mine, other, first, last, guid, entries_lba, entries):
    h = bytearray(struct.pack('<8sIIIIQQQQ16sQIII', b'EFI PART', 0x10000, 92, 0, 0,
                             mine, other, first, last, guid.bytes_le, entries_lba,
                             128, 128, zlib.crc32(entries)))
    struct.pack_into('<I', h, 16, zlib.crc32(h))
    return bytes(h).ljust(512, b'\0')

class Window:
    """A file view of one window of the image, for hashing and signing it in place."""
    def __init__(self, path, offset, size):
        self.f, self.offset, self.size, self.at = path.open('rb'), offset, size, 0
    def seek(self, at, whence=0):
        self.at = at if whence == 0 else self.at + at if whence == 1 else self.size + at
        return self.at
    def tell(self):
        return self.at
    def read(self, n=-1):
        n = self.size - self.at if n < 0 else min(n, self.size - self.at)
        self.f.seek(self.offset + self.at)
        data = self.f.read(n)
        self.at += len(data)
        return data
    def close(self):
        self.f.close()

def window_bytes(source):
    return align(MIB + source.stat().st_size + MIB)

def wrap_apfs(source, image, offset, name, volume_uuid):
    """Lisa: straight into the image at offset, leaving holes for empty blocks: the Mac has no room for a second copy."""
    with source.open('rb') as f:
        first = f.read(4096)
    if len(first) != 4096 or first[32:36] != b'NXSB':
        raise ValueError(f'{source}: require a decrypted raw APFS container')
    block_size, blocks = struct.unpack_from('<IQ', first, 36)
    size = source.stat().st_size
    if block_size != 4096 or blocks * block_size != size:
        raise ValueError(f'{source}: APFS geometry does not cover the exact input file')
    source_digest = sha256(source)
    disk_uuid = uuid.uuid5(uuid.NAMESPACE_OID, 'lisa-disk:' + source_digest)
    partition_uuid = uuid.uuid5(uuid.NAMESPACE_OID, 'lisa-apfs:' + source_digest)
    if partition_uuid == volume_uuid:
        raise ValueError('Partition and APFS volume UUIDs must be distinct')
    total = window_bytes(source)
    sectors = total // 512
    start, end = MIB // 512, (MIB + size) // 512 - 1
    entries = bytearray(128 * 128)
    struct.pack_into('<16s16sQQQ72s', entries, 0,
                     uuid.UUID('7C3457EF-0000-11AA-AA11-00306543ECAC').bytes_le,
                     partition_uuid.bytes_le, start, end, 1 << 60,
                     (name + ' APFS').encode('utf-16-le'))
    mbr = bytearray(512)
    struct.pack_into('<B3sB3sII', mbr, 446, 0, b'\0\2\0', 0xee, b'\xff\xff\xff',
                     1, min(sectors - 1, 0xffffffff))
    mbr[510:512] = b'\x55\xaa'
    with image.open('r+b') as out:
        out.seek(offset)
        out.write(mbr)
        out.write(gpt_header(1, sectors - 1, 34, sectors - 34, disk_uuid, 2, entries))
        out.write(entries)
        out.seek(offset + MIB)
        copied = hashlib.sha256()
        zero = bytes(CHUNK)
        with source.open('rb') as raw:
            for data in iter(lambda: raw.read(CHUNK), b''):
                copied.update(data)
                if data == zero[:len(data)]:
                    out.seek(len(data), os.SEEK_CUR)
                else:
                    out.write(data)
        if copied.hexdigest() != source_digest:
            raise ValueError('Input changed while copying its unchanged APFS bytes')
        out.seek(offset + (sectors - 33) * 512)
        out.write(entries)
        out.write(gpt_header(sectors - 1, 1, 34, sectors - 34,
                             disk_uuid, sectors - 33, entries))
    view = Window(image, offset, total)
    h = hashlib.sha256()
    for data in iter(lambda: view.read(CHUNK), b''):
        h.update(data)
    view.close()
    return dict(name=name, source=str(source), source_bytes=size,
                source_sha256=source_digest, source_container_unchanged=True,
                container_offset=MIB, size=total, partition_uuid=str(partition_uuid).upper(),
                volume_uuid=str(volume_uuid).upper(), offset=offset, sha256=h.hexdigest())

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--os', type=Path, required=True)
    p.add_argument('--systemos', type=Path, required=True)
    p.add_argument('--os-volume-uuid', type=uuid.UUID, required=True)
    p.add_argument('--systemos-volume-uuid', type=uuid.UUID, required=True)
    p.add_argument('--unixnu', type=Path, default=DEVICE.parents[1] / 'research-UniXNU-ios26')
    p.add_argument('--userdata-bytes', type=lambda s: int(s, 0), required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--public-header', type=Path,
                   default=DEVICE / 'lisa-owner-root-key.h')
    a = p.parse_args()
    if a.os_volume_uuid == a.systemos_volume_uuid or a.userdata_bytes <= 0:
        p.error('Require distinct volume identities and a positive partition capacity')
    a.output.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location(
        'verified_manifest', a.unixnu / 'kits/VerifiedRoot/tools/manifest.py')
    signing = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(signing)
    keys = DEVICE / '.cache/owner-root'
    keys.mkdir(parents=True, exist_ok=True, mode=0o700)
    private, public = keys / 'owner.pem', keys / 'owner-public.pem'
    if not private.exists():
        if public.exists():
            raise ValueError('Public key exists without its private key; refuse replacement')
        previous = os.umask(0o077)
        try:
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA',
                            '-pkeyopt', 'rsa_keygen_bits:3072', '-out', private],
                           check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        finally:
            os.umask(previous)
    if not public.exists():
        subprocess.run(['openssl', 'pkey', '-in', private, '-pubout', '-out', public],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    anchor = signing.public_anchor(public)
    header = ('/* Generated public owner anchor; private key stays in .cache/owner-root. */\n'
              'const unsigned char verified_root_key[VR_KEY_BYTES] = {\n' +
              ''.join('  ' + ','.join(f'0x{b:02x}' for b in anchor[i:i+16]) + ',\n'
                      for i in range(0, len(anchor), 16)) + '};\n')
    if a.public_header.exists() and a.public_header.read_text() != header:
        raise ValueError('Refuse replacing an existing different trusted public anchor')
    a.public_header.write_text(header)
    windows = []
    image = a.output / 'userdata-root.img'
    sources = (('OS', a.os, a.os_volume_uuid), ('SystemOS', a.systemos, a.systemos_volume_uuid))
    cursor = MIB
    with image.open('xb') as out:
        out.truncate(MIB + sum(align(window_bytes(p.resolve())) for _, p, _ in sources))
    for name, source, identity in sources:
        item = wrap_apfs(source.resolve(), image, cursor, name, identity)
        view = Window(image, cursor, item['size'])
        policy = signing.make(view, item['size'], private, public, identity, 1,
                              a.output / (name + '-policy'))
        view.close()
        if policy['image_sha256'] != item['sha256']:
            raise ValueError('Policy does not cover the exact wrapped image')
        item['policy_path'] = str(a.output / (name + '-policy/manifest.bin'))
        item['policy_bytes'] = Path(item['policy_path']).stat().st_size
        windows.append(item)
        cursor += align(item['size'])
        print(f'{name}: APFS wrapped in place and owner signature verified', flush=True)
    for w in windows:
        w['policy_offset'] = cursor
        cursor += align(w['policy_bytes'])
    if cursor > a.userdata_bytes:
        raise ValueError('Root image exceeds the verified userdata capacity')
    manifest = bytearray(4096)
    manifest[:8] = b'SHNGROOT'
    struct.pack_into('<IIQ', manifest, 8, 1, len(windows), cursor)
    for i, w in enumerate(windows):
        # The reserved entry bytes describe an owner-policy envelope, stored
        # outside both disk windows. The signature remains the trust decision.
        extra = struct.pack('<QQ24s', w['policy_offset'], w['policy_bytes'], bytes(24))
        struct.pack_into('<32sQQII32s40s', manifest, 24 + 128 * i,
                         w['name'].encode(), w['offset'], w['size'], 512, 1,
                         bytes.fromhex(w['sha256']), extra)
    manifest[4064:] = hashlib.sha256(manifest[:4064]).digest()
    with image.open('r+b') as out:
        out.truncate(cursor)
        out.write(manifest)
        for w in windows:
            out.seek(w['policy_offset'])
            out.write(Path(w['policy_path']).read_bytes())
        out.flush()
        os.fsync(out.fileno())
    report = dict(image=str(image.resolve()), image_bytes=cursor, image_sha256=sha256(image),
                  windows=windows, userdata_bytes=a.userdata_bytes,
                  trusted_key_sha256=hashlib.sha256(anchor).hexdigest(),
                  public_header=str(a.public_header), hardware_tested=False, flashed=False,
                  root_mount_verified=False)
    (a.output / 'userdata-root.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Prepared {image}: {cursor} bytes; nothing flashed', flush=True)

if __name__ == '__main__':
    main()
