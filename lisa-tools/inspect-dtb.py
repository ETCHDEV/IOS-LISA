#!/usr/bin/env python3
"""Read-only inspection of a flattened device tree; no hardware access."""
import argparse
import struct
import sys


def u32(data, pos):
    return struct.unpack_from(">I", data, pos)[0]


def parse(data):
    if len(data) < 40 or u32(data, 0) != 0xd00dfeed:
        raise ValueError("not a flattened device tree")
    total, off_struct, off_strings = (u32(data, i) for i in (4, 8, 12))
    size_strings, size_struct = (u32(data, i) for i in (32, 36))
    if total > len(data) or off_struct + size_struct > total or off_strings + size_strings > total:
        raise ValueError("truncated or invalid FDT")
    block = data[off_struct:off_struct + size_struct]
    strings = data[off_strings:off_strings + size_strings]
    nodes, stack, pos = {}, [], 0
    while pos + 4 <= len(block):
        token = u32(block, pos)
        pos += 4
        if token == 1:
            end = block.find(b"\0", pos)
            if end < 0:
                raise ValueError("invalid node name")
            name = block[pos:end].decode("ascii")
            stack.append(name)
            path = "/" + "/".join(stack[1:])
            nodes[path] = {}
            pos = (end + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError("unbalanced node")
            stack.pop()
        elif token == 3:
            if not stack or pos + 8 > len(block):
                raise ValueError("invalid property")
            length, nameoff = u32(block, pos), u32(block, pos + 4)
            pos += 8
            end = strings.find(b"\0", nameoff)
            if end < 0 or pos + length > len(block):
                raise ValueError("invalid property bounds")
            name = strings[nameoff:end].decode("ascii")
            nodes["/" + "/".join(stack[1:])][name] = block[pos:pos + length]
            pos = (pos + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack:
                raise ValueError("unclosed nodes")
            return nodes
        else:
            raise ValueError("unknown FDT token %d" % token)
    raise ValueError("missing FDT end token")


def number(data):
    return int.from_bytes(data, "big")


def cells(props, name, default):
    value = props.get(name)
    if value is None:
        return default
    if len(value) != 4 or number(value) not in (1, 2):
        raise ValueError("unsupported %s" % name)
    return number(value)


def regs(nodes, path):
    parent = path.rsplit("/", 1)[0] or "/"
    props = nodes[parent]
    ac = cells(props, "#address-cells", 2)
    sc = cells(props, "#size-cells", 1)
    raw = nodes[path].get("reg", b"")
    step = 4 * (ac + sc)
    if len(raw) % step:
        raise ValueError("invalid reg length for " + path)
    return [(number(raw[i:i + ac * 4]), number(raw[i + ac * 4:i + step]))
            for i in range(0, len(raw), step)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dtb", help="raw flattened device tree, not a boot image or DTB container")
    ap.add_argument("--check-range", nargs=2, metavar=("START", "SIZE"), type=lambda s: int(s, 0),
                    help="check a candidate physical RAM interval against reported banks and reservations")
    args = ap.parse_args()
    with open(args.dtb, "rb") as f:
        nodes = parse(f.read())
    root = nodes.get("/", {})
    print("Model:", root.get("model", b"unknown").rstrip(b"\0").decode("utf-8", "replace"))
    print("Compatible:", root.get("compatible", b"unknown").rstrip(b"\0").replace(b"\0", b", ").decode("utf-8", "replace"))
    memory, reserved = [], []
    for path, props in sorted(nodes.items()):
        if path.count("/") == 1 and (props.get("device_type", b"").rstrip(b"\0") == b"memory" or path.startswith("/memory@")):
            memory.extend((a, n, path) for a, n in regs(nodes, path) if n)
        if path.startswith("/reserved-memory/") and path.count("/") == 2 and "reg" in props:
            reserved.extend((a, n, path) for a, n in regs(nodes, path) if n)
    for label, entries in (("Memory", memory), ("Reserved", reserved)):
        print(label + ":")
        for start, size, path in entries:
            print("  %#x-%#x %s" % (start, start + size, path))
        if not entries:
            print("  none described")
    for path, props in sorted(nodes.items()):
        if b"simple-framebuffer" in props.get("compatible", b"").split(b"\0"):
            print("Framebuffer:", path, "reg", [(hex(a), hex(n)) for a, n in regs(nodes, path)])
            for key in ("width", "height", "stride"):
                if key in props:
                    print(" ", key, number(props[key]))
    if args.check_range:
        start, size = args.check_range
        if start < 0 or size <= 0 or start + size > 1 << 64:
            raise ValueError("range must be positive and fit in 64 bits")
        end = start + size
        contained = any(start >= a and end <= a + n for a, n, _ in memory)
        overlaps = [p for a, n, p in reserved if start < a + n and a < end]
        print("Candidate: %s; overlaps: %s" % ("inside a reported memory bank" if contained else "NOT inside a single reported memory bank", ", ".join(overlaps) or "none reported"))
        if not contained or overlaps:
            return 1
    print("Caution: DT data alone cannot establish firmware protections or safe access.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, struct.error, UnicodeError) as exc:
        print("error:", exc, file=sys.stderr)
        sys.exit(2)
