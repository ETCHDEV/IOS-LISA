# Codex handoff: iOS 26 on Xiaomi Lisa (2026-11-03)

Goal: run iOS 26.5.2 (iPhone18,2, 23F84) on a Xiaomi 11 Lite 5G NE, codename lisa (SM7325, Adreno 642L, serial `<device-serial>`), with a working display, touch, 8 CPUs, full memory, Metal GPU, Wi-Fi, Bluetooth, UFS, and if feasible camera and flash.

Read this file first. `IOS26-PORT-PLAN.md` has more background, but parts of it are older than this file (its top tables describe a route that was later changed). This file wins where they differ.

## What the owner wants
- Keep updates short and token use low.
- Do not only copy the zeta port. For each part, use the most stable and best optimized option, including Lisa's own iOS 16 drivers and UniXNU.
- Do not ask for phone photos if the log can be pulled instead.
- Do not commit or push unless asked.

## Current state
Verified on the phone:
- xnu-12377.121.6 boots from RAM (`fastboot boot`, no UEFI) with a 4 GiB window, on one CPU.
- UFS works read-write. The kernel log goes to the end of `userdata`.
- The signed OS and SystemOS windows publish, and Apple's APFS driver (from the KDK, converted to plain arm64) mounts the root read-only. Owner policy authorizes it.
- Userland is rewritten to plain arm64. `launchd` runs and starts 170+ programs. The Data volume mounts at `/private/var` from a RAM disk.
- The iOS 26 legacy display provider now registers and accepts SpringBoard/backboardd IOSurface swaps. It copies full-screen BGRA frames into the firmware framebuffer at `0xe1000000`; the physical OLED displays the kernel panic console correctly (photo verified).
- Lisa's firmware display handoff is healthy: CTL0 -> INTF1, VIG0 at `0xe1000000`, 1080x2400, stride 4320, autorefresh enabled at about 60 Hz, zero underruns. `zeta-dpu=1` surveys it but does not need to drive it.
- `watchdogd` cannot run on this bring-up and used to crash three times, causing a critical-process panic around 155 seconds. `sheng-disable-watchdogd=1` now works: `BootWatchdogPolicy.c` is linked through the legacy-display archive and called after the RAM Data mount.
- Verified image `lisa26-u12`: 560 execs, 100 exits, 0 panics; three UI presents plus changing scanout samples; it survived the full test and reset deliberately at `LISA: test boot over after 240 s`.

Not working or not started: secondary CPUs, persistent Data, touch, Metal, Wi-Fi, Bluetooth, camera, flash. The display path is working but still uses a CPU copy (about 104-107 ms per 1080x2400 frame), so it is not yet suitable for smooth UI.

**iOS 16 is no longer on the phone.** Rollback: `fastboot flash userdata lisa-tools/ios-artifacts/lisa-ios-gl-v41.simg`, then push the newer files from `lisa-gl/out` with `lisa-tools/push-and-boot.sh`.

## Display result and current test image
`lisa26-u8` exposed a missing disabled-DPU guard in `NativeLegacyDisplay.cpp`; `u9` accidentally reused the stale display archive. The guard was rebuilt into the kernel for `u10`. `u11` surveyed Lisa's live DPU state. `u12` additionally linked the watchdog policy and is the current known-good image. Rebuild it with:

```bash
cd /Users/arjun/IOS-LISA/lisa-tools
bash ios26/lisa/build-apfs.sh lisa26-u12 "-v debug=0x14e serial=3 keepsyms=1 lisa_ufs_probe=1 lisa_ufs_write=1 ufsrw=1 lisa_ufs_log=1 ufsroot=userdata native-ufs-root=1 native-ufs-diagnostics=1 lisa_reset_secs=240 exception_log_max_pid=1 dyld_flags=0x8 offline-mobile-graphics-load=1 offline-legacy-phase=4 offline-legacy-register=1 offline-legacy-gop=1 offline-legacy-validate=1 offline-legacy-retire=1 offline-legacy-cpu-present=1 offline-legacy-transfer=1 offline-legacy-pm=1 offline-legacy-unit-scale=3 sheng-vsync=60 offline-key-events=1 offline-hid-input=1 offline-hid-home=1 zeta-dpu=1 sheng-disable-watchdogd=1"
WAIT_SECONDS=420 bash ios26/lisa/run.sh lisa26-u12
bash ios26/lisa/brief.sh lisa26-u12 10
grep -aE 'zeta-display|zeta-dpu|watchdogd launch policy|LISA: test' ios26/lisa/logs/lisa26-u12.log | tail -40
```

The arguments must stay under 608 bytes (payload format limit). Those flag values were inferred from reading the driver's `bootArg()` checks, not copied from a known-good zeta boot, and the HID and key-event flag names were not confirmed in their sources. Check `research-UniXNU-ios26/kits/zeta/NativeLegacyDisplay.cpp`, `HidInputProbe.cpp` and `KeyStoreProbe.cpp` if the display does not come up.

