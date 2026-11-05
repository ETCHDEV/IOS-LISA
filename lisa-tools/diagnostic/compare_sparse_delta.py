#!/usr/bin/env python3
"""Read-only check that a sparse image delta matches the device baseline.

Only RAW chunks are compared. DONT_CARE chunks are deliberately never written.
"""

import argparse
import hashlib
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True)
    parser.add_argument("--block-device", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--delta", required=True)
    args = parser.parse_args()

    with open(args.baseline, "rb") as baseline, open(args.delta, "rb") as delta:
        header = delta.read(28)
        magic, major, minor, fh, ch, bs, total, chunks, checksum = struct.unpack(
            "<IHHHHIIII", header
        )
        if magic != 0xED26FF3A or major != 1 or fh != 28 or ch != 12 or bs != 4096:
            raise SystemExit("Unsupported sparse image layout")
        if total * bs > baseline.seek(0, 2):
            raise SystemExit("Baseline is smaller than the sparse image")
        offset = 0
        matches = mismatches = 0
        for _ in range(chunks):
            kind, reserved, count, size = struct.unpack("<HHII", delta.read(ch))
            if kind == 0xCAC3:
                if size != ch:
                    raise SystemExit("Malformed DONT_CARE chunk")
            elif kind == 0xCAC1:
                if size != ch + count * bs:
                    raise SystemExit("Malformed RAW chunk")
                desired = delta.read(count * bs)
                baseline.seek(offset * bs)
                original = baseline.read(count * bs)
                if len(original) != len(desired):
                    raise SystemExit("Truncated baseline")
                proc = subprocess.run(
                    ["adb", "-s", args.serial, "exec-out", "dd",
                     f"if={args.block_device}", "bs=4096", f"skip={offset}",
                     f"count={count}", "status=none"],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
                )
                current = proc.stdout
                if len(current) != count * bs:
                    raise SystemExit(f"Short device read at block {offset}: {len(current)}")
                same = current == original
                matches += int(same)
                mismatches += int(not same)
                print(f"block {offset:8d} count {count:3d} baseline={'MATCH' if same else 'DIFF'} "
                      f"device_sha256={hashlib.sha256(current).hexdigest()[:16]} "
                      f"new_sha256={hashlib.sha256(desired).hexdigest()[:16]}")
            else:
                raise SystemExit(f"Unsupported chunk kind {kind:#x}")
            offset += count
        if offset != total:
            raise SystemExit("Sparse image has wrong total block count")
        print(f"RAW chunks matching baseline: {matches}; differing: {mismatches}")


if __name__ == "__main__":
    main()
