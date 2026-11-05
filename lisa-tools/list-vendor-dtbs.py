#!/usr/bin/env python3
"""Enumerate concatenated FDTs from an Android vendor_boot DTB section."""
import argparse
import importlib.util
import pathlib
import struct
import sys

spec = importlib.util.spec_from_file_location("dtb_inspect", pathlib.Path(__file__).with_name("inspect-dtb.py"))
dtb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dtb)


def pairs(value):
    if len(value) % 8:
        raise ValueError("ID property is not a list of 32-bit pairs")
    return list(struct.iter_unpack(">II", value))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("bundle")
    ap.add_argument("--live", help="live FDT for exact qcom,msm-id comparison")
    args = ap.parse_args()
    live_ids = None
    if args.live:
        root = dtb.parse(pathlib.Path(args.live).read_bytes())["/"]
        live_ids = pairs(root.get("qcom,msm-id", b""))
        if not live_ids:
            raise ValueError("live FDT has no qcom,msm-id")
    data = pathlib.Path(args.bundle).read_bytes()
    offset = count = 0
    while offset < len(data):
        if len(data) - offset < 40 or data[offset:offset+4] != b"\xd0\x0d\xfe\xed":
            raise ValueError("invalid FDT at offset %#x" % offset)
        size = struct.unpack_from(">I", data, offset+4)[0]
        if size < 40 or size > len(data) - offset:
            raise ValueError("invalid FDT size at offset %#x" % offset)
        root = dtb.parse(data[offset:offset+size])["/"]
        ids = pairs(root.get("qcom,msm-id", b""))
        matches = bool(live_ids and any(i in ids for i in live_ids))
        model = root.get("model", b"unknown").rstrip(b"\0").decode("utf8", "replace")
        print("%02d offset=%#x size=%d msm=%s%s model=%s" % (count, offset, size, ",".join("%x:%x" % p for p in ids) or "none", " MATCH" if matches else "", model))
        count += 1
        offset += size
    if not count:
        raise ValueError("empty bundle")
    print("Matches are candidates only; board IDs, overlays, firmware selection and bootloader behavior also matter.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, struct.error) as e:
        print("error:", e, file=sys.stderr)
        sys.exit(2)
