#!/bin/bash
# Mount the master iOS stage read-only at ios-artifacts/inspect-root so dyld-symbols/ links resolve.
T="$(cd "$(dirname "$0")" && pwd)"
[ -f "$T/ios-artifacts/inspect-root/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.01" ] && exit 0
hdiutil attach -readonly -nobrowse -imagekey diskimage-class=CRawDiskImage -mountpoint "$T/ios-artifacts/inspect-root" "$T/ios-artifacts/lisa-ios-uncompressed-metal-stage.dmg" >/dev/null
