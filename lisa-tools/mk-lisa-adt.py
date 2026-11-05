#!/usr/bin/env python3
"""Generate an experimental, storage-free Lisa ADT for offline inspection only.

This does not establish safe RAM, prepare a boot image, or patch XNU's early
framebuffer constants. Do not use the output to boot hardware.
"""
import argparse
import importlib.util
import pathlib
import struct
import sys

root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'ios-sweet' / 'tools'))
import mkadt
import adt

spec = importlib.util.spec_from_file_location('inspect_dtb', root / 'lisa-tools' / 'inspect-dtb.py')
inspect = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspect)


def name(n):
    return dict((k, v) for k, _, v in n.props).get('name', b'').rstrip(b'\0').decode()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--live-fdt', required=True)
    p.add_argument('--ram-base', required=True, type=lambda x: int(x, 0))
    p.add_argument('--ram-size', required=True, type=lambda x: int(x, 0))
    p.add_argument('--output', required=True)
    p.add_argument('--cpus', type=int, default=1,
                   help='number of Lisa CPU nodes to expose (default: 1; experimental)')
    p.add_argument('--product-adt', help='optional extracted iPhone X ADT for iOS product properties')
    p.add_argument('--experimental-ufs', action='store_true',
                   help='include the unverified, opt-in UFS host for offline development')
    args = p.parse_args()
    base, size = args.ram_base, args.ram_size
    if base < 0 or size <= 0 or base + size > 1 << 64 or base % (32 << 20) or size % (32 << 20):
        p.error('RAM range must be positive, 32 MiB aligned, and fit in 64 bits')
    blob = pathlib.Path(args.live_fdt).read_bytes()
    nodes = inspect.parse(blob)
    if args.experimental_ufs:
        ufs_path = '/soc/ufshc@1d84000'
        ufs = nodes.get(ufs_path, {})
        if (b'qcom,ufshc' not in ufs.get('compatible', b'').split(b'\0') or
                ufs.get('status', b'okay').rstrip(b'\0') not in (b'okay', b'ok') or
                not inspect.regs(nodes, ufs_path) or
                inspect.regs(nodes, ufs_path)[0] != (0x01d84000, 0x3000)):
            p.error('live FDT does not identify the expected Lisa UFS host')
    timer = nodes.get('/soc/timer', {})
    interrupts = timer.get('interrupts', b'')
    if len(interrupts) != 48 or b'arm,armv8-timer' not in timer.get('compatible', b'').split(b'\0'):
        p.error('live FDT has an unsupported architected timer node')
    timer_type, timer_ppi, timer_flags = struct.unpack_from('>III', interrupts, 24)
    if timer_type != 1 or timer_ppi >= 16:
        p.error('live FDT virtual timer must be a GIC PPI')
    mkadt.VTIMER_INTID = 16 + timer_ppi
    print('Lisa virtual timer INTID:', mkadt.VTIMER_INTID, 'flags:', hex(timer_flags))
    if not 1 <= args.cpus <= 8:
        p.error('CPU count must be between 1 and 8')
    for core in range(args.cpus):
        path = '/cpus/cpu@%x' % (core << 8)
        props = nodes.get(path)
        if (props is None or props.get('device_type', b'').rstrip(b'\0') != b'cpu' or
                props.get('enable-method', b'').rstrip(b'\0') != b'psci' or
                len(props.get('reg', b'')) != 8 or
                inspect.number(props.get('reg', b'')) != core << 8):
            p.error('live FDT CPU topology or PSCI method mismatch at ' + path)
    memory, reserved = [], []
    # Include the FDT header's memory reservation table, bounded by totalsize.
    pos = struct.unpack_from('>I', blob, 16)[0]
    total = struct.unpack_from('>I', blob, 4)[0]
    if pos < 40 or pos % 8 or pos >= total:
        p.error('invalid FDT reservation table offset')
    while True:
        if pos + 16 > total:
            p.error('unterminated FDT reservation table')
        address, length = struct.unpack_from('>QQ', blob, pos)
        pos += 16
        if not address and not length:
            break
        if length:
            reserved.append((address, length))
    for path, props in nodes.items():
        if path.count('/') == 1 and (props.get('device_type', b'').rstrip(b'\0') == b'memory' or path.startswith('/memory@')):
            memory += inspect.regs(nodes, path)
        if path.startswith('/reserved-memory/') and path.count('/') == 2:
            if props.get('status', b'').rstrip(b'\0') in (b'disabled', b'fail', b'failed'):
                continue
            if 'reg' in props:
                reserved += inspect.regs(nodes, path)
            elif 'size' in props:
                raw = props.get('alloc-ranges')
                if not raw:
                    p.error('dynamic reservation %s has no alloc-ranges' % path)
                parent = nodes['/reserved-memory']
                ac = inspect.cells(parent, '#address-cells', 2)
                sc = inspect.cells(parent, '#size-cells', 1)
                step = 4 * (ac + sc)
                if len(raw) % step:
                    p.error('invalid alloc-ranges in %s' % path)
                reserved += [(inspect.number(raw[i:i+4*ac]), inspect.number(raw[i+4*ac:i+step])) for i in range(0, len(raw), step)]
    if not any(base >= a and base + size <= a + n for a, n in memory):
        p.error('candidate must fit within one FDT memory bank')
    if any(base < a+n and a < base+size for a, n in reserved):
        p.error('candidate overlaps a fixed reserved-memory region')
    mkadt.PHYS_BASE, mkadt.MEM_SIZE = base, size
    mkadt.FB, mkadt.FB_SIZE = 0xe1000000, 1080 * 2400 * 4
    if args.product_adt:
        product_reader = mkadt.d22ap_product
        mkadt.d22ap_product = lambda: product_reader(args.product_adt)
    tree = mkadt.build()
    if not args.experimental_ufs:
        tree.children = [child for child in tree.children if name(child) != 'sweet-ufs']
    else:
        print('WARNING: UFS node is experimental and not hardware validated; probe requires lisa_ufs_probe=1.')
    cpus = next(child for child in tree.children if name(child) == 'cpus')
    cpus.children = cpus.children[:args.cpus]
    output = pathlib.Path(args.output)
    with output.open('xb') as f:
        f.write(adt.serialize(tree))
    print('Wrote offline ADT:', output)
    print('WARNING: firmware protections, runtime changes, and XNU early framebuffer mappings are not validated. NOT BOOTABLE.')

if __name__ == '__main__':
    main()
