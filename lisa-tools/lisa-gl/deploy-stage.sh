#!/bin/bash
# Install the GL path into the full iOS HFS stage image and make a verified Android sparse image.
# Does not touch the phone.
set -euo pipefail
T="$(cd "$(dirname "$0")/.." && pwd)"
A="$T/ios-artifacts"
DMG="$A/lisa-ios-uncompressed-metal-stage.dmg"
OUT="$A/lisa-ios-gl-${GLVER:-v1}.simg"
O="$T/lisa-gl/out"
for f in libOSMesa.8.dylib liblisagl.dylib liblisaexceptions.dylib liblisaposter.dylib liblisabrightness.dylib liblisapreferences.dylib liblisair.dylib lisa-metal-smoke LisaMetal; do test -s "$O/$f"; done
df -g "$A" | tail -1
[ -e "$OUT" ] && { echo "$OUT exists; remove it first"; exit 1; }
hdiutil detach -force "$T/ios-artifacts/inspect-root" >/dev/null 2>&1 || true
MNT="$(mktemp -d /tmp/lisa-gl-mnt.XXXX)"
hdiutil attach -nobrowse -imagekey diskimage-class=CRawDiskImage -mountpoint "$MNT" "$DMG"
trap 'hdiutil detach "$MNT" >/dev/null 2>&1 || true' EXIT
test -f "$MNT/System/Library/CoreServices/SystemVersion.plist"
ls -la "$MNT/usr/local/lib/lisa" "$MNT/System/Library/Extensions/LisaMetal.bundle"
cp "$O/libOSMesa.8.dylib" "$O/liblisagl.dylib" "$O/liblisaexceptions.dylib" "$O/liblisaposter.dylib" "$O/liblisabrightness.dylib" "$O/liblisapreferences.dylib" "$MNT/usr/local/lib/lisa/"
cp "$MNT/System/Library/Extensions/LisaMetal.bundle/LisaMetal" "$A/LisaMetal.before-gl-v1"
cp "$O/LisaMetal" "$MNT/System/Library/Extensions/LisaMetal.bundle/LisaMetal"
cp "$O/liblisair.dylib" "$MNT/usr/local/lib/lisa/liblisair.dylib"
mkdir -p "$MNT/usr/local/bin"
cp "$O/lisa-metal-smoke" "$MNT/usr/local/bin/lisa-metal-smoke"
chmod 755 "$MNT/usr/local/bin/lisa-metal-smoke" "$MNT/usr/local/lib/lisa/liblisair.dylib"
cmp "$O/liblisair.dylib" "$MNT/usr/local/lib/lisa/liblisair.dylib"
cmp "$O/lisa-metal-smoke" "$MNT/usr/local/bin/lisa-metal-smoke"
python3 - "$MNT" <<'PYMETAL'
from pathlib import Path
import plistlib, sys
root = Path(sys.argv[1])
job = {'Label': 'dev.lisa.metal-smoke', 'ProgramArguments': ['/usr/local/bin/lisa-metal-smoke'],
       'EnvironmentVariables': {'LISA_NATIVE_METAL': '1'}, 'RunAtLoad': True,
       'JetsamProperties': {'JetsamMemoryLimit': 512},
       'StandardOutPath': '/dev/console', 'StandardErrorPath': '/dev/console'}
(root/'System/Library/LaunchDaemons/dev.lisa.metal-smoke.plist').write_bytes(plistlib.dumps(job))
# Keep GUI on the working GLES backend until the native test produces valid pixels.
# A later explicit per-process entry can opt an application in independently.
(root/'usr/local/lib/lisa/native-metal-processes').write_text('lisa-metal-smoke\n')
# The stock Daemon default is only6MB; Foundation/AIR compilation is killed
# before the test reaches its first device query. Scope the override to this job.
for path in (root/'System/Library/LaunchDaemons').glob('com.apple.jetsamproperties.D22*.plist'):
    profile = plistlib.loads(path.read_bytes())
    profile['Version4']['Daemon'].setdefault('Override', {})['dev.lisa.metal-smoke'] = {
        'ActiveHardMemoryLimit': 512, 'InactiveHardMemoryLimit': 512,
        'ActiveSoftMemoryLimit': 512}
    path.write_bytes(plistlib.dumps(profile, fmt=plistlib.FMT_BINARY))
PYMETAL
chmod 755 "$MNT/usr/local/lib/lisa/libOSMesa.8.dylib" "$MNT/usr/local/lib/lisa/liblisagl.dylib" \
	"$MNT/usr/local/lib/lisa/liblisaexceptions.dylib" "$MNT/usr/local/lib/lisa/liblisaposter.dylib" "$MNT/usr/local/lib/lisa/liblisabrightness.dylib" "$MNT/usr/local/lib/lisa/liblisapreferences.dylib" \
	"$MNT/System/Library/Extensions/LisaMetal.bundle/LisaMetal"
