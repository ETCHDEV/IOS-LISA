#!/bin/bash
# Prepare a local copy; no phone access or flashing.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Administrator privileges required' >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd -P)
src="$here/ios-artifacts/lisa-ios-overlay-stage.dmg"
out="$here/ios-artifacts/lisa-ios-research-stage.dmg"
[[ -f "$src" && ! -L "$src" && ! -e "$out" && ! -L "$out" ]] || { echo 'Missing source or destination already exists' >&2; exit 1; }
cp -c "$src" "$out"
work=$(mktemp -d /private/tmp/lisa-setup.XXXXXXXX)
attached=0
cleanup() {
    if ((attached)); then hdiutil detach "$work" || echo "Could not detach $work" >&2; fi
    rmdir "$work" 2>/dev/null || true
}
trap cleanup EXIT
hdiutil attach -nobrowse -owners on -mountpoint "$work" "$out"
attached=1
/usr/bin/python3 "$here/finalize-ios-setup.py" "$work"
echo "Local research image: $out"
