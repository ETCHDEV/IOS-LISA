#!/bin/bash
# Build upstream Linux ath11k + the existing XNU LinuxKPI glue on this Mac.
# This does not enable Wi-Fi on the phone, flash it, or claim all driver paths work.
set -euo pipefail
T="$(cd "$(dirname "$0")" && pwd)"
R="$T/research-hackios-kernel"
export PATH="/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/coreutils/libexec/gnubin:$PATH"
export LISA_LX_TARGET=arm64-apple-macos13.5
export XNU_SRC="$T/darwin-xnu-build/xnu"
APPLE_LD="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/ld"
if [ ! -e "$R/xnu-deps/host/bin/ld64" ] || [ "$(readlink "$R/xnu-deps/host/bin/ld64")" = /usr/bin/ld ]; then
    ln -sf "$APPLE_LD" "$R/xnu-deps/host/bin/ld64"
fi
bash "$R/tools/mk-ath11k.sh" build
printf '\nMissing ath11k functions (must be inspected before enabling association):\n'
cat "$R/xnu-deps/build/ath11k/out/stubs.txt"
