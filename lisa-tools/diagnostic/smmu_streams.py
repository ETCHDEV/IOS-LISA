#!/usr/bin/env python3
"""Inspect an offline ARM SMMUv2 stream-table snapshot; never accesses hardware.

Input JSON: {"id0": integer, "id1": integer,
             "entries": [{"smr": integer, "s2cr": integer}, ...]}
All entries advertised by ID0 must be present in hardware order. This is a
decoder, not a DMA safety test or authorization to modify the SMMU.
"""
import argparse
import json
import sys


def classify(snapshot, sid=0x80):
    def u32(value):
        if type(value) is not int or not 0 <= value <= 0xffffffff:
            raise ValueError('register values must be unsigned 32-bit integers')
        return value

    id0, id1 = u32(snapshot['id0']), u32(snapshot['id1'])
    if type(sid) is not int or not 0 <= sid <= 0xffff:
        raise ValueError('stream ID must be a 16-bit integer')
    entries = snapshot['entries']
    if not isinstance(entries, list) or len(entries) != (id0 & 0xff):
        raise ValueError('snapshot must contain every ID0-advertised stream entry')
    ncb = id1 & 0xff
    decoded = []
    for index, entry in enumerate(entries):
        smr, s2cr = u32(entry['smr']), u32(entry['s2cr'])
        valid, ident, mask = bool(smr & (1 << 31)), smr & 0xffff, (smr >> 16) & 0x7fff
        kind, bank = (s2cr >> 16) & 3, s2cr & 0xff
        if valid:
            match = (sid & ~mask & 0xffff) == (ident & ~mask & 0xffff)
            decoded.append({'index': index, 'id': ident, 'mask': mask,
                            'mode': ('translate', 'bypass', 'fault', 'reserved')[kind],
                            'context_bank': bank, 'bank_in_range': bank < ncb,
                            'matches_target': match})
    matches = [entry for entry in decoded if entry['matches_target']]
    banks = {entry['context_bank'] for entry in matches if entry['mode'] == 'translate'}
    shared = [entry for entry in decoded if not entry['matches_target']
              and entry['mode'] == 'translate' and entry['context_bank'] in banks]
    return {'target_sid': sid, 'num_context_banks': ncb, 'matches': matches,
            'other_entries_on_target_contexts': shared,
            'warning': 'Snapshot alone does not establish DMA safety or stream ownership.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', help='offline JSON register snapshot')
    parser.add_argument('--sid', type=lambda value: int(value, 0), default=0x80)
    args = parser.parse_args()
    try:
        with open(args.snapshot, encoding='utf-8') as f:
            result = classify(json.load(f), args.sid)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        parser.exit(2, f'invalid snapshot: {exc}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
