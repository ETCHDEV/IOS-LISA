#!/usr/bin/env python3
"""Extract porting facts from Lisa's captured FDT, without accessing hardware."""
import hashlib
import json
from pathlib import Path
import runpy
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'lisa-firmware/live/lisa-live.dtb'
parser = runpy.run_path(str(ROOT / 'research-hakios-os-20261026/tools/soc-args.py'))
blob = SOURCE.read_bytes()
nodes = parser['nodes_of'](blob)
handles = {struct.unpack('>I', p['phandle'])[0]: path for path, p in nodes.items() if 'phandle' in p}

def value(key, raw):
    if not raw:
        return True
    # GPIO/phandle cells often contain printable bytes surrounded by NULs.
    # They are not a string-list (e.g. <0x3c 0x55 0> was previously split into
    # empty strings). A genuine nonempty string-list starts with text.
    if raw[0] != 0 and b'\0\0' not in raw and raw.endswith(b'\0') and all(32 <= c < 127 or c == 0 for c in raw):
        return raw.rstrip(b'\0').decode().split('\0')
    if len(raw) % 4 == 0:
        cells = list(struct.unpack('>' + 'I' * (len(raw) // 4), raw))
        if key.endswith('-supply') and len(cells) == 1:
            return {'phandle': hex(cells[0]), 'node': handles.get(cells[0])}
        return [hex(c) for c in cells]
    return raw.hex()

selected = {}
for path, props in nodes.items():
    leaf = path.rsplit('/', 1)[-1]
    if not any(s in leaf for s in ('cam-sensor', 'csiphy', 'qcom,cci', 'bt_wcn', 'msm_fastrpc',
                                   'flash_led', 'camera-flash', 'tfa98', 'wcd93', 'sound',
                                   'qup_uart', 'slpi', 'ssc')):
        continue
    selected[path] = {k: value(k, v) for k, v in props.items() if k != 'phandle'}
assert len([p for p in selected if 'cam-sensor' in p]) == 4
report = {'source': str(SOURCE.relative_to(ROOT)), 'sha256': hashlib.sha256(blob).hexdigest(),
          'status': 'hardware description only; not initialized by this tool', 'nodes': selected}
print(json.dumps(report, indent=2))
