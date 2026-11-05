#!/bin/bash
# Read the guarded UFS tail log from a recovery with root ADB access.
set -euo pipefail
serial="${1:?usage: pull-lisa-log.sh SERIAL OUTPUT.txt}"
out="${2:?usage: pull-lisa-log.sh SERIAL OUTPUT.txt}"
root="$(cd "$(dirname "$0")/.." && pwd)"
dev="$(adb -s "$serial" shell 'for d in /dev/block/by-name/userdata /dev/block/bootdevice/by-name/userdata; do if [ -e "$d" ]; then readlink -f "$d"; break; fi; done' | tr -d '\r')"
[[ "$dev" == /dev/block/* && "$dev" != *[!a-zA-Z0-9/_-]* ]] || { echo 'userdata device unavailable' >&2; exit 1; }
bytes="$(adb -s "$serial" shell blockdev --getsize64 "$dev" | tr -d '\r')"
[[ "$bytes" =~ ^[0-9]+$ ]] && (( bytes % 4096 == 0 && bytes / 4096 > 1025 )) || { echo 'invalid userdata size' >&2; exit 1; }
skip=$((bytes / 4096 - 1025))
adb -s "$serial" exec-out "dd if=$dev bs=4096 skip=$skip count=1025 2>/dev/null" > "$out.bin"
[[ "$(wc -c < "$out.bin" | tr -d ' ')" == 4198400 ]] || { echo 'short log read' >&2; exit 1; }
python3 "$root/ios-sweet/tools/sweet-log.py" "$out.bin" "$out"
