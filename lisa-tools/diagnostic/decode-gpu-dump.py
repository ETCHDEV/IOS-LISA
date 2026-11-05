#!/usr/bin/env python3
"""Summarize bounded LisaGPU packet traces recovered from the userdata log."""
import argparse
import collections
import re
from pathlib import Path

START = re.compile(r"LisaGPU dump: fence (\d+) cmd (\d+) iova (0x[\da-f]+) words (\d+) cpu (\w+)")
DATA = re.compile(r"LisaGPU dump: (\d+)/(\d+) \[(\d+)\] ([\da-f]{8}) ([\da-f]{8}) ([\da-f]{8}) ([\da-f]{8})")
OPCODES = {
    0x12: "WAIT_MEM_WRITES", 0x13: "WAIT_FOR_ME", 0x24: "DRAW_AUTO",
    0x26: "WAIT_FOR_IDLE", 0x28: "DRAW_INDIRECT", 0x29: "DRAW_INDX_INDIRECT",
    0x2a: "DRAW_INDIRECT_MULTI", 0x2c: "BLIT", 0x33: "EXEC_CS",
    0x38: "DRAW_INDX_OFFSET", 0x3e: "REG_TO_MEM", 0x3f: "INDIRECT_BUFFER",
    0x41: "EXEC_CS_INDIRECT", 0x42: "MEM_TO_REG", 0x43: "SET_DRAW_STATE",
    0x46: "EVENT_WRITE", 0x47: "COND_REG_EXEC", 0x65: "SET_MARKER",
    0x66: "SET_SECURE_MODE",
}


def packets(words):
    at = 0
    while at < len(words):
        header = words[at]
        kind = header >> 28
        if kind == 7:
            count = header & 0x7fff
            opcode = (header >> 16) & 0x7f
            label = OPCODES.get(opcode, f"OP_{opcode:02x}")
        elif kind == 4:
            count = header & 0x7f
            label = f"REG_{(header >> 8) & 0x7ffff:05x}"
        else:
            yield at, "INVALID", 0, header
            break
        yield at, label, count, header
        at += count + 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    args = ap.parse_args()
    groups = {}
    for line in args.log.read_text(errors="replace").splitlines():
        found = START.search(line)
        if found:
            fence, cmd, iova, size, mapped = found.groups()
            groups[int(fence), int(cmd)] = {"iova": iova, "size": int(size), "mapped": mapped, "words": {}}
            continue
        found = DATA.search(line)
        if found:
            fence, cmd, off, *data = found.groups()
            entry = groups.get((int(fence), int(cmd)))
            if entry is not None:
                entry["words"].update((int(off) + i, int(word, 16)) for i, word in enumerate(data))
    for (fence, cmd), entry in sorted(groups.items()):
        words = [entry["words"].get(i) for i in range(min(entry["size"], 1024))]
        complete = all(word is not None for word in words)
        print(f"fence {fence} cmd {cmd}: {entry['iova']} {entry['size']} words, CPU {entry['mapped']}, captured {len(entry['words'])}, complete={complete}")
        if not complete:
            continue
        totals = collections.Counter()
        for off, label, count, header in packets(words):
            totals[label] += 1
            print(f"  {off:3d}: {label:20s} {count:3d} words  header={header:08x}")
            payload = words[off + 1:off + 1 + count]
            if label == "SET_DRAW_STATE":
                for group in range(0, len(payload) - 2, 3):
                    state, low, high = payload[group:group + 3]
                    print(f"       state={state:08x} iova={(high << 32) | low:#x} words={state & 0xffff}")
            elif label == "INDIRECT_BUFFER" and len(payload) >= 3:
                print(f"       iova={(payload[1] << 32) | payload[0]:#x} words={payload[2]}")
            elif label.startswith("REG_") or count <= 8:
                print("       " + " ".join(f"{word:08x}" for word in payload))
        print("  packet counts:", ", ".join(f"{name}={count}" for name, count in sorted(totals.items())))


if __name__ == "__main__":
    main()
