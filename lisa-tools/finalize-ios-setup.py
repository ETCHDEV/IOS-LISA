#!/usr/bin/env python3
"""Apply upstream research image setup to an isolated mounted image copy."""
import glob
import os
import plistlib
import subprocess
import sys

root = os.path.abspath(sys.argv[1])
if os.geteuid() != 0 or not os.path.ismount(root):
    sys.exit('Requires root and a mounted staging-image root')

def safe(relative):
    path = os.path.abspath(os.path.join(root, relative))
    if os.path.commonpath((root, path)) != root:
        raise ValueError('Path escapes image: ' + path)
    current = root
    for part in os.path.relpath(path, root).split(os.sep):
        current = os.path.join(current, part)
        if os.path.islink(current):
            raise ValueError('Refusing symlink: ' + current)
    return path

paths = ['System/Library/xpc/launchd.plist', 'private/var/containers',
         'private/var/mobile/Library/Preferences', 'private/var/db/diagnostics',
         'private/var/db/diagnostics/timesync', 'private/var/db/uuidtext',
         'private/var/keybags/systembag.kb', 'Library/Audio/Plug-Ins/HAL/VirtualAudio.plugin',
         'Library/Audio/Plug-Ins/HAL.disabled/VirtualAudio.plugin']
for name in ('migration', 'springboard', 'purplebuddy', 'purplebuddy.notbackedup',
             'managedconfiguration.notbackedup'):
    paths.append('private/var/mobile/Library/Preferences/com.apple.' + name + '.plist')
for rel in paths:
    safe(rel)
for d in ('diagnostics', 'diagnostics/timesync', 'uuidtext'):
    safe('private/var/db/' + d + '/.migrated')
for p in glob.glob(os.path.join(root, 'System/Library/Frameworks/MediaToolbox.framework/*/SubServerConfiguration.plist')):
    safe(os.path.relpath(p, root))
# Upstream searches existing containers using glob and can follow their links.
# This known Apple source contains no containers; reject unexpected input.
if os.path.lexists(safe('private/var/containers')):
    raise ValueError('Unexpected pre-existing containers; manual audit required')

for d, mode in (('diagnostics', 0o751), ('diagnostics/timesync', 0o755), ('uuidtext', 0o755)):
    path = safe('private/var/db/' + d)
    os.makedirs(path, exist_ok=True)
    os.chown(path, 272, 272)
    os.chmod(path, mode)
    marker = safe('private/var/db/' + d + '/.migrated')
    with open(marker, 'wb'):
        pass
    os.chown(marker, 272, 272)
    os.chmod(marker, 0o644)
keydir = safe('private/var/keybags')
os.makedirs(keydir, exist_ok=True)
os.chown(keydir, 0, 0)
os.chmod(keydir, 0o700)
keybag = safe('private/var/keybags/systembag.kb')
with open(keybag, 'wb') as f:
    plistlib.dump({'KeyBagKeys': b'SWEET'}, f, fmt=plistlib.FMT_XML)
os.chown(keybag, 0, 0)
os.chmod(keybag, 0o600)
upstream = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'lisa-build/tools/sweet-root-tweaks.py')
subprocess.run([sys.executable, upstream, root], check=True)
print('Applied upstream research setup; Lisa kernel and drivers still required.')
