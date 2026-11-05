#!/bin/bash
# Create a separate uncompressed HFS+ image; never touches the phone.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Administrator privileges required.' >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd -P)
src="$here/ios-artifacts/lisa-ios-research-stage.dmg"
out="$here/ios-artifacts/lisa-ios-uncompressed-stage.dmg"
[[ -f "$src" && ! -L "$src" && ! -e "$out" && ! -L "$out" ]] || { echo 'Source missing or destination exists.' >&2; exit 1; }
srcdir=$(mktemp -d /private/tmp/lisa-ios-src.XXXXXXXX)
dstdir=$(mktemp -d /private/tmp/lisa-ios-dst.XXXXXXXX)
src_attached=0
dst_attached=0
cleanup() {
    if ((dst_attached)); then hdiutil detach "$dstdir" || echo "Detach manually: $dstdir" >&2; fi
    if ((src_attached)); then hdiutil detach "$srcdir" || echo "Detach manually: $srcdir" >&2; fi
    rmdir "$srcdir" "$dstdir" 2>/dev/null || true
}
trap cleanup EXIT
hdiutil attach -readonly -nobrowse -owners on -mountpoint "$srcdir" "$src"
src_attached=1
hdiutil create -size 24g -layout NONE -fs 'Case-sensitive Journaled HFS+' -volname iOSRoot "$out"
hdiutil attach -nobrowse -owners on -mountpoint "$dstdir" "$out"
dst_attached=1
ditto --nopreserveHFSCompression --nohfsCompression "$srcdir/" "$dstdir/"
for file in sbin/launchd usr/lib/dyld; do
    cmp "$srcdir/$file" "$dstdir/$file"
    if stat -f '%Sf' "$dstdir/$file" | grep -q compressed; then
        echo "File remains compressed: $file" >&2; exit 1
    fi
done
sync
echo 'Local uncompressed staging image prepared. No phone changes were made.'
