#!/usr/bin/env python3
"""Add a stable synthetic Wi-Fi identity to an experimental Lisa ADT.

This supplies identity data only. It does not initialize Wi-Fi hardware.
"""
import argparse
import hashlib
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'ios-sweet' / 'tools'))
import adt

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('input')
p.add_argument('output')
a = p.parse_args()
source = pathlib.Path(a.input).read_bytes()
tree, end = adt.parse(source)
if end != len(source):
    p.error('unexpected trailing ADT data')
chosen = [node for node in tree.children if node.name == 'chosen']
if len(chosen) != 1 or chosen[0].prop('mac-address-wifi0') is not None:
    p.error('missing chosen node or Wi-Fi identity already present')
ecid = chosen[0].prop('unique-chip-id')
if ecid is None or len(ecid) != 8:
    p.error('expected stable synthetic ECID')
mac = bytearray(hashlib.sha256(b'ios-lisa-wifi-identity-v1' + ecid).digest()[:6])
mac[0] = (mac[0] | 2) & 0xfe
chosen[0].props.append(('mac-address-wifi0', 0, bytes(mac)))
encoded = adt.serialize(tree)
check, used = adt.parse(encoded)
assert used == len(encoded) and next(n for n in check.children if n.name == 'chosen').prop('mac-address-wifi0') == bytes(mac)
with open(a.output, 'xb') as out:
    out.write(encoded)
print('Added a locally administered synthetic six-byte Wi-Fi identity; no Wi-Fi driver enabled.')
