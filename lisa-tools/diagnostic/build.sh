#!/bin/sh
# Builds only an unbootable-by-itself ARM64 Linux Image; no boot.img produced.
set -eu
cd "$(dirname "$0")"
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
LLD=${LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
mkdir -p out
"$LLVM/clang" --target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -c ../../ios-sweet/shim/start.S -o out/start.o
"$LLVM/clang" --target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -c main.c -o out/main.o
"$LLVM/clang" --target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -c ../../ios-sweet/shim/fdt.c -o out/fdt.o
"$LLD" -T ../../ios-sweet/shim/shim.ld --build-id=none out/start.o out/main.o out/fdt.o -o out/lisa-diagnostic.elf
"$LLVM/llvm-objcopy" -O binary out/lisa-diagnostic.elf out/Image
"$LLVM/llvm-nm" -n out/lisa-diagnostic.elf > out/symbols.txt
python3 -c 'from pathlib import Path; s=Path("out/symbols.txt").read_text().splitlines(); end=next(int(x.split()[0],16) for x in s if x.split()[-1]=="_end"); p=Path("out/Image"); data=p.read_bytes(); assert len(data)<=end; p.write_bytes(data+bytes(end-len(data)))'
"$LLVM/llvm-readelf" -r out/lisa-diagnostic.elf
