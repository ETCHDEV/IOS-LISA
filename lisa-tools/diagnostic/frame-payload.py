#!/usr/bin/env python3
"""Frame a SWEETXNU payload for the END of a v3 concatenated initrd.

Output is for offline testing. The assumed initrd start and vendor prefix
must be measured on the target; framing does not prove a boot will be safe.
"""
import argparse
import pathlib
import struct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('payload')
p.add_argument('output')
p.add_argument('--vendor-size', required=True, type=lambda x:int(x,0))
p.add_argument('--initrd-start', required=True, type=lambda x:int(x,0))
a=p.parse_args()
b=pathlib.Path(a.payload).read_bytes()
if len(b)<0x2a8 or b[:8]!=b'SWEETXNU': p.error('invalid SWEETXNU payload')
if a.vendor_size<0 or a.initrd_start<0 or a.vendor_size+a.initrd_start >= 1<<64: p.error('invalid input addresses')
pad=(-(a.initrd_start+a.vendor_size))%8
with pathlib.Path(a.output).open('xb') as f:
    f.write(bytes(pad));f.write(b);f.write(b'LISAEND1'+struct.pack('<Q',len(b)))
print('prefix padding:',pad,'payload:',len(b),'bytes; assumes specified placement')