Driver choice: `NativeLegacyDisplay` binds iOS 26's own `IOMobileGraphicsFamily` at run time and copies frames to the framebuffer the bootloader leaves on (`0xe1000000`, 1080x2400, stride 4320). It was chosen over Lisa's `SweetFramebuffer`, which implements the iOS 16 interface. The MDP base `0xae00000` is verified readable and the live VIG0/CTL0/INTF1 state is correct. The SMMU mapping and stream IDs (`0x820, 0x821`, `0xc21`) have not been needed or verified because firmware autorefresh continues scanning the physical framebuffer.

## The test loop
All scripts are in `lisa-tools/ios26/lisa/`. Run them from `lisa-tools`.

| Change | Command |
|---|---|
| Kernel source or patches | `XNU_MAKE_KEEP_GOING=1 bash ios26/lisa/build-kernel.sh NAME`, then `build-apfs.sh` |
| Boot args, device tree or drivers in the prelink | `bash ios26/lisa/build-apfs.sh IMG ["boot args"]` |
| Boot on the phone and fetch the log | `WAIT_SECONDS=300 bash ios26/lisa/run.sh IMG` |
| Summarize a log | `bash ios26/lisa/brief.sh IMG 20` |
| Name shared cache addresses | `bash ios26/lisa/sym.sh SLIDE ADDR...` (slide = the log's `cache` value minus `0x180000000`) |

`run.sh` needs the phone in recovery with ADB, or in fastboot. Output goes to `ios26/lisa/out/IMG.img` and `ios26/lisa/logs/IMG.log`. It boots with `fastboot boot`, which writes nothing to storage, and the kernel resets itself at a panic, at `Still waiting for root device`, or after `lisa_reset_secs`.

If a boot does not reset:
1. Hold Power + Volume Down for fastboot, then get to recovery. **Boot nothing else first, because a new boot overwrites the log.**
2. In recovery choose Advanced > Enable ADB if the Mac cannot see the phone. `fastboot reboot recovery` often comes up without ADB, while a kernel reset comes up with it.
3. `bash pull-lisa-log.sh <device-serial> ios26/lisa/logs/IMG.log`

`fastboot` sometimes wedges (`usb_write failed`, or commands hang) after an automatic `adb reboot bootloader`, and `run.sh` then prints `NEEDS-USER`. A manual Power + Volume Down restart has always cleared it. It may be size-related or a cable issue; this is not understood. A stripped kernel is an untested idea.

## How the pieces fit
- **Kernel:** `lisa-tools/research-UniXNU-ios26`, local branch `lisa-ios26` from `origin/fix/cache-batch-kernel` (35c384e) of none24smu/UniXNU. **Everything here is uncommitted. Do not clean this tree.**
  - All Lisa kernel changes: `patches/xnu-12377/zzz-lisa.patch` (framebuffer console, start bars, on-screen markers, panic chain, 4 KiB table reservation, bootstrap table 24 pages, `sweet_ufs.c`, log flush, `lisa_reset_secs`).
  - Work copy: `.cache/xnu-12377.121.6-work`, reset to `ac9718f` and repatched each build. To change the patch, edit it directly (recount the hunk header for `bsd/dev/sweet_ufs.c`), or apply the series, edit, and regenerate with `git diff`.
  - Other local edits: `drivers/NativeUfs/{NativeUfs.cpp,RootCommands.h}` (`LISA_UFS_HOST`: its reads go through `sweet_ufs`), `drivers/NativeRamBlock/NativeRamBlock.cpp` (logging, 15 s volume wait), `scripts/build-storage-12377.py`, `scripts/build-native-legacy-display.py` (no Adreno units), `scripts/zeta-owner-root-key.h` (Lisa's key), `kits/zeta` (zeta's package; SM8350 probes stubbed in `CpuFreqProbe.cpp`).
  - `refer/corecrypto-2026` is checked out with LFS filters disabled (`git-lfs` is not installed).
- **Boot:** no UEFI. `ios-sweet/shim` start code plus `lisa-tools/diagnostic/{main.c,xnu_loader.c}`, built by `diagnostic/build-xnu.sh` with `LOADER_FLAGS`. `mk-lisa26-image.py` builds the image from `lisa-ios-gl-v41ma.img`'s header and ramdisk prefix. The physical base is computed so VA and PA agree below 2 MiB. The trust bundle and Data seed are extra payload segments, with their addresses written into the device tree placeholders.
- **Device tree:** `ios26/lisa/make-lisa-adt.py`, from zeta's generator, checked against `ios-sweet/dtb/lisa-live.dtb`.
- **Prelink:** `build-apfs.sh` runs zeta's audits and `emit-zeta-apfs-prelink.py` from `research-none24smu-UniXNU-zeta/scripts`, using the unpacked KDK in `ios26/kdk/expanded`. Python venv with capstone and numpy: `ios26/lisa/.venv`.
- **Root layout:** `userdata` starts with the `SHNGROOT` manifest, then the signed OS and SystemOS windows and their policies. Owner signing key: `ios26/lisa/.cache/owner-root` (private, keep local). The log is the last 4100 KiB of `userdata`.
- **Flashed root:** `ios26/lisa/out/root/userdata-root.img`, 22,338,863,104 bytes, SHA-256 `f9867c6a4e8a111e72ee5f38ebb6b57e9625bd349a38d13a3910e4bb0e27abe2`, verified by reading it back.
- **Data volume:** `ios26/work/data-seed.dbr`, formatted with macOS `newfs_apfs`, Setup marked done. The kernel build reads its volume UUID from `ios26/work/data-seed.json`, so rebuild the kernel if the seed is rebuilt. It lives in RAM and resets on every boot.

## Changing the root image
These intermediates were deleted to save space and must be recreated:

1. Decrypt the images from the IPSW into `ios26/raw/OS.raw` and `SystemOS.raw`: extract `094-54256-108.dmg.aea` and `094-54345-109.dmg.aea`, then `./ipsw fw aea -o DIR FILE`. (They can also be taken from `userdata-root.img`.)
2. `bash ios26/lisa/mount-roots.sh`, then `bash ios26/lisa/rewrite-userland.sh`. It rewrites the binaries and proves the cache (about 20 minutes).
3. `rewrite-zeta-dyld-cache.py --page-shift 12`, then `make-zeta-trust-bundle.py` (see the commands in the zeta README).
4. `make-zeta-systemos-union.py`. **Map SystemOS owners `501:20` to `0:0`; that value is a macOS mount artifact. `501:501` on the OS volume is the real `mobile` user.**
5. `ios26/lisa/grow-lisa-apfs.py --fill-cib`, then `write-lisa-apfs-files.py` (Linux guest under QEMU with `-accel hvf`; parts in `ios26/lisa/vm`, needs `/opt/homebrew/opt/e2fsprogs/sbin/mkfs.ext4`). Its `--replace` merged list and owner mapping were produced by an inline script, so recreate them.
6. `ios26/lisa/build-lisa-root.py` (writes straight into the image with holes, about 19 GB on disk).
7. Stream to the phone from recovery, then compare hashes:

```bash
cat userdata-root.img | adb -s <device-serial> exec-in "cat > /dev/block/by-name/userdata"
```

Do not use plain `fastboot flash` with this image: it failed with `Failed reading from userdata`. `diagnostic/dmg-to-simg.py` leaves zero runs as don't-care, so old data stays behind and breaks verification.

## Remaining work, in order
1. **8 CPUs:** the device tree already lists 8. Try `sheng-smp=1 armcpu-self-ipi=1`, untested. zeta needed self-IPI for interrupts between cores. Lisa's core numbering is in MPIDR Aff1.
2. **Memory:** 4 GiB now. More needs a check of reserved regions, the bootstrap table size, and the window base (`0x180000000`). `/proc/iomem` from recovery lists usable RAM.
3. **Persistent Data:** add a `LISA_UFS_HOST` write path for `native_ufs_store_write`, plus `sheng-data-persist`. It must stay clear of the log area.
4. **Touch:** Lisa's Goodix driver (`darwin-xnu-build/xnu`, `lisa_goodix_berlin.h`, `lisa_geni_spi.c`, `lisa_touch_init`). Feed the `native_touch_*` interface now stubbed in `kits/zeta/CpuFreqProbe.cpp`, which `HidInputProbe.cpp` reads.
5. **Metal:** rebuild on NativeMetal, metal2vulkan and Turnip over LisaGPU (`darwin-xnu-build/xnu/iokit/Kernel/LisaAdrenoUserClient.cpp`, `bsd/dev/lisa_port/lisa_adreno_reference.c`). The old LisaMetal to Gallium path is dropped. On iOS 16 the phone passed compute, render and no-copy import (`VK_EXT_external_memory_host`, kernel request `0x7c`, in `STATUS-2026-11-03.md`). None of this is ported to the iOS 26 kernel yet. zeta's notes list known problems: wallpaper linked functions, client object limits, GPU address exhaustion, slow UI.
6. **Wi-Fi, Bluetooth:** Lisa's iOS 16 Wi-Fi driver (`bsd/dev/lisa_wifi.c` and friends) as a base. The iOS 16 Settings Wi-Fi pages already hung. Bluetooth support there is unconfirmed.
7. **Camera, flash:** no driver exists, only sensor tables in `lisa-firmware/camera`. May not be feasible.

## Rules and limits
- Boot kernels with `fastboot boot` only. Never flash boot partitions.
- Ask before replacing the phone's root again.
- Never delete the master DMG (`lisa-ios-uncompressed-metal-stage.dmg`), IPSWs, `kdk-expanded`, `lisa-firmware`, sources or logs.
- Disk: 17 GB free when this was written. The Trash holds about 10 GB of old DMGs and caches; the owner has to empty it.
- Running outside code (Docker builds, new repos) needed the owner's OK in the earlier tool, so ask first.
- The old `build-lisa-kernel.sh` and `darwin-xnu-build` tree are the iOS 16 kernel (xnu-8796). Do not mix them with this work.
- The RAM log code (`lisa_ring*`, `0xa9000000`) is useless: the data does not survive reset. It can be removed. The `pram` panic region at `0xa9100000` overlaps it and is needed.
