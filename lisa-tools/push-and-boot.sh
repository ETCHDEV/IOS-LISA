#!/bin/bash
# Build the GL/Metal userland, push only the changed files to the phone's iOS root over USB mass
# storage (ums-userdata.sh: no userdata flash), boot a kernel image, and pull the log at recovery.
#   KERNEL=lisa-ios-gl-v41m.img LOG=name bash push-and-boot.sh liblisair.dylib LisaMetal ...
set -e
T="$(cd "$(dirname "$0")" && pwd)"
S="${LISA_SERIAL:?set LISA_SERIAL to the fastboot serial of your phone}"
ERR="$(bash "$T/lisa-gl/build.sh" 2>&1 | grep -E ": (fatal )?error:|ninja: build stopped" | head -5 || true)"
if [ -n "$ERR" ]; then echo "$ERR"; echo "BUILD FAILED: nothing pushed"; exit 1; fi
for f in "$@"; do ls -la "$T/lisa-gl/out/$f"; done
MODE=push MEDIA_RW=1 bash "$T/ums-userdata.sh" "$@"
adb -s $S reboot bootloader
sleep 12
for i in $(seq 1 600); do P="$(timeout 5 fastboot -s $S getvar product 2>&1 | awk '/^product:/{print $2}')"; [ "$P" = lisa ] && break; sleep 3; done
echo "product=$P"; [ "$P" = lisa ]
fastboot -s $S boot "$T/diagnostic/out-ufs-xnu/${KERNEL:-lisa-ios-gl-v41m.img}"
echo BOOTED
sleep 60
for i in $(seq 1 2400); do adb devices | grep -q "$S.*recovery" && break; sleep 3; done
sleep 8; bash "$T/pull-lisa-log.sh" $S "$T/diagnostic/${LOG:-gl-latest}-recovery.log" && ls -la "$T/diagnostic/${LOG:-gl-latest}-recovery.log"
# crash frames in our libraries, symbolized against the unstripped builds
LOGF="$T/diagnostic/${LOG:-gl-latest}-recovery.log"
B="$T/research-hackios-userland/xnu-deps/build/mesa/build-softpipe-freedreno/src/gallium/drivers/freedreno/libredwoodair.dylib"
grep -a -o "liblisair.dylib+0x[0-9a-f]*" "$LOGF" 2>/dev/null | sort -u | head -40 | while read f; do
  echo "$f -> $(atos -o "$B" -arch arm64 -l 0x0 "${f#liblisair.dylib+}" 2>/dev/null)"
done
