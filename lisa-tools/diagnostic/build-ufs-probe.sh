#!/bin/sh
# Read-only UFS controller status diagnostic, not an XNU loader.
set -eu
cd "$(dirname "$0")"
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
LLD=${LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
mkdir -p out-ufs-probe
flags="--target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror"
"$LLVM/clang" $flags -c ../../ios-sweet/shim/start.S -o out-ufs-probe/start.o
"$LLVM/clang" $flags -DLISA_UFS_PROBE -c main.c -o out-ufs-probe/main.o
"$LLVM/clang" $flags -c ../../ios-sweet/shim/fdt.c -o out-ufs-probe/fdt.o
"$LLD" -T ../../ios-sweet/shim/shim.ld --build-id=none out-ufs-probe/start.o out-ufs-probe/main.o out-ufs-probe/fdt.o -o out-ufs-probe/lisa-ufs-probe.elf
"$LLVM/llvm-objcopy" -O binary out-ufs-probe/lisa-ufs-probe.elf out-ufs-probe/Image
"$LLVM/llvm-nm" -n out-ufs-probe/lisa-ufs-probe.elf > out-ufs-probe/symbols.txt
python3 -c 'from pathlib import Path; s=Path("out-ufs-probe/symbols.txt").read_text().splitlines(); end=next(int(x.split()[0],16) for x in s if x.split()[-1]=="_end"); p=Path("out-ufs-probe/Image"); data=p.read_bytes(); assert len(data)<=end; p.write_bytes(data+bytes(end-len(data)))'
"$LLVM/llvm-readelf" -r out-ufs-probe/lisa-ufs-probe.elf
