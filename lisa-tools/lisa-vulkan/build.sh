#!/bin/bash
# Turnip for the phone, and its first test: out/libvulkan_freedreno.dylib, out/lisa-turnip-smoke.
set -euo pipefail
T="$(cd "$(dirname "$0")/.." && pwd)"
H="$T/research-hackios-userland"
source "$H/xnu-env.sh"
SDK="$H/sdks/iPhoneOS16.5.sdk"
M="$H/xnu-deps/build/mesa"
O="$T/lisa-gl/out"
bash "$T/build-lisa-turnip.sh"
cp "$M/build-turnip/src/freedreno/vulkan/libvulkan_freedreno.dylib" "$O/libvulkan_freedreno.dylib"
install_name_tool -id /usr/local/lib/lisa/libvulkan_freedreno.dylib "$O/libvulkan_freedreno.dylib"
xnu-clang -arch arm64 -isysroot "$SDK" -O2 -I"$M/mesa-25.0.7/include" -o "$O/lisa-turnip-smoke" \
	"$T/lisa-vulkan/src/turnip-smoke.c" "$O/libvulkan_freedreno.dylib"
xnu-clang -arch arm64 -isysroot "$SDK" -O2 -dynamiclib -I"$M/mesa-25.0.7/include" \
	-install_name /usr/local/lib/lisa/liblisavk.dylib -o "$O/liblisavk.dylib" \
	"$T/lisa-vulkan/src/lisavk-loader.c" "$O/libvulkan_freedreno.dylib"
B="$T/lisa-vulkan/m2v-bridge/target/aarch64-apple-ios/release/libadreno_metal_bridge.a"
if [ -f "$B" ]; then
  xnu-clang -arch arm64 -isysroot "$SDK" -O2 -I"$T/research-UniXNU-AdrenoMetal/bridge" -o "$O/lisa-m2v-smoke" \
	"$T/lisa-vulkan/src/m2v-smoke.c" "$B" -framework CoreFoundation -framework Security -liconv -lresolv 2>&1 | grep -v "was built for newer" | head -20
fi
for f in "$O/libvulkan_freedreno.dylib" "$O/lisa-turnip-smoke" "$O/liblisavk.dylib" "$O/lisa-m2v-smoke"; do ldid -S "$f" 2>/dev/null || codesign -f -s - "$f"; done
otool -L "$O/lisa-turnip-smoke" | head -5
nm -gU "$O/libvulkan_freedreno.dylib" | grep -c vk_icdGetInstanceProcAddr
ls -la "$O/libvulkan_freedreno.dylib" "$O/lisa-turnip-smoke"
echo VULKAN-BUILT
