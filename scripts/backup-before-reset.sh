#!/bin/bash
# Copy what is NOT in this repo to an external disk before wiping the Mac.
#   scripts/backup-before-reset.sh SRC_TREE DEST           about 2 GB: cannot be downloaded again
#   scripts/backup-before-reset.sh SRC_TREE DEST --full    about 85 GB more: saves rebuilding everything
# DEST holds a private signing key and device dumps. Keep it private; never upload it to a public place.
set -euo pipefail
SRC="${1:?usage: backup-before-reset.sh SRC_TREE DEST [--full]}"
DEST="${2:?usage: backup-before-reset.sh SRC_TREE DEST [--full]}"
cd "$SRC"
ESSENTIAL=(
	lisa-tools/ios26/lisa/.cache/owner-root
	ios-sweet/backup
	ios-sweet/dtb
	lisa-tools/lisa-firmware
	lisa-tools/ios26/stock-firmware
	lisa-tools/ios26/lisa/logs
	lisa-tools/diagnostic/logs
	lisa-tools/diagnostic/shots
	lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v41ma.img
	lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v41ma-payload.bin
	lisa-tools/lisa-gl/out
	lisa-build/artifacts
	lisa-tools/ios26/work/trust-bundle.bin
	lisa-tools/ios26/raw/trust
)
FULL=(
	lisa-tools/ios-artifacts
	lisa-tools/ios26/lisa/out
	lisa-tools/ios26/work
	lisa-tools/ios26/raw
	lisa-tools/ios26/kdk
	lisa-tools/ios26/iPhone18,2_26.5.2_23F84_Restore.ipsw
)
LIST=("${ESSENTIAL[@]}")
# Data seeds are small but tied to the kernel's volume UUID.
for f in lisa-tools/ios26/work/data-seed*; do LIST+=("$f"); done
[ "${3:-}" = "--full" ] && LIST+=("${FULL[@]}")
mkdir -p "$DEST"
for item in "${LIST[@]}"; do
	if [ -e "$item" ]; then echo "copy $item"; rsync -aR --sparse "./$item" "$DEST/"; else echo "MISSING $item"; fi
done
du -sh "$DEST"
