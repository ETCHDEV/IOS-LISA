#!/usr/bin/env python3
"""Add the upstream cryptex overlay to a mounted local staging image.

This does not provide Lisa hardware support or make the image bootable.
"""
import os
import stat
import sys

root = os.path.abspath(sys.argv[1])
if os.geteuid() != 0 or not os.path.ismount(root):
    sys.exit('Requires root and a mounted staging-image root')

def check(path):
    path = os.path.abspath(path)
    if os.path.commonpath([root, path]) != root:
        raise ValueError('path escapes image: ' + path)
    rel = os.path.relpath(path, root)
    current = root
    for part in rel.split(os.sep):
        current = os.path.join(current, part)
        if os.path.islink(current):
            raise ValueError('symlink in destination: ' + current)
    return path

def mkdir(path, mode=0o755):
    check(path)
    os.makedirs(path, mode=mode, exist_ok=True)

planned = {}
cache_rel = 'System/Library/Caches/com.apple.dyld'
for name in ('OS', 'App'):
    base = check(os.path.join(root, 'private/preboot/Cryptexes', name))
    if not os.path.isdir(base):
        raise ValueError('missing cryptex: ' + base)
    for directory, dirs, files in os.walk(base, followlinks=False):
        linked_dirs = [n for n in dirs if os.path.islink(os.path.join(directory, n))]
        dirs[:] = [n for n in dirs if n not in linked_dirs]
        for filename in files + linked_dirs:
            src = os.path.join(directory, filename)
            rel = os.path.relpath(src, base)
            if rel.startswith(cache_rel + '/'):
                continue
            dst = os.path.join(root, rel)
            if os.path.lexists(dst) or dst in planned:
                continue
            check(os.path.dirname(dst))
            mode = os.lstat(src).st_mode
            if not (stat.S_ISREG(mode) or stat.S_ISLNK(mode)):
                raise ValueError('unsupported cryptex file: ' + src)
            planned[dst] = src

cache = check(os.path.join(root, 'private/preboot/Cryptexes/OS', cache_rel))
if not os.path.isdir(cache):
    raise ValueError('missing dyld cache: ' + cache)
for filename in os.listdir(cache):
    src = os.path.join(cache, filename)
    if not stat.S_ISREG(os.lstat(src).st_mode):
        raise ValueError('unexpected dyld cache entry: ' + src)
    dst = check(os.path.join(root, cache_rel, filename))
    if os.path.lexists(dst):
        raise ValueError('dyld cache target already exists: ' + dst)
    planned[dst] = src

for dst, src in planned.items():
    mkdir(os.path.dirname(dst))
    if os.path.islink(src):
        os.symlink(os.readlink(src), dst)
    else:
        os.link(src, dst, follow_symlinks=False)
print('Created', len(planned), 'cryptex overlay entries')
print('Root tweaks, Lisa drivers and device boot remain incomplete.')
