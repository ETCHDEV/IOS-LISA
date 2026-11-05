#!/usr/bin/env python3
"""Add one ad-hoc SHA-256 CodeDirectory hash to a single-module v2 trust bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', required=True, type=Path)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists() or args.output.with_suffix('.json').exists():
        raise SystemExit('refuse to overwrite output or report')
    data = args.input.read_bytes()
    if struct.unpack_from('<II', data, 0) != (1, 8) or struct.unpack_from('<I', data, 8)[0] != 2:
        raise SystemExit('input is not one version-2 trust-cache module')
    count = struct.unpack_from('<I', data, 28)[0]
    if len(data) != 32 + count * 24:
        raise SystemExit('input trust-cache size mismatch')
    entries = {data[32 + i * 24:52 + i * 24]: data[52 + i * 24:56 + i * 24] for i in range(count)}
    detail = subprocess.run(['codesign', '-dvvv', str(args.binary)], capture_output=True, text=True).stderr
    marker = 'CandidateCDHash sha256='
    line = next((line for line in detail.splitlines() if line.startswith(marker)), None)
    if line is None:
        raise SystemExit('binary has no SHA-256 CodeDirectory')
    cdhash = bytes.fromhex(line[len(marker):])
    if len(cdhash) != 20 or cdhash in entries:
        raise SystemExit('invalid or already-trusted cdhash')
    entries[cdhash] = bytes((2, 0, 0, 0))
    ordered = sorted(entries)
    module_uuid = uuid.uuid5(uuid.NAMESPACE_URL, 'lisa-owner-trust:' + hashlib.sha256(b''.join(ordered)).hexdigest())
    module = struct.pack('<I16sI', 2, module_uuid.bytes, len(ordered)) + b''.join(key + entries[key] for key in ordered)
    output = struct.pack('<II', 1, 8) + module
    args.output.write_bytes(output)
    report = dict(schema='lisa-owner-trust-add-v1', input=str(args.input), binary=str(args.binary),
                  cdhash=cdhash.hex(), attributes='02000000', entries=len(ordered), module_uuid=str(module_uuid),
                  sha256=hashlib.sha256(output).hexdigest())
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
