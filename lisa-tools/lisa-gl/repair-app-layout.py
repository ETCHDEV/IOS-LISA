#!/usr/bin/env python3
"""Install complete staged stock apps into the port's system-app directory.

Use only on a mounted local staging image, before generating the full sparse image.
No placeholders are promoted and no executable or signature is modified.
"""
import argparse
import hashlib
import json
import pathlib
import plistlib

ap = argparse.ArgumentParser()
ap.add_argument('root', type=pathlib.Path)
ap.add_argument('--apply', action='store_true')
args = ap.parse_args()
root = args.root.resolve()
assert (root / 'System/Library/CoreServices/SystemVersion.plist').is_file()
staged = root / 'private/var/staged_system_apps'
manifest = []
for source in sorted(staged.glob('*.app')):
    info = plistlib.loads((source / 'Info.plist').read_bytes())
    binary = source / info['CFBundleExecutable']
    assert binary.is_file() and binary.stat().st_size > 32, f'Missing executable: {source}'
    dest = root / 'Applications' / source.name
    assert not dest.exists(), f'Refusing to overwrite {dest}'
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    manifest.append({'bundle': source.name, 'identifier': info['CFBundleIdentifier'], 'sha256': digest})
    if args.apply:
        source.rename(dest)
        assert hashlib.sha256((dest / binary.name).read_bytes()).hexdigest() == digest

# Empty writable music-library parents let medialibraryd initialise the actual
# library; do not fabricate a SQLite database or return synthetic storage data.
media = root / 'private/var/mobile/Media'
media_dirs = ['iTunes_Control', 'iTunes_Control/iTunes', 'iTunes_Control/Music']
for rel in media_dirs:
    if args.apply:
        path = media / rel
        path.mkdir(parents=True, exist_ok=True)
        path.chmod(0o750)

observer = '/usr/local/lib/lisa/liblisaexceptions.dylib' 
def update_job(job):
    if job.get('Label') not in ('com.apple.PosterBoard', 'com.apple.assetsd'):
        return
    env = job.setdefault('EnvironmentVariables', {})
    libs = [x for x in env.get('DYLD_INSERT_LIBRARIES', '').split(':') if x]
    if observer not in libs:
        libs.append(observer)
    if job.get('Label') == 'com.apple.PosterBoard':
        poster = '/usr/local/lib/lisa/liblisaposter.dylib'
        if poster not in libs:
            libs.append(poster)
    env['DYLD_INSERT_LIBRARIES'] = ':'.join(libs)

def relocate(value):
    if isinstance(value, str):
        return value.replace('/private/var/staged_system_apps/', '/Applications/')
    if isinstance(value, dict):
        return {relocate(k): relocate(v) for k, v in value.items()}
    if isinstance(value, list):
        return [relocate(v) for v in value]
    return value

paths = ['System/Library/xpc/launchd.plist',
         'System/Library/LaunchAngels/com.apple.PosterBoard.plist',
         'System/Library/LaunchDaemons/com.apple.assetsd.plist']
for rel in paths:
    p = root / rel
    if not p.is_file():
        continue
    data = relocate(plistlib.loads(p.read_bytes()))
    if rel.endswith('xpc/launchd.plist'):
        for job in data.get('LaunchDaemons', {}).values():
            update_job(job)
    else:
        update_job(data)
    if args.apply:
        p.write_bytes(plistlib.dumps(data, fmt=plistlib.FMT_BINARY))
print(json.dumps({'apply': args.apply, 'apps': manifest}, indent=2))
