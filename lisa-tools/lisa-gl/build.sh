#!/bin/bash
# Build the Lisa OpenGL ES compositor path on the Mac:
#   out/libOSMesa.8.dylib  Mesa (freedreno over the LisaGPU shim), relinked from the existing build tree
#   out/liblisagl.dylib    upstream HakiOS eagl.c + gl_osmesa.c (EAGLContext replacement)
#   out/LisaMetal          the Metal plugin, with GL mode (boot-arg lisagl=1)
# Install paths on the phone: /usr/local/lib/lisa/{libOSMesa.8.dylib,liblisagl.dylib},
# /System/Library/Extensions/LisaMetal.bundle/LisaMetal
set -euo pipefail
T="$(cd "$(dirname "$0")/.." && pwd)"           # lisa-tools
H="$T/research-hackios-userland"
source "$H/xnu-env.sh"
export PATH="/opt/homebrew/opt/bison/bin:/opt/homebrew/opt/flex/bin:$PATH"
SDK="$H/sdks/iPhoneOS16.5.sdk"
[ -d "$SDK" ] || SDK="$T/theos-sdks/iPhoneOS16.5.sdk"
CC="xnu-clang -arch arm64 -isysroot $SDK -O2"
bash "$T/build-lisa-air.sh"
M="$H/xnu-deps/build/mesa"
O="$T/lisa-gl/out"; mkdir -p "$O" "$T/lisa-gl/obj"
echo "== relink OSMesa (shim service name must be LisaGPU)"
grep -q '"LisaGPU"' "$M/mesa-25.0.7/src/redwood-drm/redwood_drm.c"
ninja -C "$M/build-softpipe-freedreno" src/gallium/targets/osmesa/libOSMesa.8.dylib 2>&1 | tail -5
cp "$M/build-softpipe-freedreno/src/gallium/targets/osmesa/libOSMesa.8.dylib" "$O/libOSMesa.8.dylib"
install_name_tool -id /usr/local/lib/lisa/libOSMesa.8.dylib "$O/libOSMesa.8.dylib" 2>&1 | grep -v 'invalidate the code signature' || true
[ "$(strings -a "$O/libOSMesa.8.dylib" | grep -cx LisaGPU)" -ge 1 ] || { echo "libOSMesa does not name LisaGPU"; exit 1; }
GLXML="$M/out/gl.xml"; cp "$M/out/exported-gl.txt" "$T/lisa-gl/obj/exported-gl.txt"
echo "== liblisagl"
python3 "$T/lisa-gl/mkeagl.py" "$T/lisa-gl/src/eagl_slots.txt" "$GLXML" "$T/lisa-gl/obj/exported-gl.txt" \
	"$T/lisa-gl/src/eagl.c" > "$T/lisa-gl/obj/eagl_gen.h"
$CC -dynamiclib -install_name /usr/local/lib/lisa/liblisagl.dylib -I"$T/lisa-gl/obj" -framework CoreFoundation \
	-framework IOKit -framework IOSurface -lobjc "$O/libOSMesa.8.dylib" -o "$O/liblisagl.dylib" \
	"$T/lisa-gl/src/eagl.c" "$T/lisa-gl/src/gl_osmesa.c" > "$T/lisa-gl/obj/gl-build.log" 2>&1 || {
	cat "$T/lisa-gl/obj/gl-build.log"; exit 1;
}
test -f "$O/liblisagl.dylib"
echo "== LisaMetal plugin with GL mode"
P="$T/lisa-metal-userland"
$CC -bundle -framework Foundation -framework IOKit -framework Metal -lobjc \
	-o "$O/LisaMetal" "$P/src/lisametal.m" "$P/src/lisadrawable.m" > "$T/lisa-gl/obj/metal-build.log" 2>&1 || {
	cat "$T/lisa-gl/obj/metal-build.log"; exit 1;
}
test -f "$O/LisaMetal"
$CC -dynamiclib -install_name /usr/local/lib/lisa/liblisaexceptions.dylib \
	-framework Foundation -lobjc \
	-o "$O/liblisaexceptions.dylib" "$T/lisa-gl/src/exceptions.m"
$CC -dynamiclib -install_name /usr/local/lib/lisa/liblisaposter.dylib \
	-framework Foundation -framework UIKit -framework QuartzCore -lobjc \
	-o "$O/liblisaposter.dylib" "$T/lisa-gl/src/poster.m"
$CC -dynamiclib -install_name /usr/local/lib/lisa/liblisabrightness.dylib \
	-framework Foundation -lobjc -o "$O/liblisabrightness.dylib" "$T/lisa-gl/src/brightness.m"
$CC -dynamiclib -install_name /usr/local/lib/lisa/liblisapreferences.dylib \
	-framework Foundation -framework MediaPlayer -lobjc \
	-o "$O/liblisapreferences.dylib" "$T/lisa-gl/src/preferences.m"
$CC -framework Foundation -framework Metal -o "$O/lisa-metal-smoke" \
	"$T/lisa-metal-userland/src/metal-smoke.m"
for f in "$O"/*; do ldid -S "$f" 2>/dev/null || codesign -f -s - "$f" 2>/dev/null || true; done
otool -L "$O/liblisagl.dylib" | head -12
ls -la "$O"; shasum -a 256 "$O"/*
