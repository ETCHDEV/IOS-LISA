#!/usr/bin/env python3
"""Sync only Lisa's explicitly changed jobs into iOS's consolidated job cache."""
import argparse
import plistlib
from pathlib import Path

JOBS = (
    'System/Library/LaunchDaemons/dev.lisa.metal-smoke.plist',
    'System/Library/LaunchDaemons/com.apple.wifid.plist',
    'System/Library/LaunchDaemons/com.apple.voicemail.vmd.plist',
)


def sync(root):
    cache = root / 'System/Library/xpc/launchd.plist'
    data = plistlib.loads(cache.read_bytes())
    jobs = data['LaunchDaemons']
    if not isinstance(jobs, dict):
        raise ValueError('unexpected launchd job-cache layout')
    for rel in JOBS:
        job = plistlib.loads((root / rel).read_bytes())
        if not job.get('Label'):
            raise ValueError(f'{rel}: missing Label')
        key = '/' + rel
        existing = jobs.get(key)
        if existing and existing.get('Label') != job['Label']:
            raise ValueError(f'{key}: cache label mismatch')
        jobs[key] = job
        print('Cached job:', job['Label'])
    assert jobs['/' + JOBS[0]]['RunAtLoad']
    assert '_LimitLoadToDeviceTree' not in jobs['/' + JOBS[1]]
    assert jobs['/' + JOBS[2]]['Disabled']
    cache.write_bytes(plistlib.dumps(data, fmt=plistlib.FMT_BINARY))
    reread = plistlib.loads(cache.read_bytes())['LaunchDaemons']
    for rel in JOBS:
        assert reread['/' + rel] == plistlib.loads((root / rel).read_bytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    sync(parser.parse_args().root)
