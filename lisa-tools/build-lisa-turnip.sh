#!/bin/bash
# Turnip (Mesa's Vulkan driver for Adreno) for iOS 16, msm kernel interface through redwood-drm,
# in its own build directory beside the Gallium one. 2026-10-31.
set -uo pipefail
T="$(cd "$(dirname "$0")" && pwd)"
H="$T/research-hackios-userland"
source "$H/xnu-env.sh"
M="$H/xnu-deps/build/mesa"
B="$M/build-turnip"
cd "$M"
source "$M/venv/bin/activate"
export PATH="$VIRTUAL_ENV/bin:/opt/homebrew/opt/bison/bin:$PATH:/opt/homebrew/bin"
if [ ! -f "$B/build.ninja" ]; then
  meson setup "$B" mesa-25.0.7 --cross-file cross.ini -Dwrap_mode=nofallback -Dprefix=/redwood -Dlibdir=lib \
    -Dbuildtype=release -Db_ndebug=true -Dplatforms= -Dgallium-drivers= -Dvulkan-drivers=freedreno \
    -Dfreedreno-kmds=msm -Dllvm=disabled -Dshader-cache=disabled -Dexpat=disabled -Dzstd=disabled \
    -Dlibunwind=disabled -Dlmsensors=disabled -Dvalgrind=disabled -Dbuild-tests=false -Dvideo-codecs= \
    -Dvulkan-layers= -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Degl=disabled \
    -Dgbm=disabled -Dshared-glapi=disabled -Dxmlconfig=disabled -Dtools= 2>&1 | tail -40 || exit 1
fi
ninja -C "$B" -k 0 src/freedreno/vulkan/libvulkan_freedreno.dylib 2>&1 | grep -E "error|FAILED:|ninja: build stopped|Linking|Undefined|_[a-z_]*, referenced|duplicate symbol|  \"_" | head -60
ls -la "$B/src/freedreno/vulkan/libvulkan_freedreno.dylib" 2>/dev/null && echo TURNIP-BUILT