for f in usr/local/lib/lisa/libOSMesa.8.dylib usr/local/lib/lisa/liblisagl.dylib usr/local/lib/lisa/liblisaexceptions.dylib usr/local/lib/lisa/liblisaposter.dylib usr/local/lib/lisa/liblisabrightness.dylib usr/local/lib/lisa/liblisapreferences.dylib System/Library/Extensions/LisaMetal.bundle/LisaMetal; do
	cmp "$MNT/$f" "$O/$(basename $f)" && echo "installed $f"
done
# The phone's own Wi-Fi firmware (from its modem_a partition, lisa-firmware/wifi) for the kernel's
# Wi-Fi port (bsd/dev/lisa_wifi_glue.c reads /usr/local/lib/lisa/fw/).
if [ -d "$T/lisa-firmware/wifi" ]; then
	mkdir -p "$MNT/usr/local/lib/lisa/fw"
	cp "$T/lisa-firmware/wifi/"* "$MNT/usr/local/lib/lisa/fw/"
	chmod 644 "$MNT/usr/local/lib/lisa/fw/"*
	ls "$MNT/usr/local/lib/lisa/fw" | tr '\n' ' '; echo
fi
# ADSP remote processor image for the read-only PMIC GLINK battery service.
if [ -d "$T/lisa-firmware/adsp" ]; then
    mkdir -p "$MNT/usr/local/lib/lisa/fw"
    for f in "$T/lisa-firmware/adsp/"adsp.mdt "$T/lisa-firmware/adsp/"adsp.b[0-9][0-9]; do
        [ -f "$f" ] || continue
        cp "$f" "$MNT/usr/local/lib/lisa/fw/"
        chmod 644 "$MNT/usr/local/lib/lisa/fw/$(basename "$f")"
    done
fi
# Visual Voicemail burns CPU while this port has no modem/CommCenter backend.
# Reversible, scoped workaround: keep Phone/CommCenter and all other services intact.
python3 - "$MNT" "$A" <<'PYVMD'
from pathlib import Path
import plistlib, shutil, sys
root, artifacts = map(Path, sys.argv[1:])
p = root/'System/Library/LaunchDaemons/com.apple.voicemail.vmd.plist'
backup = artifacts/'com.apple.voicemail.vmd.before-v14.plist'
if p.exists():
    if not backup.exists(): shutil.copy2(p, backup)
    obj = plistlib.loads(p.read_bytes())
    obj['Disabled'] = True
    p.write_bytes(plistlib.dumps(obj, fmt=plistlib.FMT_BINARY))
    print('Temporarily disabled unavailable Visual Voicemail; original plist saved:', backup)
PYVMD
# Lisa publishes IO80211Controller/en0 after its own ath11k bring-up. The stock
# job's Apple-only DT gate otherwise prevents wifid from launching at all.
python3 - "$MNT" "$A" <<'PYWIFI'
from pathlib import Path
import plistlib, shutil, sys
root, artifacts = map(Path, sys.argv[1:])
p = root/'System/Library/LaunchDaemons/com.apple.wifid.plist'
obj = plistlib.loads(p.read_bytes())
backup = artifacts/'com.apple.wifid.before-v15.plist'
if not backup.exists(): shutil.copy2(p, backup)
gate = obj.pop('_LimitLoadToDeviceTree', None)
obj['Disabled'] = False
p.write_bytes(plistlib.dumps(obj, fmt=plistlib.FMT_BINARY))
assert '_LimitLoadToDeviceTree' not in plistlib.loads(p.read_bytes())
print('Enabled stock wifid for Lisa IO80211/en0; removed Apple DT gate:', gate)
PYWIFI
python3 "$T/lisa-gl/repair-app-layout.py" "$MNT" --apply > "$T/diagnostic/gl-${GLVER:-v1}-app-layout-applied.json"
python3 "$T/lisa-gl/link-compat.py" "$MNT" "$A/v8-executable-backup" > "$T/diagnostic/gl-${GLVER:-v1}-compat-linked.json"
python3 "$T/lisa-gl/sync-launchd-jobs.py" "$MNT"
sync; hdiutil detach "$MNT"; trap - EXIT
DEV="$(hdiutil attach -nomount -readonly -imagekey diskimage-class=CRawDiskImage "$DMG" | tail -1 | awk '{print $1}')"
fsck_hfs -n "$DEV" 2>&1 | tail -4 || true
hdiutil detach "$DEV"
python3 "$T/make-android-sparse.py" "$DMG" "$OUT"
python3 "$T/verify-android-sparse.py" "$OUT"
