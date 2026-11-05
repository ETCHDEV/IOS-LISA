# Lisa iOS 26 port plan (2026-11-03)

Target: iPhone18,2, iOS 26.5.2 (23F84), kernel source xnu-12377.121.6. Phone: Lisa, SM7325, Adreno 642L.
Old GPU path (LisaMetal -> liblisair -> Gallium) is dropped. Metal is rebuilt on the UniXNU design below.

**Route decided by the user, 2026-11-03: follow zeta.** UEFI firmware + DarwinBoot, 4 KiB kernel (BCM2837 config +
UniXNU patches), APFS root with VerifiedRoot, userland rewritten and re-signed for 4 KiB. The "Lisa today" column below
is background only; the milestones here replace those further down: (1) uefi-lisa + UniXNU-lisa device repo,
(2) IPSW identity + ADT, (3) kernel/payload, (4) kernel bring-up, (5) storage + APFS root, (6) userland,
(7) Metal, (8) peripherals.

**Boot change requested by the user, 2026-11-03: no UEFI; boot XNU directly.** Stock Mu-Silicium does run on Lisa (RAM boot
reached "no operating system"; image kept in `lisa-tools/ios26/uefi/` as a fallback), but the port extends the existing
`ios-sweet/shim` instead of DarwinBoot. Still zeta's route for the 4 KiB kernel, APFS root and userland rewrite.
Shim changes needed, from comparing `shim/loader.c` with DarwinBoot: xnu-12377 boot_args (0x480 bytes, 1024-byte command line,
was 736 bytes); 4 KiB kernel layout (loader and `repack-sweet-kernel.py` assume 16 KiB); fill the ADT's ramdisk and trust cache
placeholders; a memory window sized for iOS 26 (zeta's UEFI window was limited, so this may be easier here).
Not yet tested on hardware. Lost compared with zeta: nothing from UEFI is used after handoff except the framebuffer, which
ABL already leaves on (mainline DTS address), and Lisa already has its own recovery log path.

## Status, 2026-11-03 evening
Working on the phone (RAM boot, 1 CPU, 1 GiB window): xnu-12377 to "Still waiting for root device"; framebuffer console; UFS (read-write,
polled, ported `sweet_ufs.c`); console log to the userdata tail; reset at panic and at the root wait.
Loop, no photos: `bash lisa-tools/ios26/lisa/build-kernel.sh NAME` then `bash lisa-tools/ios26/lisa/run.sh NAME` (log in `ios26/lisa/logs/`).
Flashed: userdata holds `ios26/lisa/out/root/userdata-root.img` (SHA-256 3e5f9807..., hash read back from the phone). iOS 16 is NOT on the phone;
restore with `fastboot flash userdata lisa-tools/ios-artifacts/lisa-ios-gl-v41.simg` plus newer files from `lisa-gl/out`.
Lisa patches: `research-UniXNU-ios26/patches/xnu-12377/zzz-lisa-*.patch`. Owner key: `ios26/lisa/.cache/owner-root` (private, local).
Lessons: VA and PA must agree below 2 MiB; BOOTSTRAP_TABLE_SIZE is 8 pages on 4 KiB; RAM does not survive the reset (pstore stayed empty);
`dmg-to-simg.py` leaves zero runs as don't-care, so zero them on the phone or old data stays; fastboot may wedge after `adb reboot bootloader`.
APFS done (kernel lisa26-a4): `bash lisa-tools/ios26/lisa/build-kernel.sh NAME` then `build-apfs.sh NAME2` then `run.sh NAME2` (the a-series images).
UniXNU branch: local `lisa-ios26` on zeta's 35c384e; all Lisa kernel changes in `patches/xnu-12377/zzz-lisa.patch`; NativeUfs reads through sweet_ufs (LISA_UFS_HOST).
On the phone: signed windows verified, APFS 2811.121.1 (KDK, arm64) mounts OS `LuckF23F84.V54OS` read-only, owner root authorizes, then
`Process 1 exec of /sbin/launchd failed, errno 86` (EBADARCH): launchd is arm64e. Same point zeta reached.
Next: userland rewrite to arm64 and 4 KiB (zeta README "Userland rewritten to the ordinary arm64 ABI"); UI; touch; Metal; Wi-Fi/Bluetooth; camera and flash (no driver exists).

## Inputs on disk
- `lisa-tools/ios26/iPhone18,2_26.5.2_23F84_Restore.ipsw`, complete.
- `lisa-tools/ios26/raw/`: decrypted images. OS `094-54256-108.dmg`, SystemOS `094-54345-109.dmg`,
  ExclaveOS `094-54539-112.dmg` (not needed for the port). AppOS `094-54871-112.dmg` is unencrypted, in the IPSW.
- `lisa-tools/research-UniXNU-ios26` (with driver submodules), `research-none24smu-UniXNU-zeta`, `research-xnu-12377`.

## What the survey found
| Topic | zeta (reference, SM8350) | Lisa today | Effect |
|---|---|---|---|
| Boot | UEFI + DarwinBoot | framed SWEETXNU image, `fastboot boot`, works | Keep Lisa's; no UEFI port |
| Kernel pages | 4 KiB, so all userland is re-signed | 16 KiB (`SWEET.h`) | Possibly no 4 KiB rewrite (unproven on iOS 26) |
| Root filesystem | APFS: needs arm64e kext, unfinished in its notes | HFSX in userdata, own `sweet_hfs` | Possibly copy the tree to HFSX, no APFS driver (unproven) |
| Userland ABI | arm64e rewritten to arm64 (`rewrite-zeta-*`) | iOS 16 for A11 was already arm64 | **New work for Lisa: needed, reuse zeta's scripts** |
| CPU features | audited for X1/A78/A55 | A78/A55 | zeta's audit should mostly apply; rerun |
| GPU kernel | NativeAdreno: A660/A740 | own LisaGPU brings up A642L, runs Turnip | Keep Lisa's bring-up; align its interface to what Turnip/NativeMetal need |
| Metal | NativeMetal -> metal2vulkan -> Turnip -> IR3 | LisaMetal -> AIR -> Gallium | **Replace** |

Lisa's kernel delta: xnu-8796.141.3 + 85 changed files (2,034 lines added) + new drivers (`bsd/dev/lisa_*`, LisaGPU,
Sweet* IOKit, `sweet_hfs`, bearssl, pthread). That must move forward about four years of xnu.

