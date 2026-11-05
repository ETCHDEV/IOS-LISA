#!/usr/bin/env python3
"""Restore matching previously generated interfaces into a local XNU build.

The generated files come from an earlier build; kernel compilation is local.
This only handles the previously prepared 17 IOKit interface inputs.
"""
import argparse
import pathlib
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference_tree', type=pathlib.Path)
    parser.add_argument('local_tree', type=pathlib.Path)
    args = parser.parse_args()
    ref, local = args.reference_tree.resolve(), args.local_tree.resolve()
    source = ref / 'xnu' / 'iokit'
    inputs = list(source.rglob('*.iig'))
    if len(inputs) != 17:
        parser.error(f'expected 17 interface inputs, found {len(inputs)}')
    for src in inputs:
        other = local / 'xnu' / src.relative_to(ref / 'xnu')
        if not other.is_file() or src.read_bytes() != other.read_bytes():
            parser.error(f'interface input differs: {src.relative_to(source)}')
    old = ref / 'build/xnu.obj/DEVELOPMENT_ARM64_SWEET'
    new = local / 'build/xnu.obj/DEVELOPMENT_ARM64_SWEET'
    generated = list(old.rglob('*.iig.cpp'))
    if len(generated) != 34:
        parser.error(f'expected 34 generated source files, found {len(generated)}')
    files = []
    for src in generated:
        header = src.with_name(src.name.removesuffix('.iig.cpp') + '.h')
        if not header.is_file():
            parser.error(f'missing generated header: {header}')
        files.extend((src, header))
    for src in files:
        dst = new / src.relative_to(old)
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
    print(f'restored {len(files)} generated files from matching interface inputs')


if __name__ == '__main__':
    main()
