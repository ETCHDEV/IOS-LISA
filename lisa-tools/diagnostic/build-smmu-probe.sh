#!/bin/sh
# Read-only SMMU stream status diagnostic, not an XNU loader.
set -eu
cd "$(dirname "$0")"
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
LLD=${LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
mkdir -p out-smmu-probe
flags="--target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror"
"$LLVM/clang" $flags -c ../../ios-sweet/shim/start.S -o out-smmu-probe/start.o
"$LLVM/clang" $flags -DLISA_SMMU_PROBE -c main.c -o out-smmu-probe/main.o
"$LLVM/clang" $flags -c ../../ios-sweet/shim/fdt.c -o out-smmu-probe/fdt.o
"$LLD" -T ../../ios-sweet/shim/shim.ld --build-id=none out-smmu-probe/start.o out-smmu-probe/main.o out-smmu-probe/fdt.o -o out-smmu-probe/lisa-smmu-probe.elf
"$LLVM/llvm-objcopy" -O binary out-smmu-probe/lisa-smmu-probe.elf out-smmu-probe/Image
"$LLVM/llvm-nm" -n out-smmu-probe/lisa-smmu-probe.elf > out-smmu-probe/symbols.txt
python3 -c 'from pathlib import Path; s=Path("out-smmu-probe/symbols.txt").read_text().splitlines(); end=next(int(x.split()[0],16) for x in s if x.split()[-1]=="_end"); p=Path("out-smmu-probe/Image"); data=p.read_bytes(); assert len(data)<=end; p.write_bytes(data+bytes(end-len(data)))'
"$LLVM/llvm-readelf" -r out-smmu-probe/lisa-smmu-probe.elf
