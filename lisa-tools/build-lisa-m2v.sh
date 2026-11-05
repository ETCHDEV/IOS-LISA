#!/bin/bash
# The Metal-on-Vulkan bridge, as UniXNU builds it (scripts/build-adreno-metal-bridge.py): metal2vulkan at
# their pinned commit plus their 16 patches, behind their bridge's C ABI (bridge/NativeMetal*.rs), as a
# static library for arm64 iOS 16 (theirs is iOS 17). 2026-11-03.
set -euo pipefail
T="$(cd "$(dirname "$0")" && pwd)"
export PATH=/opt/homebrew/opt/rustup/bin:$HOME/.cargo/bin:/opt/homebrew/bin:$PATH
S="$T/research-metal2vulkan"
A="$T/research-UniXNU-AdrenoMetal/bridge"
O="$T/lisa-vulkan/m2v-bridge"
mkdir -p "$O"
PIN=720ef1ce9ae2a2068999a8675717418b9416283e
[ "$(git -C "$S" rev-parse HEAD)" = "$PIN" ] || { echo "metal2vulkan not at pin"; exit 1; }
if [ ! -f "$O/patched" ]; then
  git -C "$S" checkout -q -- . 
  for p in 0001-optional-constant-output-members 0002-resource-wait-storage-dependencies 0003-cache-device-entry-point-priming \
           0004-fold-constant-global-field-loads 0005-keep-module-decodes-and-profile-fingerprints 0006-skip-draws-without-a-pipeline \
           0007-accept-two-plane-rgb-surfaces 0008-alias-padded-rows-of-written-textures 0009-keep-final-pass \
           0010-fewer-allocations-per-draw 0011-keep-draw-commands 0012-keep-draw-lists 0013-skip-unchanged-rebinds \
           0014-map-tracker-keys 0015-two-plane-shadow-as-memory-lies 0016-tile-stores-within-rows; do
    git -C "$S" apply "$A/patches/$p.patch" && echo "patch $p"
  done
  touch "$O/patched"
fi
LLVM_AS=/opt/homebrew/opt/llvm/bin/llvm-as
$LLVM_AS -o "$O/runtime_examples.air" "$S/crates/metal2vulkan-validation/fixtures/public/examples/runtime_examples.ll"
$LLVM_AS -o "$O/function_constant.air" "$S/crates/metal2vulkan-validation/fixtures/public/kernel_function_constant_gate_fold.ll"
cat > "$O/Cargo.toml" <<TOML
[package]
name = "adreno-metal-bridge"
version = "0.1.0"
edition = "2024"
rust-version = "1.98"
[workspace]
[lib]
crate-type = ["staticlib"]
path = "$A/NativeMetalBridge.rs"
[dependencies]
metal2vulkan = { path = "$S/crates/metal2vulkan", features = ["ash"] }
metal2vulkan-runtime = { path = "$S/crates/metal2vulkan-runtime" }
libc = "0.2"
sha2 = { version = "0.10" }
[profile.release]
panic = "unwind"
debug = 1
TOML
export NMB_RUNTIME_ID="lisa-$(printf %s "$PIN" | cut -c1-12)-16p"
export NMB_FIXTURE_AIR="$O/runtime_examples.air" NMB_FC_AIR="$O/function_constant.air" IPHONEOS_DEPLOYMENT_TARGET=16.0
cargo +1.98.0 build --release --manifest-path "$O/Cargo.toml" --target aarch64-apple-ios --target-dir "$O/target" 2>&1 | grep -E "^(error|warning: unused)|Compiling|Finished|could not" | tail -40
ls -la "$O/target/aarch64-apple-ios/release/libadreno_metal_bridge.a" && echo M2V-BUILT