## Reimplemented Metal
`QuartzCore -> NativeMetal plugin (MTLAddDevice) -> metal2vulkan -> Turnip -> LisaGPU`, with the composite handed to the
display driver. Proven so far on the phone, iOS 16: Turnip, metal2vulkan compute, render and no-copy import
(gl-v52ma1). QuartzCore translation: 4 tile functions refused as expected; the other 8 fail on the test's
generic descriptors (`VertexAttribute`, `ColorAttachment`), to confirm with real pipeline descriptors.
Known zeta issues to take care of early: render-stage linked functions (black wallpaper), per-client object limits,
GPU address exhaustion, contiguous-allocation stalls.

## Milestones
1. **Offline image study**: mount images read-only, list what iOS 26 needs from the kernel; rerun zeta's CPU and
   arm64e audits on this image.
2. **Kernel**: forward-port SWEET and Lisa drivers to xnu-12377, using UniXNU's `patches/xnu-12377` where they apply.
   Test: `fastboot boot`, reach root mount wait. Touches no storage.
3. **Userland**: rewrite OS + SystemOS to arm64, build the root image, first launchd. Needs a second root on the phone.
4. **Springboard without GPU**: reach the UI with a software framebuffer if possible.
5. **Metal**: NativeMetal + Turnip + LisaGPU composition.
6. **Peripherals**: touch, Wi-Fi, audio; re-check each.

## Risks and limits
- Large and uncertain. zeta's README still describes userland as incomplete; its graphics notes mention a later
  image reaching Home with a black wallpaper and slow UI.
- The two "possibly" rows above are untested on iOS 26 and could turn out wrong: iOS 26 may insist on APFS or cryptex
  behaviour, in which case zeta's APFS route is the fallback.
- Disk: about 36 GB free on the Mac while decrypting. Rewritten images need more; emptying the Trash helps.
- KDK for xnu-12377 is fetched from a third-party GitHub mirror (dortania, SHA-256 pinned in UniXNU). Ask first.
- Xcode here is 27.2; zeta used 26.6, so the kernel may need build fixes.

## Rules kept
`fastboot boot` only for kernels. Never replace the iOS 16 root on the phone without asking: the rollback is
`lisa-tools/ios-artifacts/lisa-ios-gl-v41.simg`. Keep master DMG, IPSWs, kdk-expanded, lisa-firmware, sources, logs.

## Resume here (stopped 2026-11-03 night)
On the phone: rewritten iOS 26 root (image `lisa-tools/ios26/lisa/out/root/userdata-root.img`, SHA-256 f9867c6a..., read back OK). launchd runs, 170+ programs start, SpringBoard starts, then dies in `FBSDisplayMonitor` (no display) and launchd reboots after 3 crashes.
Loop: `build-kernel.sh NAME` (kernel changes only), `build-apfs.sh IMG ["boot args"]`, `run.sh IMG`, `brief.sh IMG`; `sym.sh SLIDE ADDR...` names cache addresses (slide = log's `cache` value minus 0x180000000). If a boot does not reset, get to recovery with Advanced > Enable ADB, then `pull-lisa-log.sh <device-serial> logs/IMG.log`; boot nothing first, it overwrites the log.
Next test, was just started and stopped, result unknown: image lisa26-u8 with the display chain on:
`offline-mobile-graphics-load=1 offline-legacy-phase=4 offline-legacy-register=1 offline-legacy-gop=1 offline-legacy-validate=1 offline-legacy-retire=1 offline-legacy-cpu-present=1 offline-legacy-transfer=1 offline-legacy-pm=1 offline-legacy-unit-scale=3 sheng-vsync=60 offline-key-events=1 offline-hid-input=1 offline-hid-home=1` plus the defaults in build-apfs.sh. Keep the arguments under 608 bytes (payload limit).
Kit: `research-UniXNU-ios26/kits/zeta` is zeta's package with SM8350 probes stubbed in CpuFreqProbe.cpp; `native_touch_*` there is the hook for Lisa's Goodix touch driver.
To do: display; 8 CPUs (`sheng-smp=1 armcpu-self-ipi=1`, not yet tested; the ADT already lists 8; check the secondary reset path and interrupts); memory is 4 GiB now (more needs reserved-region checks); persistent Data (now RAM, resets every boot: `sheng-data-persist` plus a LISA_UFS_HOST write path); auto reset also on the restart timeout panic, which hangs; touch; Metal (NativeMetal, Turnip, LisaGPU); Wi-Fi, Bluetooth; camera and flash (no driver exists). User asked: not only zeta, take the most stable and optimized option for each part, and keep token use low.
Local changes to keep: UniXNU branch `lisa-ios26` is uncommitted (patches/xnu-12377/zzz-lisa.patch, drivers/NativeUfs and NativeRamBlock edits, scripts/zeta-owner-root-key.h, build-storage LISA_UFS_HOST, build-native-legacy-display.py, kits/zeta).
