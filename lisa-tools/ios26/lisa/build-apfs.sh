#!/bin/bash
# The KDK's APFS, AMFI and zlib drivers as plain arm64, prelinked into the kernel build-kernel.sh made, and its boot image:
# build-apfs.sh NAME ["boot args"]. zeta's audits and emitter; they refuse stale or mismatched inputs. Offline only.
set -uo pipefail
export PATH=/opt/homebrew/opt/llvm/bin:$PATH
L="$(cd "$(dirname "$0")" && pwd)"
T="$(cd "$L/../.." && pwd)"
NAME="${1:?usage: build-apfs.sh NAME [boot args]}"
ARGS="${2:--v debug=0x14e serial=3 keepsyms=1 lisa_ufs_probe=1 lisa_ufs_write=1 ufsrw=1 lisa_ufs_log=1 ufsroot=userdata native-ufs-root=1 native-ufs-diagnostics=1 lisa_reset_secs=180 exception_log_max_pid=1 dyld_flags=0x8}"
SEED="${LISA_DATA_SEED:-$T/ios26/work/data-seed.dbr}"
TRUST_BUNDLE="${LISA_TRUST_BUNDLE:-$T/ios26/work/trust-bundle.bin}"
Z="$T/research-none24smu-UniXNU-zeta/scripts"
U="$T/research-UniXNU-ios26"
W="$U/.cache/xnu-12377.121.6-work"
X="$T/ios26/kdk/expanded/KDK.pkg/Payload/System/Library"
E="$X/Extensions"
KK="$X/Kernels/kernel.development.t8103"
K="$U/build/xnu-12377.121.6/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf"
A="$L/out/apfs/$NAME"
rm -rf "$A"
mkdir -p "$A"
MACHO() { echo "$E/$1.kext/Contents/MacOS/$1"; }

"$L/.venv/bin/python" "$Z/audit-zeta-apfs-closure.py" --native "$K" --kdk-extensions "$E" \
	--root com.apple.filesystems.apfs --root com.apple.driver.AppleMobileFileIntegrity \
	--root com.apple.AppleFSCompression.AppleFSCompressionTypeZlib --root com.apple.security.sandbox --root com.apple.filesystems.tmpfs --root com.apple.iokit.IOMobileGraphicsFamily --root com.apple.iokit.IOSurface --root com.apple.iokit.IOHIDFamily --output "$A/closure.json" > "$A/closure.log" 2>&1 || { tail -3 "$A/closure.log" | cut -c1-170; echo CLOSURE-FAILED; exit 1; }
"$L/.venv/bin/python" "$Z/audit-zeta-apfs-vtables.py" --native "$K" --kdk-kernel "$KK" --kdk-storage "$(MACHO IOStorageFamily)" \
	--apfs "$(MACHO apfs)" --output "$A/vtables.json" > "$A/vtables.log" 2>&1 || { tail -3 "$A/vtables.log" | cut -c1-170; echo VTABLES-FAILED; exit 1; }
# Exit 3 is its normal answer here: the two providers only the KDK has are not in the kernel; the emitter checks that exact set.
"$L/.venv/bin/python" "$Z/audit-zeta-apfs-extra-vtables.py" --native "$K" --kdk-provider "$KK" --kdk-provider "$(MACHO IOStorageFamily)" \
	--kdk-provider "$(MACHO AppleEffaceableStorage)" --kdk-provider "$(MACHO CoreAnalyticsFamily)" \
	--apfs "$(MACHO apfs)" --apfs "$(MACHO AppleMobileFileIntegrity)" --apfs "$(MACHO AppleFSCompressionTypeZlib)" \
	--xnu-source "$W" --storage-source "$U/refer/IOStorageFamily" --record-instance-sizes --output "$A/extra.json" > "$A/extra.log" 2>&1
rc=$?
[ "$rc" = 0 ] || [ "$rc" = 3 ] || { tail -3 "$A/extra.log" | cut -c1-170; echo EXTRA-VTABLES-FAILED; exit 1; }
"$L/.venv/bin/python" "$Z/emit-zeta-apfs-prelink.py" --native "$K" --closure "$A/closure.json" --vtables "$A/vtables.json" --extra-vtables "$A/extra.json" \
	--kdk-extensions "$E" --xnu-source "$W" --storage-source "$U/refer/IOStorageFamily" --crypto-source "$U/refer/corecrypto-2023" \
	--output "$A/emit" 2>&1 | tail -6 | cut -c1-220
[ -f "$A/emit/kernel.macho" ] || { cat "$A/emit/rejected.json" 2>/dev/null | cut -c1-400; echo EMIT-FAILED; exit 1; }
cd "$T" || exit 1
rm -f "$L/out/$NAME.img"
# The OS window's APFS volume is root (build-lisa-root.py); SIP word as zeta needs for the rewritten dyld cache.
python3 -I "$L/make-lisa-adt.py" --dram-size 0x200000000 --cpu-regs ${LISA_CPU_REGS:-0,0x100,0x200,0x300,0x400,0x500,0x600,0x700} --unique-chip-id 62144b38 --chip-id 7325 \
	--root-match-volume-uuid B6C635B2-67F6-4034-B5D5-539E4D03D1EC --csr-config 0x2 --trust-cache --packed-data-ramdisk --gpu-firmware "$L/out/gpu-fw" || exit 1
read -r RB RS <<EOF_WINDOW
$(python3 -I - "$A/emit/kernel.macho" <<'EOF_PY'
import struct, sys
b = open(sys.argv[1], 'rb').read(); o = 32; low = None
for _ in range(struct.unpack_from('<I', b, 16)[0]):
    c, s = struct.unpack_from('<II', b, o)
    if c == 0x19:
        va, vs = struct.unpack_from('<2Q', b, o + 24)
        if vs: low = va if low is None else min(low, va)
    o += s
m = low & 0x1fffff
import os; gib = os.environ.get('LISA_RAM_GIB', '6'); base = 0x100000000 if gib == '6' else 0x180000000; size = 0x280000000 - base; print(hex(base + m), hex(size - m))   # 6 GiB of the phone's contiguous RAM (/proc/iomem); not the framebuffer's 1 GiB L1 slot, which start.s maps separately
EOF_PY
)
EOF_WINDOW
(cd diagnostic && OUT=out-xnu26 RAM_SIZE="${RS}ULL" LOADER_FLAGS="-DLISA_BOOT_LINE=1024 -DLISA_PAYLOAD_LINE=1024 -DLISA_RAM_BASE=${RB}ULL" sh build-xnu.sh > /dev/null 2>&1) || { echo LOADER-FAILED; exit 1; }
python3 -I "$L/mk-lisa26-image.py" diagnostic/out-ufs-xnu/lisa-ios-gl-v41ma.img diagnostic/out-xnu26/Image "$A/emit/kernel.macho" "$L/out/lisa.adt" "$L/out/$NAME.img" --data-seed "$SEED" --trust-cache "$TRUST_BUNDLE" --cmdline "$ARGS" --phys "$RB" --mem "$RS" || exit 1
echo "IMAGE-READY $L/out/$NAME.img"
