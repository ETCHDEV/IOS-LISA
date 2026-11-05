#!/bin/sh
# Builds a no-copy, no-jump diagnostic, not an XNU loader.
set -eu
cd "$(dirname "$0")"
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
LLD=${LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
mkdir -p out-preflight
flags="--target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror"
"$LLVM/clang" $flags -c ../../ios-sweet/shim/start.S -o out-preflight/start.o
"$LLVM/clang" $flags -DLISA_PREFLIGHT -c main.c -o out-preflight/main.o
for src in fdt_bounded payload_tail payload_validate payload_preflight; do
    "$LLVM/clang" $flags -c "$src.c" -o "out-preflight/$src.o"
done
"$LLD" -T ../../ios-sweet/shim/shim.ld --build-id=none out-preflight/start.o out-preflight/main.o out-preflight/fdt_bounded.o out-preflight/payload_tail.o out-preflight/payload_validate.o out-preflight/payload_preflight.o -o out-preflight/lisa-preflight.elf
"$LLVM/llvm-objcopy" -O binary out-preflight/lisa-preflight.elf out-preflight/Image
"$LLVM/llvm-nm" -n out-preflight/lisa-preflight.elf > out-preflight/symbols.txt
python3 -c 'from pathlib import Path; s=Path("out-preflight/symbols.txt").read_text().splitlines(); end=next(int(x.split()[0],16) for x in s if x.split()[-1]=="_end"); p=Path("out-preflight/Image"); data=p.read_bytes(); assert len(data)<=end; p.write_bytes(data+bytes(end-len(data)))'
"$LLVM/llvm-readelf" -r out-preflight/lisa-preflight.elf
