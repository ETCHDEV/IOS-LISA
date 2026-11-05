#!/bin/sh
# Run inside a complete HackiOS userland checkout after setting THEOS.
set -eu
: "${THEOS:?Set THEOS to a tree containing sdks/iPhoneOS16.5.sdk}"
test -d "$THEOS/sdks/iPhoneOS16.5.sdk"
exec xnu-clang -arch arm64 -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" -O1 -bundle \
  -framework Foundation -framework IOKit -framework Metal -lobjc \
  -o LisaMetal src/lisametal.m src/lisadrawable.m
