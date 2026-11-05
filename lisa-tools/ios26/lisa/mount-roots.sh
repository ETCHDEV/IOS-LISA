#!/bin/bash
# Read-only mounts of the raw OS and SystemOS containers at ios26/mnt/{OS,SystemOS}. mount-roots.sh [detach]
set -uo pipefail
L="$(cd "$(dirname "$0")" && pwd)"
R="$(cd "$L/.." && pwd)"
if [ "${1:-}" = detach ]; then
	for n in OS SystemOS; do umount "$R/mnt/$n" 2>/dev/null; done
	for dev in $(hdiutil info | awk '/ios26\/raw\/(OS|SystemOS)\.raw/{f=1} f&&/^\/dev\/disk[0-9]+[ \t]/{print $1; f=0}'); do hdiutil detach "$dev" >/dev/null 2>&1; done
	exit 0
fi
attach() {
	local name=$1 vol=$2 before after s
	mkdir -p "$R/mnt/$name"
	mount | grep -q " $R/mnt/$name " && return 0
	hdiutil attach -nomount -readonly -imagekey diskimage-class=CRawDiskImage "$R/raw/$name.raw" >/dev/null 2>&1
	sleep 3
	s=$(diskutil list 2>/dev/null | awk -v v="$vol" '$0 ~ v {print $NF; exit}')
	[ -n "$s" ] || { echo "no volume $vol"; return 1; }
	mount -t apfs -o rdonly,nobrowse "/dev/$s" "$R/mnt/$name"
}
attach OS LuckF23F84.V54OS
diskutil list 2>/dev/null | grep -q . # settle
hdiutil attach -nomount -readonly -imagekey diskimage-class=CRawDiskImage "$R/raw/SystemOS.raw" >/dev/null 2>&1
sleep 3
s=$(diskutil list 2>/dev/null | awk '/LuckF23F84.V54System/{print $NF; exit}')
mkdir -p "$R/mnt/SystemOS"
# Sealed: mount(8) refuses it, diskutil goes through the root snapshot.
mount | grep -q " $R/mnt/SystemOS " || diskutil mount readOnly nobrowse -mountPoint "$R/mnt/SystemOS" "$s" >/dev/null
mount | grep "$R/mnt/" | cut -c1-110
