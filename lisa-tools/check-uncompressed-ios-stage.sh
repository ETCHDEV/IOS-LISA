#!/bin/bash
set -euo pipefail
image=/Users/arjun/IOS-LISA/lisa-tools/ios-artifacts/lisa-ios-uncompressed-stage.dmg
[[ $(id -u) == 0 && -f "$image" && ! -L "$image" ]] || exit 1
device=$(/usr/bin/hdiutil attach -readonly -nomount "$image" | /usr/bin/awk '/^\/dev\/disk[0-9]+[[:space:]]/ { print $1; exit }')
[[ "$device" =~ ^/dev/disk[0-9]+$ ]] || { echo 'No disk device returned.' >&2; exit 1; }
trap '/usr/bin/hdiutil detach "$device"' EXIT
/sbin/fsck_hfs -fn "$device"
