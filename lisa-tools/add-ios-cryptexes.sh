#!/bin/bash
# Add the two verified restore cryptexes to the OS-only staging image.
# This does not make the result bootable on Lisa.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'Run with administrator privileges' >&2; exit 1; }
here=$(cd "$(dirname "$0")" && pwd -P)
a="$here/ios-artifacts"
root_image="$a/lisa-ios-os-only.dmg"
sys_image="$a/087-86593-047.dmg"
app_image="$a/087-86715-047.dmg"
for p in "$root_image" "$sys_image" "$app_image"; do
    [[ -f "$p" && ! -L "$p" ]] || { echo "Missing or unsafe file: $p" >&2; exit 1; }
done
work=$(mktemp -d /private/tmp/lisa-cryptex.XXXXXXXX)
mkdir "$work/root" "$work/system" "$work/app"
attached=()
cleanup() {
    local i
    for ((i=${#attached[@]}-1; i>=0; i--)); do
        hdiutil detach "${attached[$i]}" || echo "Could not detach ${attached[$i]}" >&2
    done
    rmdir "$work/root" "$work/system" "$work/app" "$work" 2>/dev/null || true
}
trap cleanup EXIT
hdiutil attach -nobrowse -owners on -mountpoint "$work/root" "$root_image"
attached+=("$work/root")
hdiutil attach -readonly -nobrowse -owners on -mountpoint "$work/system" "$sys_image"
attached+=("$work/system")
hdiutil attach -readonly -nobrowse -owners on -mountpoint "$work/app" "$app_image"
attached+=("$work/app")
[[ -d "$work/root/System/Library" && -d "$work/system/System" && -d "$work/app/System" ]] || { echo 'Unexpected filesystem layout' >&2; exit 1; }
dst="$work/root"
for part in private preboot Cryptexes; do
    dst="$dst/$part"
    [[ ! -L "$dst" ]] || { echo "Refusing symlink destination: $dst" >&2; exit 1; }
    if [[ ! -e "$dst" ]]; then mkdir "$dst"; fi
    [[ -d "$dst" ]] || { echo "Not a directory: $dst" >&2; exit 1; }
done
for name in OS App; do
    [[ ! -e "$dst/$name" && ! -L "$dst/$name" ]] || { echo "Cryptex destination exists: $name" >&2; exit 1; }
done
mkdir "$dst/OS" "$dst/App"
rsync -aH --numeric-ids "$work/system/" "$dst/OS/"
rsync -aH --numeric-ids "$work/app/" "$dst/App/"
echo 'Cryptex copies completed. This image still requires overlay links, root tweaks, and a Lisa UFS driver.'
