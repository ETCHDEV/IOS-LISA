#!/bin/bash
# Work on a local copy only; never accesses the phone.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Administrator privileges required' >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd -P)
src="$here/ios-artifacts/lisa-ios-os-only.dmg"
out="$here/ios-artifacts/lisa-ios-overlay-stage.dmg"
[[ -f "$src" && ! -L "$src" && ! -e "$out" && ! -L "$out" ]] || { echo 'Missing source or destination already exists' >&2; exit 1; }
cp -c "$src" "$out"
work=$(mktemp -d /private/tmp/lisa-overlay.XXXXXXXX)
attached=0
cleanup() {
    if ((attached)); then hdiutil detach "$work" || echo "Could not detach $work" >&2; fi
    rmdir "$work" 2>/dev/null || true
}
trap cleanup EXIT
hdiutil attach -nobrowse -owners on -mountpoint "$work" "$out"
attached=1
/usr/bin/python3 "$here/finalize-ios-overlay.py" "$work"
echo "Local overlay image: $out"
