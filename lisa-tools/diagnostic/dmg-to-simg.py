#!/usr/bin/env python3
import os, struct, sys
src, dst = sys.argv[1:3]
BS, BLOCKS = 4096, 1024
RAW, DONT = 0xCAC1, 0xCAC3
size=os.path.getsize(src)
if size % BS: raise SystemExit('input must be 4K aligned')
total=size//BS
chunks=0
with open(src,'rb',buffering=4*1024*1024) as i, open(dst,'wb+',buffering=4*1024*1024) as o:
    o.write(b'\0'*28)
    done=0
    while done < total:
        n=min(BLOCKS,total-done); data=i.read(n*BS)
        if len(data)!=n*BS: raise SystemExit('short input')
        if data == b'\0'*len(data):
            o.write(struct.pack('<HHII',DONT,0,n,12))
        else:
            o.write(struct.pack('<HHII',RAW,0,n,12+len(data))); o.write(data)
        chunks+=1; done+=n
        if chunks % 256 == 0: print(f'{done*BS//(1024*1024)} MiB',flush=True)
    o.seek(0); o.write(struct.pack('<IHHHHIIII',0xed26ff3a,1,0,28,12,BS,total,chunks,0))
