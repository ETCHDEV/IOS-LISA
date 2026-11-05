# iOS 26 Lisa continuation — 2026-11-04

## User constraints

- **Never flash a phone partition.** Use only `fastboot boot <image>`.
- Preserve the installed Lineage recovery.
- Device serial: `<device-serial>`.
- Goal is iOS 26 reaching the lock/home screen, then real touch and GPU acceleration.

## Current state at handoff

The phone was rebooted from Lineage recovery to fastboot after building the image below. After the usual physical USB reconnect, the image was successfully sent and RAM-booted (`Sending OKAY`, `Booting OKAY`, total 1.177 s). Its resulting screen state is not yet known at the time this note was finalized.

RAM-boot candidate:

`lisa-tools/ios26/lisa/out/lisa26-lsfix.img`

- size: 42,332,160 bytes
- APFS emitter output SHA-256: `dd7c9bd7fb7d9cbc25b0ab1758226aa617ba16e28762d41e6d47ea3b0d353230`
- fresh xnu-12377 kernel, 8-core ADT, APFS/KDK closure accepted
- boot arg includes `native-cpumon-override=1`
- no `native-springboard-stack` sampler and framebuffer console stops after 8 seconds

To repeat this temporary boot if needed:

```sh
fastboot devices -l
fastboot -s <device-serial> boot lisa-tools/ios26/lisa/out/lisa26-lsfix.img
```

Do not use `fastboot flash`, `erase`, `format`, dtbo/vbmeta writes, or userdata writes.

## What was fixed in this continuation

1. `CONFIG_VNODES` in the Lisa BCM2837 machine profile was increased from the xsmall ceiling of 1,024 to the normal medium ceiling of 263,168. The device tree had requested memory-sized vnode capacity, but XNU clipped it at build time while `lsd` indexed 207 system apps.

   Source: `lisa-tools/research-UniXNU-ios26/patches/xnu-12377/00-bcm2837-machine-config.patch`

2. Added an explicit, boot-arg-gated compatibility path allowing stock `lsd` to disable its per-thread CPU monitor during its first LaunchServices store build. Previously every request was rejected because the incomplete non-Apple AMFI bridge did not carry `com.apple.private.kernel.override-cpumon`; XNU reset the monitor to defaults each time.

   Source: `lisa-tools/research-UniXNU-ios26/patches/xnu-12377/native-cpumon-override.patch`

3. Corrected the explanatory vnode comment in:

   `lisa-tools/ios26/lisa/make-lisa-adt.py`

4. The intermediate `lisa26-vnode-fix.img` booted without a panic or fastboot fallback but remained at the Apple logo after two minutes. It contained the vnode fix but not the CPU-monitor entitlement fix. Do not repeat it.

## Proven boot blockers before this image

- SpringBoard first blocked in `BKSDisplayGetMainScreenInfo`.
- backboardd was stuck in `CAWindowServer::_detectDisplays -> AccelServer::renderer -> CAMetalContextCreate`, polling for `IOAcceleratorES`.
- `native-ios-class-aliases.patch` was changed so `IOAcceleratorES` resolves through `IOMobileFramebufferLegacy`, permitting CPU framebuffer fallback.
- The CPU fallback accepted/presented three swaps and SpringBoard advanced from 6 to 10 threads.
- SpringBoard then synchronously waited in `_LSCopyServerStore` / `LSApplicationRecord`.
- `lsd` initially waited in `ISIconManager`, then traversed application directories and parsed Info.plists. The old kernel printed hundreds of `denied attempt to escape CPU monitor` messages.

Relevant logs:

- `lisa-tools/ios26/lisa/logs/lisa26-cpu-fallback.log`
- `lisa-tools/ios26/lisa/logs/lisa26-lsd-stack.log`
- `lisa-tools/ios26/lisa/logs/lisa26-backboard-stack.log`

## GPU and touch reality

Do not claim Metal or touch is working yet.

- The iOS 26 Data seed's `NativeAdrenoMetal.bundle` contains only a 449-byte placeholder `Info.plist`; there is no executable.
- The current Lisa xnu-12377 build does not link a usable Lisa Adreno user client. `kits/zeta/LisaAdrenoPort.c` explicitly prints `user client not linked yet`.
- UniXNU's included `NativeAdreno` driver is the proven Adreno 740/sheng implementation. Lisa is SM8350/Adreno 660; enabling `XNU_NATIVE_ADRENO=1` blindly is not a valid port.
- `zeta_touch_probe()` in `kits/zeta/CpuFreqProbe.cpp` is an empty Lisa stub. `TouchProbe.cpp` is not the Goodix Lisa hardware driver.
- Older `lisa-ios-gl-v20` through `v52ma` artifacts contain a real Lisa A660 GLES path and older Goodix work, but they are iOS 16-era kernels expecting a separately flashed 24 GiB system. They cannot simply replace the iOS 26 kernel.
- Historical evidence says the older path achieved Adreno GLES draws/presents and hardware buttons/touch work, while full compositor Metal remained unfinished. See `lisa-tools/UI-FOLLOWUP-2026-10-26.md` and `lisa-tools/METAL-STATUS-2026-10-26.md`.

The correct hardware follow-up is to port the older Lisa A660 service/user client and Goodix transport into xnu-12377, then build an actual signed iOS 26 Metal plugin. Do not map a placeholder class and call that acceleration.

## If `lisa26-lsfix.img` still shows only the Apple logo

Return to Lineage recovery without flashing and pull the persistent log. Determine whether SpringBoard has advanced beyond `_LSCopyServerStore`; do not assume the same block. The previous 20-minute quiet run did not produce a fresh flushed log, so its final wait state was never established.

Build commands used:

```sh
bash lisa-tools/ios26/lisa/build-kernel.sh lisa26-lsfix-kernel '<args>'
bash lisa-tools/ios26/lisa/build-apfs.sh lisa26-lsfix '<args including native-cpumon-override=1>'
```

The kernel build requires normal macOS `sysctl` access outside a restricted sandbox.
