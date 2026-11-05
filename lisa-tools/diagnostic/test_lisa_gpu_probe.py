#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
src = (root / 'darwin-xnu-build/xnu/bsd/dev/lisa_adreno_probe.c').read_text()
conf = (root / 'darwin-xnu-build/xnu/bsd/conf/files').read_text()
assert 'bsd/dev/lisa_adreno_probe.c' in conf
assert 'lisagpu_probe' in src
assert 'ml_io_map' in src
assert '__asm__ volatile("ldr' in src
code = '\n'.join(line for line in src.splitlines() if not line.lstrip().startswith('*'))
for forbidden in ('\"str ', 'gpu_wr(', 'rpmh_', 'firmware_load', 'submit('):
    assert forbidden not in code, forbidden
for pa in ('0x03d90000u', '0x03da0000u', '0x03d00000u'):
    assert pa in src
print('Lisa GPU probe structural checks passed: opt-in and read-only')
