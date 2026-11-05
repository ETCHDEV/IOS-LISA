#!/usr/bin/env python3
"""Pack Lisa's vendor A642L firmware for mkpayload.py --gpu-firmware."""
import argparse, pathlib, struct
MAGIC=b'LISAGPUF'; ALIGN=0x4000
HEADER=struct.Struct('<8sII')
# Fixed-width records keep the early-boot C parser independent of compiler
# alignment.  The final four bytes are reserved for a future flags field.
ENTRY=struct.Struct('<I64sQ4x')
ap=argparse.ArgumentParser(); ap.add_argument('out'); ap.add_argument('files', nargs='+'); a=ap.parse_args()
items=[]
for name in a.files:
    p=pathlib.Path(name); data=p.read_bytes(); items.append((p.name,data))
hdr=bytearray(HEADER.pack(MAGIC,1,len(items)))
for n,d in items:
    b=n.encode(); assert len(b)<64
    hdr+=ENTRY.pack(len(b), b.ljust(64,b'\0'), len(d))
off=(len(hdr)+ALIGN-1)&-ALIGN; out=bytearray(off); out[:len(hdr)]=hdr
for n,d in items:
    out+=d; out+=bytes((-len(out))&(ALIGN-1))
pathlib.Path(a.out).write_bytes(out)
