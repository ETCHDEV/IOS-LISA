#!/bin/bash
# Create an OS-only staging image. This is NOT a bootable iOS root.
set -euo pipefail
if [[ $(id -u) != 0 ]]; then
    echo 'Run this script with sudo so file ownership can be preserved.' >&2
    exit 1
fi
here=$(cd "$(dirname "$0")" && pwd -P)
artifacts="$here/ios-artifacts"
source_image="$artifacts/087-86698-044.dmg"
output="$artifacts/lisa-ios-os-only.dmg"
[[ -f "$source_image" && ! -L "$source_image" ]] || { echo 'Missing source image' >&2; exit 1; }
[[ ! -e "$output" && ! -L "$output" ]] || { echo 'Output exists; refusing to overwrite' >&2; exit 1; }
mountpoint=$(mktemp -d /private/tmp/lisa-ios-source.XXXXXXXX)
attached=0
cleanup() {
    if [[ $attached == 1 ]]; then
        hdiutil detach "$mountpoint" || echo "Detach manually: $mountpoint" >&2
    fi
    rmdir "$mountpoint" 2>/dev/null || true
}
trap cleanup EXIT
hdiutil attach -readonly -nobrowse -owners on -mountpoint "$mountpoint" "$source_image"
attached=1
[[ -d "$mountpoint/System/Library" && -d "$mountpoint/usr" ]] || { echo 'Unexpected source layout' >&2; exit 1; }
hdiutil create -size 16g -layout NONE -fs 'Case-sensitive Journaled HFS+' \
    -volname iOSRoot -srcfolder "$mountpoint" -srcowners on -noanyowners \
    -format UDRW "$output"
echo "OS-only stage created: $output"
echo 'Do not flash: cryptexes, root tweaks, and Lisa UFS support are still required.'
