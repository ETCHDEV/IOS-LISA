#!/usr/bin/env python3
"""Create a temporary Lisa high-bank payload with matching ADT and XNU limit."""
import argparse, struct, sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent/'lisa-build'/'tools'))
import adt
p=argparse.ArgumentParser()
p.add_argument('source', type=Path); p.add_argument('output', type=Path); p.add_argument('mib', type=int)
a=p.parse_args()
size=a.mib*1024*1024
if not 0x20000000 <= size <= 0x100000000 or size % 0x4000: raise SystemExit('size must be 512..4096 MiB and 16 KiB aligned')
b=bytearray(a.source.read_bytes()); off, old=struct.unpack_from('<2Q',b,0x20)
root,_=adt.parse(b[off:off+old]); chosen=next(x for x in root.children if x.name=='chosen'); defaults=next(x for x in root.children if x.name=='defaults')
chosen.props=[(n,f,struct.pack('<Q',size) if n=='dram-size' else v) for n,f,v in chosen.props]
defaults.props=[('hw.memsize' if n=='AAPL,phandle' else n,f,struct.pack('<I',size) if n=='AAPL,phandle' else v) for n,f,v in defaults.props]
new=adt.serialize(root); b=b[:off]+new; b+=bytes((-len(b))&15)
struct.pack_into('<Q',b,0x28,len(new)); struct.pack_into('<Q',b,0x40,size)
a.output.write_bytes(b)
print(f'{a.output}: high bank 0x180000000+{size:#x} ({a.mib} MiB)')
