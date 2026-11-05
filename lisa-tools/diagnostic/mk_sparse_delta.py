#!/usr/bin/env python3
import argparse, struct
from pathlib import Path
MAGIC=0xED26FF3A; RAW=0xCAC1; DONT=0xCAC3
BLK=4096; MAX_RAW_BLOCKS=4096
p=argparse.ArgumentParser(); p.add_argument('base'); p.add_argument('new'); p.add_argument('out'); a=p.parse_args()
base=Path(a.base); new=Path(a.new)
if base.stat().st_size!=new.stat().st_size or base.stat().st_size%BLK: raise SystemExit('incompatible images')
total=base.stat().st_size//BLK
chunks=[]; raw=[]; skip=0

def emit_skip(n):
 if n: chunks.append((DONT,n,None))
def emit_raw(data):
 if data: chunks.append((RAW,len(data)//BLK,bytes(data)))
with base.open('rb',buffering=0) as f, new.open('rb',buffering=0) as g:
 for i in range(total):
  x=f.read(BLK); y=g.read(BLK)
  if x==y:
   if raw: emit_skip(skip); skip=0; emit_raw(raw); raw.clear()
   skip+=1
  else:
   if len(raw)//BLK>=MAX_RAW_BLOCKS:
    emit_skip(skip); skip=0; emit_raw(raw); raw.clear()
   raw.extend(y)
 if raw: emit_skip(skip); skip=0; emit_raw(raw)
 if skip: emit_skip(skip)
with Path(a.out).open('wb') as o:
 o.write(struct.pack('<I4H4I',MAGIC,1,0,28,12,BLK,total,len(chunks),0))
 for typ,n,data in chunks:
  if typ==RAW:
   o.write(struct.pack('<2H2I',typ,0,n,12+len(data))); o.write(data)
  else: o.write(struct.pack('<2H2I',typ,0,n,12))
print(f'blocks={total} chunks={len(chunks)} raw_bytes={sum(len(c[2]) for c in chunks if c[0]==RAW)} output={Path(a.out).stat().st_size}')
