#!/usr/bin/env python3
"""Derive XNU-safe Lisa RAM ranges from a decompiled Linux device tree.

The Snapdragon boot firmware owns several `reserved-memory` areas.  XNU's
single `physBase + memSize` boot contract cannot describe holes, so never use
the resulting list as a single contiguous range.  The generated list is an
input to the pending range-aware pmap implementation.
"""
import argparse
import re
from pathlib import Path


def cells(text: str):
    return [int(value, 0) for value in re.findall(r"0x[0-9a-fA-F]+|\d+", text)]


def ranges_from_reg(value: str):
    v = cells(value)
    if len(v) % 4:
        raise ValueError(f"bad 64-bit reg cells: {value}")
    return [((v[i] << 32) | v[i + 1], (v[i + 2] << 32) | v[i + 3])
            for i in range(0, len(v), 4)]


def subtract(ranges, holes):
    out = []
    for base, size in ranges:
        pieces = [(base, base + size)]
        for hbase, hsize in holes:
            hend = hbase + hsize
            next_pieces = []
            for start, end in pieces:
                if hend <= start or hbase >= end:
                    next_pieces.append((start, end))
                else:
                    if start < hbase:
                        next_pieces.append((start, hbase))
                    if hend < end:
                        next_pieces.append((hend, end))
            pieces = next_pieces
        out.extend((start, end - start) for start, end in pieces if end > start)
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dts", type=Path)
    ap.add_argument("output", type=Path)
    args = ap.parse_args()
    source = args.dts.read_text()
    mem = re.search(r"\n\s*memory\s*\{.*?\n\s*reg\s*=\s*<(.*?)>;.*?\n\s*\};", source, re.S)
    if not mem:
        raise SystemExit("memory node not found")
    dram = ranges_from_reg(mem.group(1))
    reserved = re.search(r"\n\s*reserved-memory\s*\{(.*?)(?=\n\s*\};\n\s*firmware\s*\{)", source, re.S)
    if not reserved:
        raise SystemExit("reserved-memory node not found")
    holes = []
    for node in re.finditer(r"\n\s*[\w,-]+(?:@[^\s{]+)?\s*\{(.*?)\n\s*\};", reserved.group(1), re.S):
        body = node.group(1)
        if "no-map;" not in body:
            continue
        reg = re.search(r"\breg\s*=\s*<(.*?)>;", body, re.S)
        if reg:
            holes.extend(ranges_from_reg(reg.group(1)))
    safe = subtract(dram, holes)
    lines = ["/* Generated from Lisa DTS; do not flatten these ranges. */",
             "#ifndef _OSFMK_ARM64_LISA_MEMORY_RANGES_H_",
             "#define _OSFMK_ARM64_LISA_MEMORY_RANGES_H_", "",
             "#include <mach/vm_types.h>", "",
             "struct lisa_memory_range { uint64_t base; uint64_t size; };",
             "extern const struct lisa_memory_range lisa_memory_ranges[];",
             "extern const unsigned int lisa_memory_range_count;",
             "uint64_t lisa_memory_ranges_total(void);",
             "boolean_t lisa_memory_range_contains(uint64_t pa, uint64_t size);",
             "",
             f"#define LISA_MEMORY_RANGE_COUNT {len(safe)}",
             "#ifdef LISA_MEMORY_RANGES_DEFINE",
             "const struct lisa_memory_range lisa_memory_ranges[] = {"]
    lines += [f"\t{{ 0x{base:016x}ULL, 0x{size:016x}ULL }}," for base, size in safe]
    lines += ["};", "const unsigned int lisa_memory_range_count = LISA_MEMORY_RANGE_COUNT;", "#endif", "", "#endif"]
    args.output.write_text("\n".join(lines) + "\n")
    print(f"{len(safe)} usable ranges, {sum(size for _, size in safe) // (1024 * 1024)} MiB")


if __name__ == "__main__":
    main()
