#!/bin/bash
# Rebuild the current Mesa AIR bridge without unpacking over Lisa's local changes.
set -euo pipefail
T="$(cd "$(dirname "$0")" && pwd)"
H="$T/research-hackios-userland"
source "$H/xnu-env.sh"
M="$H/xnu-deps/build/mesa"
cp "$H/tools/mesa/air/air_metal.c" "$H/tools/mesa/air/air_metal.h" "$M/mesa-25.0.7/src/compiler/air/"
ninja -C "$M/build-softpipe-freedreno" src/gallium/drivers/freedreno/libredwoodair.dylib
mkdir -p "$T/lisa-gl/out"
llvm-strip -x -o "$T/lisa-gl/out/liblisair.dylib" "$M/build-softpipe-freedreno/src/gallium/drivers/freedreno/libredwoodair.dylib"
install_name_tool -id /usr/local/lib/lisa/liblisair.dylib "$T/lisa-gl/out/liblisair.dylib"
exports="$(nm -gU "$T/lisa-gl/out/liblisair.dylib")"
for symbol in rwa_buffer_copy rwa_buffer_fill rwa_texture_copy rwa_render_draw_region rwa_present_panel; do
    printf '%s\n' "$exports" | grep -E "_${symbol}$"
done
