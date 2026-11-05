#!/bin/sh
# Experimental XNU entry image. This writes RAM and runs XNU; do not flash.
set -eu
cd "$(dirname "$0")"
LLVM=${LLVM:-/opt/homebrew/opt/llvm/bin}
LLD=${LLD:-/opt/homebrew/opt/lld/bin/ld.lld}
OUT=${OUT:-out-xnu}
RAM_SIZE=${RAM_SIZE:-0x20000000ULL}
mkdir -p "$OUT"
flags="--target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only -mstrict-align -Wall -Wextra -Werror"
"$LLVM/clang" $flags -c ../../ios-sweet/shim/start.S -o "$OUT/start.o"
"$LLVM/clang" $flags ${LOADER_FLAGS:-} -DLISA_PREFLIGHT -DLISA_XNU_BOOT -DLISA_RAM_SIZE="$RAM_SIZE" -c main.c -o "$OUT/main.o"
for src in fdt_bounded payload_tail payload_validate payload_preflight xnu_loader; do
    "$LLVM/clang" $flags ${LOADER_FLAGS:-} -c "$src.c" -o "$OUT/$src.o"
done
"$LLD" -T ../../ios-sweet/shim/shim.ld --build-id=none "$OUT/start.o" "$OUT/main.o" "$OUT/fdt_bounded.o" "$OUT/payload_tail.o" "$OUT/payload_validate.o" "$OUT/payload_preflight.o" "$OUT/xnu_loader.o" -o "$OUT/lisa-xnu.elf"
"$LLVM/llvm-objcopy" -O binary "$OUT/lisa-xnu.elf" "$OUT/Image"
"$LLVM/llvm-nm" -n "$OUT/lisa-xnu.elf" > "$OUT/symbols.txt"
python3 -c 'from pathlib import Path; import sys; d=Path(sys.argv[1]); s=(d/"symbols.txt").read_text().splitlines(); end=next(int(x.split()[0],16) for x in s if x.split()[-1]=="_end"); p=d/"Image"; data=p.read_bytes(); assert len(data)<=end; p.write_bytes(data+bytes(end-len(data)))' "$OUT"
"$LLVM/llvm-readelf" -r "$OUT/lisa-xnu.elf"
