# Lisa Metal/Mesa bridge

This tree is the HackiOS Mesa/Metal userspace bridge mechanically adapted for Lisa's `LisaGPU` IOService. It is intentionally not enabled automatically. The companion kernel publishes `IOAcceleratorES`, but it only accepts `MetalPluginName` and `MetalPluginClassName` while booted with `metal=1`.

## Required build inputs

- iPhoneOS 16.5 SDK or compatible iOS 16 arm64 SDK (`THEOS` pointing to a Theos tree containing `sdks/iPhoneOS16.5.sdk`)
- HackiOS `xnu-env.sh`, iOS rootfs/cryptex extraction, and Mesa dependency tree from the full HackiOS userland checkout
- Mesa freedreno built with the Lisa service ABI and a660 firmware interface

## Installation contract

1. Build `LisaMetal` and the Mesa GLES library with the full userland build.
2. Stage `LisaMetal.bundle` at `/System/Library/Extensions/LisaMetal.bundle` and its dylib dependencies.
3. Boot the GPU-enabled Lisa kernel with `metal=1`.
4. Run the Lisa equivalent of `metal_props` only in the test environment. It sets `MetalPluginName=LisaMetal` and `MetalPluginClassName=LisaMetalDevice` on `IOAcceleratorES`.
5. Verify a standalone GLES/Metal triangle and IOSurface round-trip before allowing backboardd/SpringBoard to discover the bundle.

## Current local workflow (2026-10-27)

The SDK, patched Mesa tree and build dependencies are now available locally.
Run `bash lisa-tools/lisa-gl/build.sh` from the workspace root. It builds the
plugin, matching AIR backend and native Metal test; `build-lisa-air.sh` rebuilds
the existing patched Mesa tree without replacing Claude's frontend changes.

The current system compositor uses the Adreno GLES path with `lisagl=1`.
Native Metal is selected per process using `LISA_NATIVE_METAL=1` or an exact
process name in `/usr/local/lib/lisa/native-metal-processes`. This selection
does not imply complete Metal support. Buffer fill/copy, vertex/fragment AIR,
viewport, scissor and matching uncompressed colour texture-region copies have
built implementations; hardware validation is pending.

The staged `dev.lisa.metal-smoke` launch daemon verifies actual buffer bytes and
shader pixels before allowing AegirPoster to try the native driver on its next
launch. It keeps backboardd/SpringBoard on GLES. Retrieve its `LISA METAL TEST`
results from the guarded UFS log in recovery. See the newest entry in
`HANDOFF-CLAUDE-TO-CODEX.md` for image versions, hashes and remaining limitations.

This iOS image uses `/System/Library/xpc/launchd.plist` as its consolidated job
cache. Editing standalone LaunchDaemons plists alone does not load the changes.
`deploy-stage.sh` runs `sync-launchd-jobs.py` to update only Lisa's explicitly
changed jobs in that cache. The v17 test never launched because this step was
missing; v18 includes it. Missing test output is not a successful or failed GPU test.

The v18 daemon launched but was jetsam-killed (namespace 1/code 7) before opening
the device: stock D22/D221 daemon defaults allow only 6 MB. v19 scopes a 512 MB
allowance to `dev.lisa.metal-smoke` and logs its actual limits before Foundation
initialization. Other process limits and global memory protections remain intact.
