#!/usr/bin/env python3
"""Link scoped compatibility libraries into local stage executables.

App launches through RunningBoard do not inherit a launchd job's DYLD environment.
Use a normal LC_LOAD_DYLIB, with existing header padding only. Preserve the exact
XML entitlements and signature identifier when re-signing the changed executable.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import tempfile

def commands(data):
    assert struct.unpack_from('<I', data)[0] == 0xfeedfacf, 'Expected thin arm64 Mach-O'
    p = 32
    for _ in range(struct.unpack_from('<I', data, 16)[0]):
        cmd, size = struct.unpack_from('<II', data, p)
        assert size >= 8 and p + size <= len(data)
        yield cmd, p, size
        p += size

def signature(data):
    for cmd, p, _ in commands(data):
        if cmd != 0x1d:
            continue
        off, length = struct.unpack_from('<II', data, p + 8)
        sig = data[off:off+length]
        magic, total, count = struct.unpack_from('>III', sig)
        assert magic == 0xfade0cc0 and total <= len(sig)
        xml = ident = None
        for i in range(count):
            typ, at = struct.unpack_from('>II', sig, 12 + i*8)
            magic, size = struct.unpack_from('>II', sig, at)
            if magic == 0xfade7171:
                xml = sig[at+8:at+size]
            if typ == 0 and magic == 0xfade0c02:
                offset = struct.unpack_from('>I', sig, at+20)[0]
                ident = sig[at+offset:].split(b'\0')[0].decode()
        assert xml and ident, 'Refusing to lose executable entitlements/identity'
        plistlib.loads(xml)
        return xml, ident
    raise ValueError('No embedded signature')

def insert(data, library):
    earliest = len(data)
    for cmd, p, size in commands(data):
        if cmd == 0xc:
            at = struct.unpack_from('<I', data, p+8)[0]
            if data[p+at:p+size].split(b'\0')[0].decode() == library:
                return data
        if cmd == 0x19:
            nsects = struct.unpack_from('<I', data, p+64)[0]
            for i in range(nsects):
                section = p+72+80*i
                offset = struct.unpack_from('<I', data, section+48)[0]
                if offset:
                    earliest = min(earliest, offset)
    ncmds, size = struct.unpack_from('<II', data, 16)
    at = 32+size
    name = library.encode()+b'\0'
    length = (24+len(name)+7) & ~7
    assert at+length <= earliest and not any(data[at:at+length]), 'No spare header padding'
    result = bytearray(data)
    entry = struct.pack('<IIIIII', 0xc, length, 24, 0, 0x10000, 0x10000)+name
    result[at:at+length] = entry+bytes(length-len(entry))
    struct.pack_into('<II', result, 16, ncmds+1, size+length)
    return bytes(result)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('root', type=Path)
    ap.add_argument('backup', type=Path)
    args = ap.parse_args()
    assert (args.root/'System/Library/CoreServices/SystemVersion.plist').is_file()
    targets = {
        'Applications/PosterBoard.app/PosterBoard': '/usr/local/lib/lisa/liblisaposter.dylib',
        'Applications/Preferences.app/Preferences': '/usr/local/lib/lisa/liblisapreferences.dylib',
        'System/Library/Frameworks/AssetsLibrary.framework/Support/assetsd': '/usr/local/lib/lisa/liblisaexceptions.dylib',
        # crash stacks (signals, not only ObjC exceptions) of iOS's Wi-Fi daemon; optional
        'usr/sbin/wifid': '/usr/local/lib/lisa/liblisaexceptions.dylib',
    }
    optional = {'usr/sbin/wifid'}
    report = []
    for rel, lib in targets.items():
        assert (args.root/lib.lstrip('/')).is_file()
        path = args.root/rel
        if rel in optional and not path.is_file():
            report.append({'file': rel, 'skipped': 'not present'})
            continue
        before = path.read_bytes()
        try:
            after = insert(before, lib)
        except AssertionError as e:
            if rel not in optional:
                raise
            report.append({'file': rel, 'skipped': str(e)})
            continue
        if after == before:
            report.append({'file': rel, 'already_linked': True})
            continue
        xml, ident = signature(before)
        backup = args.backup/rel
        backup.parent.mkdir(parents=True, exist_ok=True)
        if not backup.exists():
            shutil.copy2(path, backup)
        path.write_bytes(after)
        with tempfile.TemporaryDirectory(prefix='lisa-entitlements-') as temp:
            ent = Path(temp)/'entitlements.plist'
            ent.write_bytes(xml)
            subprocess.run(['codesign', '-f', '-s', '-', '--identifier', ident,
                '--entitlements', str(ent), '--generate-entitlement-der', str(path)], check=True)
        subprocess.run(['codesign', '--verify', '--strict', str(path)], check=True)
        saved, saved_ident = signature(path.read_bytes())
        assert plistlib.loads(saved) == plistlib.loads(xml) and saved_ident == ident
        report.append({'file': rel, 'library': lib, 'entitlements_preserved': True,
            'before': hashlib.sha256(before).hexdigest(), 'after': hashlib.sha256(path.read_bytes()).hexdigest()})
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
