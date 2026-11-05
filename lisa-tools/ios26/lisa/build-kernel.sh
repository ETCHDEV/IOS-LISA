#!/bin/bash
# xnu-12377 for Lisa and its boot image, RAM boot only: build-kernel.sh NAME ["boot args"]
# UniXNU's patch series does not reapply over itself, so the generated work copy is reset each time.
set -uo pipefail
export PATH=/opt/homebrew/opt/llvm/bin:$PATH
L="$(cd "$(dirname "$0")" && pwd)"
T="$(cd "$L/../.." && pwd)"
U="$T/research-UniXNU-ios26"
NAME="${1:?usage: build-kernel.sh NAME [boot args]}"
ARGS="${2:--v debug=0x14e serial=3 cpus=1 keepsyms=1 lisa_ufs_probe=1 lisa_ufs_write=1 ufsrw=1 lisa_ufs_log=1 ufsroot=userdata native-ufs-root=1 native-ufs-diagnostics=1}"
O="$L/out"
LOG="$O/$NAME-build.log"
SEED="${LISA_DATA_SEED:-$T/ios26/work/data-seed.dbr}"
SEED_REPORT="${SEED%.dbr}.json"
mkdir -p "$O"
cd "$U" || exit 1
W=.cache/xnu-12377.121.6-work
git -C "$W" reset -q --hard && git -C "$W" clean -qfdx || exit 1
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer JOBS=8
export XNU_NATIVE_ARCH_TIMER=1 XNU_VIRTUAL_TIMER_ONLY=1 XNU_SOURCE_CORECRYPTO14=1 XNU_SOURCE_PTHREAD=1
export XNU_ARM_DT_PLATFORM=1 XNU_ARMCPU=1 XNU_NATIVE_PAN=1 XNU_DT_PANIC_REGION=1 XNU_TRUST_CACHE_RUNTIME=1
export XNU_DYLD_FLAGS_BOOTARG=1 XNU_PROC_TRACE_CONSOLE=1 XNU_EDT_FSTAB=1
export XNU_NATIVE_LEGACY_DISPLAY=1 XNU_NATIVE_IOS_CLASS_ALIASES=1
export XNU_NATIVE_RAM_BLOCK=1 XNU_OWNER_DATA_MOUNT=1 XNU_OWNER_DATA_VOLUME_UUID="$(python3 -I -c "import json;print(json.load(open('$SEED_REPORT'))['volume_uuid'])")"
# zeta_apfs_owner_root_defconfig's APFS profile. NativeUfs's windows and policies read through sweet_ufs (LISA_UFS_HOST).
export XNU_NATIVE_UFS_SM8350=1 XNU_NATIVE_UFS_VERIFIED_ROOT=1 XNU_NATIVE_UFS_OWNER_ROOT=1
export XNU_KDK_IOKIT_ABI=${XNU_KDK_IOKIT_ABI:-1} XNU_APFS_CRYPTO_KPIS=${XNU_APFS_CRYPTO_KPIS:-1} XNU_APFS_CSR=${XNU_APFS_CSR:-1} XNU_APFS_MLDSA=${XNU_APFS_MLDSA:-1}
bash scripts/build-xnu-12377.sh > "$LOG" 2>&1
rc=$?
grep -E ': error:|fatal error|build-xnu-12377.sh: line|Undefined symbols|^  "_' "$LOG" | sed -E 's/^.*research-UniXNU-ios26\///' | cut -c1-180 | sort -u | head -30
[ "$rc" = 0 ] || { echo "KERNEL-BUILD-FAILED rc=$rc, log $LOG"; exit 1; }
K="$U/build/xnu-12377.121.6/ios-obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837.unstripped.noctf"
cd "$T" || exit 1
python3 -I "$L/make-lisa-adt.py" --dram-size 0x200000000 --cpu-regs 0 --unique-chip-id 62144b38 --chip-id 7325 || exit 1
rm -f "$O/$NAME.img"
python3 -I "$L/mk-lisa26-image.py" diagnostic/out-ufs-xnu/lisa-ios-gl-v41ma.img diagnostic/out-xnu26/Image "$K" "$O/lisa.adt" "$O/$NAME.img" --cmdline "$ARGS" || exit 1
echo "IMAGE-READY $O/$NAME.img"
