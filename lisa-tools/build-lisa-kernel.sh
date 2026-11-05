#!/bin/bash
# Lisa's Darwin build entry point. Never call lisa-resume-build.sh from the repository root.
set -euo pipefail
T="$(cd "$(dirname "$0")" && pwd)"
B="$T/darwin-xnu-build"
D="$B/build/xnu.obj/DEVELOPMENT_ARM64_SWEET/bsd/DEVELOPMENT"
if [ -d "$D" ]; then
    bash "$T/build-lisa-wlfw.sh"
fi
if [ "${LISA_WIFI_DRIVER:-0}" = 1 ]; then
    LX="$T/research-hackios-kernel/xnu-deps/build/ath11k/out/ath11k.o"
    test -s "$LX" || { echo "Build ath11k first: bash lisa-tools/build-lisa-ath11k.sh" >&2; exit 1; }
    export LISA_OBJECTS="$LX"
fi
cd "$B"
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
export MACOS_VERSION=13.5 KERNEL_CONFIG=DEVELOPMENT MACHINE_CONFIG=SWEET LISA_FORCE_KERNEL_BUILD=1
export KDKROOT="$T/kdk-expanded/KDK.pkg/Payload"
exec bash lisa-resume-build.sh "$@"
