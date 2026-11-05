# Lisa iOS port handoff — 2026-10-22

## Goal and preferences

Port iOS 16 to Xiaomi Mi 11 Lite 5G NE (`lisa`, Snapdragon 778G), with all CPUs, touch and buttons. The user wants maximum progress with as few phone interactions as possible. Build on this Mac; do not run GitHub Actions. USB must be unplugged/reconnected after switching between Android and fastboot. The user explicitly authorized flashing and erasing userdata; preserve bootloader, modem, persist and other partitions. Do not claim that iOS works until observed. The user does not want repeated photos when a shorter observation will do.

## Current phone state

The phone is on a kernel panic screen unless the user has restarted it since the last message. Android userdata has been overwritten: do not promise a return to a working Android install. On 2026-10-22, fastboot identified product `lisa`, serial `<device-serial>`, userdata size `0x1a5e7fb000`. A 24 GiB case-sensitive HFS+ iOS staging image was flashed to userdata; all 23 sparse chunks reported OKAY, exit 0. It contains iOS 16.7.16 iPhone X userland, not a validated iOS port. The bootloader was not flashed. The XNU root driver is configured read-only, and hardware input is not yet functional.

Latest temporary boot used `lisa-tools/diagnostic/out-ufs-xnu/lisa-ufs-read-v3.img` (eight CPUs, UFS, `rd=ufsroot0`). Fastboot returned OKAY. User reported: `panic(cpu 1 caller 0xfffffe0007af94a0) unexpected SIGKILL of launchd (CS_KILLED) with reason -- namespace 9 code 0xe description none`, followed by `handshake structure not initialized`. The latter is a coredump message, not the cause. `bsd/sys/reason.h` defines namespace 9 = `OS_REASON_EXEC`, code 14 = `EXEC_EXIT_REASON_WRONG_PLATFORM`; do not confuse it with namespace 3 code-signing reasons. This panic shows the kernel attempted to execute real launchd from the flashed filesystem; it does not prove launchd ran or iOS UI works.

## Latest result and immediate next step

**Latest user observation:** the platform-argument boot produced `panic(cpu 0 caller 0xfffffe0007af6ed4): mount[3] exited -- no exit reason avaliable -- (signal 0, exit status 1)`. This is a later failure than the wrong-platform panic and indicates a process named `mount` started and exited with status 1; it does not establish an iOS UI boot. XNU `bsd/kern/kern_exit.c` prints the named critical process and its exit status. The exact underlying mount error is not known from this line.

Investigate the boot log immediately preceding this panic if available, particularly mount's own errors. Upstream Sweet's history in `lisa-build/NOTES.md` around lines 152–153 documented a read-only root remount failure and a separate missing `/chosen/boot-manifest-hash`; those are clues, not proof of Lisa's cause. The current Lisa driver intentionally rejects `ufsrw` and the root is read-only. Do not simply enable writes: prove DMA mapping, host ownership, cache coherency, partition bounds and write/flush semantics first, and honor the user's permission while protecting other partitions. Determine whether a read-only-compatible boot path exists; avoid another full image reflash unless needed. There is no working USB logging yet.

`lisa-tools/darwin-xnu-build/xnu/bsd/kern/kern_exec.c` around line 7380 rejects iOS executables under the macOS-target development kernel unless `unentitled_ios_sim_launch=1` or the appropriate entitlement exists. The Sweet port used that boot argument. The previous Lisa boot did not include it. A new **temporary** image was prepared and accepted by fastboot; the screen result is pending:

`/Users/arjun/IOS-LISA/lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-platform-v3.img`

Its payload passed `lisa-tools/diagnostic/validate-payload.py`; it is the same locally compiled patch-0092 kernel and ADT, with boot arguments `debug=0xc serial=3 cpus=8 keepsyms=1 -noprogress rd=ufsroot0 ufsroot=userdata lisa_ufs_probe=1 sweetlog=0 unentitled_ios_sim_launch=1`. This development argument relaxes a platform execution check and is suitable only for this experimental port; it does not solve iOS security or driver compatibility. No filesystem reflash is required for this test.

The user re-entered fastboot, product was confirmed `lisa`, and `fastboot -s <device-serial> boot lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-platform-v3.img` returned OKAY on 2026-10-22. The resulting screen is the `mount[3]` panic above; repeating this identical boot without new instrumentation is unlikely to explain its cause. This boots from RAM, does not flash a boot partition, and uses an experimental UFS driver that writes host-controller registers and initiates DMA reads. If needed hold Power to restart, but Android userdata was erased.

## Findings and remaining work

- XNU's timer was corrected to PPI INTID 27, derived from the live FDT. A previous temporary userspace test reported `LISA active CPUs: 8`. Sustained eight-core workloads and correct heterogeneous scheduling are not demonstrated.
- Before rebuilding the filesystem, launchd failed exec with errno 45. The Mac staging image had compressed `launchd` and `dyld`; compression being unsupported was a plausible cause. An uncompressed 24 GiB copy was created with `ditto --nopreserveHFSCompression --nohfsCompression`; both executables matched their originals, and a read-only HFS check reported the volume appeared OK. The subsequent wrong-platform panic is a later execution failure, consistent with progress, though not proof of every underlying cause.
- `lisa-tools/ios-artifacts/lisa-ios-uncompressed-stage.dmg` is the raw, local 24 GiB image; `lisa-ios-uncompressed-stage.simg` is its Android sparse conversion. Expanded SHA-256 verified independently: `3437afdf09f99ba32a18efb1e4bec49a434139b909fc02a21de9cc57a3e3226e`. The sparse converter encodes zeroes as FILL, not DONT_CARE. Do not blindly reflash; the next test uses the image already on the phone.
- UFS is **experimental**: patch `lisa-build/xnu/patches/0092-lisa-ufs-smmu-guard.patch` gates probing behind `lisa_ufs_probe=1` and requires a visible SMMU configuration consistent with bypass. It does not prove safe ownership, hidden translation, coherency, sustained IO or reliable writes. Probe explicitly rejects `ufsrw`. Latest launchd exec panic is evidence that the UFS root path was usable far enough to reach exec, but there is no independent block-level readback verification. Do not use inherited Sweet touch, power, USB or PMIC code unchanged on Lisa.
- Lisa has GT9897T on Qualcomm GENI SPI according to the local Mu Lisa device tree; `lisa-tools/diagnostic/lisa_goodix_berlin.h` and XNU adapter contain offline-tested parsing but no verified hardware transport, clock, IRQ, reset or HID pipeline. Buttons, battery, USB logging and iOS UI remain unverified. Sweet log labels identify inherited code, not proof that hardware is Sweet.
- The iOS root is mounted read-only in the current boot; even if launchd begins executing, later services may require writable data, storage and platform drivers. Diagnose concrete failures; do not assume the current image is a complete functional iOS installation. Avoid risky UFS or PMIC register programming based only on superficial similarities.

## Project paths and build

- Detailed running history: `lisa-build/NOTES.md` (older Sweet logs describe a different device; Lisa entries start near its end).
- Local XNU source: `lisa-tools/darwin-xnu-build/xnu`; local patches: `lisa-build/xnu/patches`; local build log `lisa-tools/local-build-0092.log`.
- Local build was run in `lisa-tools/darwin-xnu-build` with `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer MACOS_VERSION=13.5 KERNEL_CONFIG=DEVELOPMENT MACHINE_CONFIG=SWEET LISA_FORCE_KERNEL_BUILD=1 KDKROOT=/Users/arjun/IOS-LISA/lisa-tools/kdk-expanded/KDK.pkg/Payload bash lisa-resume-build.sh`. Xcode may require running outside the sandbox; user requires local builds.
- FDT: `ios-sweet/dtb/lisa-live.dtb`; ADT generator `lisa-tools/mk-lisa-adt.py`; snapshot of current ADT `lisa-tools/diagnostic/out-ufs-xnu/lisa-ufs.adt`.
- Staging scripts, local source images and IPSW live under `lisa-tools/`; these can be large and should not be committed. Git metadata may be read-only in the agent sandbox; do not circumvent sandbox permissions.
- The system may require permission escalation for ADB/fastboot or administrator authentication for local filesystem image operations. The user already authorized the device operations above; tool-level approval rules still apply.

## Writable-root attempt prepared

The user asked for read/write access and boot. Patch `lisa-build/xnu/patches/0093-lisa-ufs-userdata-rw.patch` enables the inherited UFS write path only with `lisa_ufs_probe=1 lisa_ufs_write=1 ufsrw=1` and GPT partition name `userdata`; it checks strategy block-number arithmetic and keeps partition/device bounds and timeout quarantine. `rootrw=1` is also set. The local XNU build completed and the existing offline GPT, UFS bounds, timeout and SMMU tests passed. These tests do not prove hardware DMA mappings, cache coherency, controller ownership or reliable writes; this is experimental and can corrupt userdata. The source image filesystem on userdata has already been flashed; no new flash was done for this change.

Temporary image: `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-rw-v3.img`. Payload validation passes. It has not yet been booted on the device. Ask the user to enter fastboot and reconnect USB, verify serial/product, then boot the image temporarily and record the precise screen result. Do not claim iOS has booted, nor that touch or buttons work. If the mount panic remains, capture the preceding mount failure; writable root is a hypothesis, not a verified fix. Android userdata was overwritten earlier.

Fastboot identified `product: lisa` for serial `<device-serial>`; `fastboot -s <device-serial> boot lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-rw-v3.img` returned OKAY for sending and booting. The phone-screen result is pending. This temporary boot enables the unverified UFS write path; no boot partition was flashed.

Writable-root boot result (reported by user): `launchd_cache_loader[22] exited -- exit reason namespace 7 subcode 0x1 description: Unable to find AMFI`. This is progress past the earlier `mount[3]` exit, but does not independently prove write reliability or a successful iOS boot. Upstream Sweet documented this exact failure in `lisa-build/NOTES.md` around line 144, and used `launchd_unsecure_cache=1` to bypass the loader's AMFI signature/trust checks. This weakens security and is not an AMFI implementation. A new structurally validated temporary image is ready at `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-rw-cache-v3.img`; it uses the same 0093 kernel and arguments as the writable boot, plus `launchd_unsecure_cache=1`. No new filesystem flash is needed. Awaiting user to re-enter fastboot and reconnect USB.

2026-10-23: confirmed fastboot product `lisa` and temporarily booted `lisa-ios-rw-cache-v3.img`; fastboot reported sending and booting OKAY. Phone screen result pending. This image enables experimental writes and bypasses the launchd cache loader's AMFI check.

The user reported the 2026-10-23 boot repeatedly displaying `exec fairplayd.H2`, `exec xpcproxy`, `exec bluetoothd`, and briefly showing an Apple logo. This confirms iOS user processes start; it does not confirm SpringBoard or touch. Upstream notes report similar bluetoothd/fairplayd restart loops on Sweet. Prepared `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gui-v3.img`, structurally validated, same flags plus `sweetfb_noconsole=1` to stop console drawing after first framebuffer present. Panics after that point may be hidden; no USB logging is verified on Lisa. Await fastboot reconnect for a temporary boot.

2026-10-23: `lisa-ios-gui-v3.img` was accepted by fastboot on product lisa; screen result pending. This enables `sweetfb_noconsole=1`, so a panic after the first framebuffer presentation may not be visible.

2026-10-23 GUI image result: user reports Lisa remained on an Apple logo; duration unknown. The logo does not prove a graphics presentation or working UI. USB logging has not been ported for Lisa; inherited Sweet UFS logging is not initialized and requires a verified non-filesystem region and retrieval path. Patch `lisa-build/xnu/patches/0094-lisa-quiet-exec.patch` adds opt-in suppression for only `fairplayd.H2`, `xpcproxy`, and `bluetoothd` exec lines. Local build passed (`lisa-tools/local-build-0094.log`) and `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-diagquiet-v3.img` passed structural payload validation. It keeps console visible and writes enabled; it has not been booted. The cause of slow boot is not established; default framebuffer refresh is already capped at 10 Hz.

2026-10-23: fastboot product lisa verified and `lisa-ios-diagquiet-v3.img` sent and booted successfully. User screen result pending. The image suppresses only three repetitive exec messages and leaves the console visible.

2026-10-23: User reports diagnostic boot continues launching services with 5–10 seconds between new console lines after the logo. This does not establish which component is slow. UFS currently serializes one command and polls; default framebuffer copying is already limited to 10 Hz. Added opt-in aggregate UFS command timing, `lisa-build/xnu/patches/0095-lisa-ufs-perf.patch`: every 256 successful post-init commands it prints average and max microseconds and bytes. Local build `lisa-tools/local-build-0095.log` passed. Prepared and structurally validated temporary `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-ufsperf-v3.img` with `lisa_ufs_perf=1`, quiet exec, writable root and visible console. Awaiting fastboot reconnection to run it; performance has not yet been measured and USB/UFS log retrieval is not working.

2026-10-23: `lisa-ios-ufsperf-v3.img` was accepted by fastboot on product lisa; UFS timing and screen result pending.

2026-10-23 UFS timing result from user: `256 cmds avg 219 us max 645 us bytes 18444288`; the screen's last named process was `exec adid`, followed by two timing summaries and no further visible changes. The timing covers successful UFS command execution only, not mutex wait, application I/O wait or overall throughput; 5–10 s between console lines is not itself proof of a stall. User saw an Apple logo in the prior GUI boot, not a proven iOS-drawn frame. The existing AppleCLCD registered on Lisa's 1080x2400 boot framebuffer in earlier logs; frame submissions and SpringBoard still unverified. Read-only inspection of local `lisa-ios-uncompressed-stage.dmg` found `/private/var/mobile/Library/Preferences/com.apple.migration.plist` contains `LastSystemVersion=20H392`, matching SystemVersion ProductBuildVersion, as well as DMUserDataDisposition; this is the original staging image, not the live filesystem after RW boots. Thus the upstream missing migration-marker issue is not established as the cause. Read-only staging image detached after inspection. Asked user whether current screen shows framebuffer swap, backboardd, or SpringBoard lines.

2026-10-23: Lisa live FDT reserves splash_region 0xe1000000–0xe3300000, matching previously observed boot framebuffer and text. Fixed inherited console muting that happened on any present call before validating a drawable layer: patch `lisa-build/xnu/patches/0096-lisa-fb-valid-present.patch` delays muting until an actual valid copied layer, logging `LISA FB: first valid layer`. It preserves console until then; direct use of default surface and actual graphical rendering still require validation. Local build `lisa-tools/local-build-0096.log` passed; payload `lisa-ios-fbfix-v3.img` structurally validated, uses writable root, AMFI workaround and sweetfb_noconsole=1. Fastboot reconnect pending.

2026-10-23: Verified fastboot product lisa and booted `lisa-ios-fbfix-v3.img` successfully via fastboot; phone screen result pending.

2026-10-23: User reports framebuffer-fix boot ended on Apple logo; did not establish whether text was shown after it or a valid layer was presented. Asked for transition details. Decompilation of the captured live Lisa DTB independently confirms splash region `0xe1000000+0x2300000`, GENI SPI `spi@a94000`, Goodix touch at chip-select 0, 6 MHz, IRQ GPIO 81, reset GPIO 105, panel 1080x2400 and GT9897T firmware variants. The inherited Sweet touch code targets SE7 I2C at `0xa84000` and GT9896; do not enable it on Lisa. The Lisa Goodix packet-to-HID adapter and synthetic multi-contact/ack test passed again, but it has no Lisa SPI transport, clock, power, pinmux or IRQ implementation, so physical touch remains unsupported. Average UFS command latency 219 us does not explain full service delays. The previous GUI test cannot by itself establish SpringBoard, an iOS frame or working touch.

The user clarified that on `lisa-ios-fbfix-v3.img` the Apple logo stayed visible throughout, with no console text observed. Fastboot acceptance and structural validation do not show the kernel or UI ran in that attempt. The prior visible-console `lisa-ios-ufsperf-v3.img` did reach iOS service execs and UFS activity. Both payloads validated offline; the loader sets physical base 0xe1000000, width 1080, height 2400 and stride 4320. Live stock DTB independently confirms `spi@a94000` / `xiaomi,k9-spi`, Goodix IRQ 81 and reset 105; hardware SPI and touch handling remain unimplemented. The synthetic Goodix adapter test passed. The two boots differ in kernel revision and framebuffer console argument; either a failed early handoff or muted/obscured output remains possible. Do not claim framebuffer or touch fixed.

2026-10-23: Prepared `lisa-ios-fbtrace-v3.img` from framebuffer-fix kernel with console kept visible, verified product lisa, fastboot send and boot returned OKAY. User screen result pending.

2026-10-23: User reports framebuffer-trace boot displayed logs after Apple logo, ending at `exec routined` / `exec adid`; the 0096 framebuffer kernel does reach userspace with console visible. No frame submissions or SpringBoard have been confirmed. Added opt-in `lisa_status=1` kernel status thread (patch 0097), every 30 s counts SpringBoard/backboardd/datamigrator processes and atomically reads framebuffer swap count; process presence does not imply health, and a swap does not by itself establish visible output. Local build `local-build-0097.log` and payload validation passed. `lisa-ios-status-v3.img` ready; awaiting fastboot reconnection.

2026-10-23: Fastboot verified product lisa and accepted `lisa-ios-status-v3.img`; status counts pending from phone screen.

2026-10-23: User reported 0097 status `SpringBoard 1 backboardd 1 migration 0 swaps 4`; status appeared four times at 30-second intervals then stopped updating. This establishes both processes existed at the sample and swaps were submitted, not their health or visible scanout. Added 0098 framebuffer counters for presentation calls, accepted copied layers and default-surface uses, extending status output. Local build `local-build-0098.log` passed; `lisa-ios-fbstats-v3.img` structurally validated and is ready for temporary boot. User asked to reconnect fastboot.

2026-10-23: Fastboot product lisa verified; temporary `lisa-ios-fbstats-v3.img` was accepted and booted; framebuffer counter result pending.

2026-10-23: User reports `SB 1 BB 1 DM 0 swaps 4 present 3 copy 3 default 0`, status kept repeating. This shows processes present, four swap submissions and three valid copy-path entries; the original counter was incremented before memcpy, so completion and scanout were not proven. Patch 0099 moves the counter after the copy and enables opt-in `lisa_ufs_log=1 sweetlog=1`: it checks the live HFS/HFSX volume signature and block-count bounds before writing a 4 MiB ring to the end of the selected userdata partition, outside the reported HFS volume. The local 24 GiB staging volume ends at 25769803776 bytes, well before log start 113250377728 on the previously measured partition, but live checks still apply. UFS writes and log recovery remain experimental. Local kernel build `local-build-0099.log` passed; `lisa-ios-log-v3.img` payload structurally valid. `lisa-tools/pull-lisa-log.sh SERIAL OUT.txt` retrieves from root-capable recovery using ADB and checks length/magic via decoder; it has not been tested on Lisa, whose recovery/root availability is unknown. Awaiting fastboot reconnect for temporary boot; no other partitions were targeted.

2026-10-23: Fastboot verified product lisa and accepted temporary `lisa-ios-log-v3.img`; user asked to run at least two minutes then enter recovery and reconnect USB for log retrieval. Logger and recovery access remain unverified.

2026-10-23: User reports status continued repeating, and requested focus on the graphical interface and touch instead of log retrieval. Prepared `lisa-ios-upstream-gui-v3.img` with upstream `ignition_level=1` and `wdt=-1`, writable iOS root, console suppression after a valid layer, and persistent logging disabled. Fastboot accepted the temporary boot; graphical result pending. Lisa Goodix SPI hardware transport and physical touch remain unimplemented. The user must reconnect USB after switching modes.

2026-10-23: User reports `lisa-ios-upstream-gui-v3.img` showed an Apple logo for 1–2 minutes with no setup or home screen. This does not establish whether launchd ran or scanout was updated; console is suppressed after the first accepted framebuffer layer. Earlier visible-console boot showed SB and BB process counts and 3 copy-path entries, but 0098 counted before memcpy. Framebuffer maps the boot handoff address through an IOMemoryDescriptor with write-combined caching; no evidence yet that the copied layers are the desired GUI or are visible on Lisa. The local overlay preparation script hard-links cryptex dyld cache entries into the root cache path required by `ignition_level=1`. Touch still lacks actual Lisa GENI SPI transport, firmware/clock/power and interrupt integration. Do not claim GUI, touch or improved boot speed; no proven display or speed fix has been established.

2026-10-23: The upstream GUI image remained on the logo after 1–2 minutes. Verified the original uncompressed staging image read-only: its migration preference records 20H392 and purplebuddy has setup completed, so reflashing the same stage is not a targeted fix (the running userdata may differ). Detached staging disk. Booted `lisa-ios-log-v3.img` temporarily on verified fastboot product lisa to capture startup without photographs; this image uses guarded userdata-tail logging and visible console, and does not set ignition_level=1. Fastboot returned OKAY. User was asked to run at least two minutes and enter recovery; ADB recovery and log capture remain unverified.

2026-10-23: User reports `lisa-ios-log-v3.img` repeatedly showed `SB 1 BB 1 DM 0 swaps 5 present 2 copy 2 default 0` and entered recovery. These 0099 copy counts increment after memcpy completes; they do not establish that the copied content is the interface or that the panel scans it out. ADB detects serial <device-serial> as unauthorized, so log retrieval has not occurred; asked the user to check recovery's ADB authorization or describe the recovery screen.

2026-10-23: Lineage recovery root ADB enabled and verified; retrieved `lisa-tools/lisa-recovery-verified.log` from guarded userdata tail (4 MiB ring, flush #14, 132114 bytes). Fixed `pull-lisa-log.sh` to redirect remote dd stderr; verified a second retrieval and decoder. Log shows backboardd creates 1125x2436 BGRA surfaces (stride 4608), two valid frames copied and no further swaps, repeated searches for absent IOAcceleratorES/AGX, while SpringBoard and backboardd remain present. This does not prove a missing GPU is the sole reason for the stall. Existing Sweet user-stack sampler was compiled but not started on Lisa. Added opt-in `lisa_sample=1` patch 0100, built locally (`local-build-0100.log`; sandbox build failed on /dev/fd, elevated local build passed), prepared structurally validated `lisa-ios-sampler-v3.img` with guarded logging and no status spam; sampled result not yet obtained. Rebooted recovery to fastboot and asked user to reconnect USB. User-data tail logging can be read with the fixed script from root recovery.

2026-10-23: `lisa-ios-sampler-v3.img` sample and root recovery retrieval succeeded, log `lisa-tools/lisa-sample-20261023.log`. Downloaded blacktop/ipsw release 3.1.739 for local dyld symbolication; read-only staging cache mounted at `lisa-tools/ios-artifacts/inspect-root`, symlinked into `lisa-tools/dyld-symbols`. SpringBoard t0 is blocked by unfair lock while initializing an LS context for ControlCenter module availability; t1 waits synchronously for Launch Services reseeding. Captured a second sample with `lisa-ios-lsd-v3.img`, log `lisa-tools/lisa-lsd-20261023.log`, symbolication `lisa-tools/lisa-lsd-symbols.txt`: lsd t1 blocks on a dispatch semaphore inside libMobileGestalt during installation identity generation; lsd t9 is in a run loop waiting for an IOKit match. Disassembly `lisa-tools/mg-wifi-disassembly.txt` shows the path queries /chosen `mac-address-wifi0` as CFData, then searches sdio/wlan/marconi-wifi, and otherwise waits for a matching service. Nested MG keys decoded from the dyld cache correspond to UniqueDeviceIDData and WifiAddressData; this suggests absent Wi-Fi identity is causing the Launch Services wait, but proof requires boot. `lisa-ios-mg-v3.img` was booted to trace lsd IOKit; user was asked to return to recovery after 3 min but is unavailable for now. Do not interrupt their phone remotely.

Prepared an offline experimental fix: `lisa-tools/add-lisa-wifi-identity.py` adds a stable locally administered synthetic 6-byte address derived from the already synthetic ADT ECID as /chosen `mac-address-wifi0` (no hardware I/O or actual Wi-Fi). Input `lisa-ufs.adt`; output `lisa-wifi-identity.adt`. `lisa-ios-wifi-id-v3.img` uses this ADT, console mute upon valid frame, guarded logging and lsd/SpringBoard sampler; payload structurally valid. It has NOT been booted. iOS identity will be synthetic; it is not Lisa's physical MAC, nor Wi-Fi support. Next: when user returns, retrieve `lisa-ios-mg-v3.img` log from root recovery with `bash lisa-tools/pull-lisa-log.sh <device-serial> OUTPUT`, inspect IOK lsd, then enter fastboot/reconnect and temporarily boot the Wi-Fi identity image. Do not reflash userdata for this ADT-only test. Verify whether SpringBoard UI appears and whether stacks move beyond the wait; touch remains unimplemented.

2026-10-23: After user returned to Lineage recovery, root ADB retrieval of `lisa-ios-mg-v3.img` log succeeded (`lisa-tools/lisa-mg-20261023.log`, 127415 bytes, flush #40). IOK lsd trace repeatedly shows chosen lookup followed by missing `sdio`, `wlan`, `marconi-wifi`, and notification waiting on `IONetworkController`; corroborates the MobileGestalt Wi-Fi identity wait. Initiated recovery reboot to fastboot for the prepared `lisa-ios-wifi-id-v3.img`; waiting for USB reconnect. This supplies synthetic identity only; no Wi-Fi hardware or touch driver.

2026-10-23: Fastboot verified product lisa and accepted temporary `lisa-ios-wifi-id-v3.img` with synthetic `/chosen/mac-address-wifi0`; boot result unknown. User will observe/re-enter recovery later. Keep phone running; retrieve log on their return to recovery and compare `IOK lsd` lookup and `SMP` stacks. No verified SpringBoard screen or touch.

2026-10-23: User reports temporary `lisa-ios-wifi-id-v3.img` reached iOS lock screen with time/date and charging icon, then slept; power/volume buttons did not wake it. Animations were slow. Root recovery log `lisa-tools/lisa-lockscreen-20261023.log` contains `8/8 CPUs initialized, SGI1 verified`; this does not prove all cores render at full speed. Recovery input inventory identifies PMK8350 PON for power/volume down, separate gpio-keys volume up, and Goodix touch. Lisa DTS maps PON at SID 0 peripheral 0x13 and volume up to GPIO specifier <0x59c 6 1>; resolve phandle before use. Sweet button polling code uses different PMIC addresses and was not enabled. Lisa Goodix GT9897T is SPI; Sweet's I2C driver is not compatible. No GPU/Metal acceleration or real touch/wake-button support is established. Prepared `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-fb30-v3.img` from the lock-screen payload adding only `sweetfb_hz=30` to test increasing the default 10 Hz software framebuffer copy rate; payload validator passed, device result pending. No userdata reflash. Recovery was commanded to bootloader; USB must be reconnected in fastboot.
2026-10-23 follow-up: `lisa-ios-fb30-v3.img` was accepted by `fastboot boot`; awaiting user observation. No flash. The research Lisa DTS resolves volume-up phandle 0x59c to PM7325 SID 1 pinctrl@0x8800, gpio6 active low; PMK8350 PON is SID 0 @0x1300. Register offsets, observer APID ownership, and wake event delivery still need verification before enabling a new key driver.
2026-10-23: User reported 30 Hz boot animation did not look smoother and returned to recovery. Retrieved `lisa-tools/lisa-fb30-20261023.log` (flush 12); 8/8 CPUs initialized and backboardd submitted frames; display off/on appears. Prepared `lisa-ios-fb60-v3.img`: same proven payload with `sweetfb_hz=60`, opt-in stack sampler disabled (`lisa_sample=0`) and targeted I/O logger disabled (`sweetiolog=off`); guarded UFS logging remains. Payload validated structurally, image not yet booted. User requested faster boot and GPU support; no performance result or GPU implementation claimed. Rebooted recovery to bootloader; fastboot reconnect needed.
2026-10-23: `lisa-ios-fb60-v3.img` was accepted by fastboot; user is observing, result pending. Implemented opt-in `lisakeys=1` PMK8350 PON observer polling for power (status bit 7) and volume-down (bit 6), from Linux pm8941-pwrkey driver and Lisa DTS SID 0 peripheral 0x13 offset 0x10. No PMIC peripheral configuration writes; the observer command is an MMIO write requesting a read. Timeouts/errors stop polling; support depends on accessible arbiter mapping and event wake behavior, both unverified. Local build `local-build-0101b.log` succeeded; initial build needed explicit thread_terminate declaration. `lisa-build/xnu/patches/0101-lisa-pon-keys.patch` contains changes. Prepared structurally valid `lisa-ios-pon-keys-v3.img` with 60 Hz and `lisakeys=1`; not tested. Volume-up PM7325 GPIO6 and Goodix SPI touch remain unimplemented. No GPU/Metal acceleration. Keep user informed before testing; verify button log and whether it actually wakes UI.
2026-10-23: User reports 60 Hz boot looked smoother, though still laggy. Connected fastboot and accepted `lisa-ios-pon-keys-v3.img`; awaiting observation of power/volume-down/wake. Studied Lisa DTS and Linux primary driver references; `lisa-tools/LISA-INPUT-GRAPHICS-NOTES.md` records hardware mapping, dependencies and safe next steps. `test_lisa_goodix_kernel.py` and host GT9897 synthetic decoder tests passed; this does not test actual hardware or SPI. Adreno 642L needs GPU/GMU, SMMU, power and an iOS graphics interface, none implemented.
2026-10-23: Recovered `lisa-pon-keys-20261023.log` from Lineage recovery (flush 26); observer initialized `PON APID 100 status 0x10`, 8/8 CPUs initialized, power and volume-down press/release events recorded. Power events precede `SWEET FB: display off` then subsequent power events precede `display on`; user confirmation of visible wake and whether many repeated volume events were intentional is pending. No PMIC writes beyond observer read commands. Added host-only `lisa-tools/input/lisa_goodix_spi.c/.h`: SPI read framing and restricted event ACK using injected full-duplex callback; synthetic sanitizer tests pass. It is not connected to hardware or XNU; GENI transport, clocks, pinctrl, interrupt, and touch wake remain unfinished. GPU acceleration absent.
2026-10-23: User confirms short power press and volume down work; repeated volume events in log were deliberate. Volume up does not work or may invoke Siri (not independently confirmed). Recovery `/sys/class/power_supply/battery` is provided by `qcom,battery_charger` over `qcom,pmic_glink`; reported design capacity 4250000 uAh, model K9_COSMX_4250MAH. Sweet's PM6150 SPMI gauge is not suitable. Corrected fallback design capacity from 5020 to 4250 mAh in `0102-lisa-battery-design-capacity.patch`, local build `local-build-0102.log` passed; `lisa-ios-battery-4250-v3.img` prepared and structurally checked, NOT booted. The reported percentage and charging state in iOS are still static fallback, not real telemetry. A live gauge needs PMIC GLINK/RPMSG transport and read-only battery-manager property requests; no charger or PMIC settings changed. Physical GT9897 SPI transport and GPU acceleration remain unimplemented.

2026-10-23: User requested evaluation of oosexgang HackiOS for Lisa. Cloned `lisa-tools/research-hackios-kernel` and `lisa-tools/research-hackios-userland`; see `lisa-tools/HACKIOS-LISA-PORT.md`. Upstream claims Adreno 642L Mesa/freedreno CoreAnimation, not complete app Metal. Its redwood Goodix GENI SE13 and IRQ81/reset105 match Lisa, but Lisa is GT9897T and regulator topology differs; do not enable redwood's GPIO107/power/reset sequence unmodified. Its ADSP GLINK battery code matches Lisa recovery's `PMIC_RTR_ADSP_APPS` channel, but firmware and memory mappings must be verified. No hardware driver, UEFI, or iOS image from this port was installed or tested. Lisa recovery was connected, but vendor firmware paths were not mounted/exposed.

2026-10-23: Added XNU GT9897 SPI read/ack and IC-info discovery at `0x10068` (`0103-lisa-goodix-spi-adapter.patch`) and bounded GENI FIFO implementation adapted from HackiOS (`0104-lisa-geni-spi.patch`). Host sanitizer tests cover protocol and fake registers; hardware setup, bus binding, touch worker and physical tests remain unfinished. Copied Lisa's ADSP firmware from recovery's read-only `/firmware/image` to `lisa-tools/lisa-firmware/adsp` (38 files, 20 MiB); the Lisa DT and Redwood ADSP reserved region both say `0x86700000 + 64 MiB`. No DSP was started, PMIC configuration changed or phone image flashed. Check `lisa-tools/local-build-0104.log` for final compilation status. See `lisa-tools/HACKIOS-LISA-PORT.md`.
Lisa ADSP firmware validation: `python3 lisa-tools/diagnostic/check_lisa_adsp.py` passed 36 loadable segments and the HackiOS metadata size/layout checks; the phone's read-only adsp.mdt SHA-256 matched the local copy. This is a file/metadata check only, not firmware authentication, carveout ownership verification or a DSP boot. Redwood's property IDs 12+ differ from generic Linux and must be verified for Lisa.
The local full kernel rebuild for 0103+0104 completed successfully (`lisa-tools/local-build-0104.log`); both host sanitizer tests and ADSP segment validation passed. No image containing these changes has been booted, and the FIFO is deliberately not attached to live hardware yet.

## 2026-10-26 (Claude): GPU UI through OpenGL ES first, Metal second

User decision: get a GPU-drawn UI through CoreAnimation's OpenGL ES renderer first (the path upstream HakiOS
uses on the same SM7325/Adreno 642L, chip 0x06030500; its README lists "GPU rendering on Adreno (OpenGL ES
through Mesa)" as working and "Metal" as not yet), then return to Metal for apps. Upstream moved to
github.com/oosexgang/hackios-xnu-kernel (to be renamed hakios-os); our local research clones are 10-22 and
their GL files (eagl.c, gl_osmesa.c, freedreno-darwin.patch, the drm shim) are identical to upstream main
apart from the redwood->hakios rename.

- `lisa-tools/lisa-gl/`: upstream eagl.c/gl_osmesa.c/eagl_slots.txt/mkeagl.py (header renamed lisa_gl.h),
  `build.sh` (relinks libOSMesa from the existing Mesa tree, whose shim already names LisaGPU; builds
  liblisagl.dylib and the LisaMetal plugin), `deploy-stage.sh` (installs into
  ios-artifacts/lisa-ios-uncompressed-metal-stage.dmg, writes ios-artifacts/lisa-ios-gl-v1.simg, verifies).
- Injection without kernel changes: `lisametal.m` gained a constructor (end of file, original saved as
  lisametal.m.pre-gl). With boot-arg `lisagl=1` it makes +[LisaMetalDevice isSubclassOfClass:_MTLDevice]
  answer NO, so Metal's registration silently drops the plugin (no Metal device in any process), and in
  backboardd it dlopens /usr/local/lib/lisa/liblisagl.dylib, whose constructor replaces EAGLContext's
  methods; CoreAnimation then falls back to its GLES renderer on Mesa freedreno. Without lisagl=1 nothing
  changes.
- Boot image `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v1.img`: v14-safetrace with ` gpu_dump=1`
  replaced by ` lisagl=1` (10 bytes differ, payload-ios-gl-v1.bin structurally valid). Not booted yet.
- eagl_slots.txt is iOS 16.5 (20F66) OpenGLES dispatch-table offsets; Lisa runs 16.7.16. Job
  `agent-queue/002-gl-slots.job` disassembles the entries in Lisa's cache to confirm before booting.
- `lisa-tools/agent-runner.sh` (user starts it in Terminal) runs `lisa-tools/agent-queue/*.job` on the Mac,
  output in *.out/*.rc; Claude's device shell is a Linux VM without USB or Xcode.
- Status: nothing built, flashed or booted yet in this session. Do not claim GPU UI until observed.

### 2026-10-26 afternoon (Claude): GPU-drawn home screen reached; root causes found

Observed by user: `lisa-ios-gl-v7.img` (GL mode, gpu_cached=1) booted iOS to the HOME SCREEN drawn by the
Adreno through CoreAnimation's OpenGL ES renderer (laggy, icons missing). Log: 300-400 GPU submissions / 5 s,
no hangs (diagnostic/gl-v7-recovery.log).

Root causes fixed (each verified on the phone by the next log):
1. SweetSurface passed CoreAnimation's GPU-renderer screen format 'b3a8' (10-bit XR + separate alpha plane)
   through; upstream MiniIOSurface turns it (and w30r/l10r/w30R) into one plane of BGRA. Added in
   iokit/Kernel/arm/SweetSurface.cpp (sweet_surface_layout; boot-arg lisa_surf_bgra=0 disables).
2. SweetSurface answered selector 6/7 (WrapClientImage/Memory) with success + zeroed lock result -> GLES
   renderer crash in IOSurfaceClientGetBulkAttachments (far 0x30). Now kIOReturnUnsupported (as upstream).
3. LisaAdrenoUserClient::surfaceBuffer assumed upstream's layout (pixels one page in). SweetSurface pixels are
   at 0: MiniIOSurface.pixelStart (0) is used, with a bounds clamp.
4. THE GPU HANG (also Metal's stall): Codex moved GEM buffers to pageable client-task memory (cacheable), but
   with default gpu_cached=0 the submit path never cleaned them (`else if (!b->seen || !gGPUCached) continue;`),
   so the GPU executed stale shader/constant memory: CP reaches CP_WAIT_FOR_IDLE, SP/HLSQ/VFD busy
   (RBBM_STATUS 0xe59085), no SMMU fault. Boot with gpu_cached=1. Cleaning by the user VA then panicked
   (dc civac translation fault, esr 0x96000147), so GEM buffers are now cleaned per physical page through
   phystokv (lisaFlushPhysical in LisaAdrenoUserClient.cpp).
5. Metal: lisadrawable.m dlopened "/lisa/lib/libredwoodair.dylib" (rename artefact) with RTLD_NOLOAD, so
   rwa_texture_from_surface was never found and no drawable was ever made (black screen). Now
   /usr/local/lib/lisa/liblisair.dylib.
6. Icons: SweetSurface now wraps IOSurfaceAddress client memory (withAddressRange + prepare), GPU import
   refuses wrapped surfaces (MiniIOSurface.client) so eagl uploads them.
7. Volume-up: sweet_keys.c polls PM7325 GPIO6 (ppid 0x18d, RT_STS 0x10 bit0, active low) via observer reads.

Images: diagnostic/out-ufs-xnu/lisa-ios-gl-v8.img (GL) and lisa-ios-metal-v18.img (Metal), both gpu_cached=1,
kernel with 1-7. Userdata flashed with ios-artifacts/lisa-ios-gl-v3.simg (LisaMetal path fix + GL libs).
Tools: diagnostic/replace-payload.py (swap payload in a boot image), agent-runner.sh/agent-queue.
Lag: eagl refills a 10.8 MB screen texture per frame in blit mode; try boot-arg glblit=0.
Long-term Metal option: github.com/nullmoth/nvidia-macos-driver (Metal 3 plugin -> AIR->SPIR-V -> Mesa NVK;
PolyForm Noncommercial + LGPL translator) — port with Turnip (Mesa Adreno Vulkan) over the LisaGPU msm shim.

### 2026-10-26 evening (Claude): performance round
- User-observed: GL v8-v11 boot to home screen with icons; v11 (GPU present) smoother (~30 Hz), sleep instant.
- `lisa_cpufreq.c` (new, bsd/conf/files): EPSS 0x18591000 x3 (Lisa live DTB matches redwood). lisacpufreq=1 top
  row (log: little 1.8 GHz, big/prime row 0 -> 2.4 GHz — big cores had been at their lowest row);
  lisacpufreq=2 adds a 100 ms load governor (processor_info CPU_LOAD_INFO ticks); lisacpufreq_cap=MHz.
- GPU present: SweetFramebuffer publishes the panel memory as a 2160x1200 / 8640-byte-row "wide" surface
  (Adreno 64-byte pitch rule; panel rows are 4320) as IOMobileFramebuffer properties LisaPanelSurface /
  LisaPanelOffset; with lisa_gpu_present=1 it skips its memcpy for full-screen layers, and eagl.c
  (panel_present) draws each frame onto the panel with a texelFetch triangle.
- glblit=0: no 10.8 MB per-frame texture refill. gpu_cached=3: clean GEM at allocation (and after kept-buffer
  bzero) only, not at every submit (hypothesis: the hang came from dirty zero-fill lines; Mesa's mapping is
  WC). Images: diagnostic/out-ufs-xnu/lisa-ios-gl-v12.img (lisacpufreq=2, gpu_cached=3, gpu present).
- Metal (lisa-ios-metal-v21.img + userdata v5): RG8 'LA08', block-aware texture layout copies (ASTC crash
  fixed), drawable path fixed; renders but only ~18 frames reach the screen -> stack sampler run pending
  (metal-v19-sample cmdline: lisa_sample=1).
- Volume-up works (sweet_keys.c, PM7325 GPIO6 ppid 0x18d APID 133).
- Open: RAM limited to 4 GiB bank (multi-range physmap, see memory/LISA-MULTIRANGE-PLAN.md); apps missing
  from home screen (cause not in logs yet); PosterBoard uncaught ObjC exception (wallpaper); Control Center
  glyphs white (GLES renderer functions); brightness (needs DSI DCS path); Wi-Fi/audio/battery/haptics have
  upstream SM7325 drivers to port (hakios.wifi.c, hakios.wlfw.c, hakios.adsp.c, hakios.sound.c, hakios.mic.c).
- Disk: removed regenerable images (old simgs, lisa-ios-uncompressed-stage.dmg, research-mu, KDK dmg, old
  boot images). Symbolication: lisa-tools/sym-mount.sh mounts the master stage at ios-artifacts/inspect-root.

## 7. Codex continuation — 2026-10-26, v16 cache-transition fix

Latest user photo: `/Users/arjun/Downloads/PXL_20261026_103930336.jpg`.
Pulled recovery log: `lisa-tools/diagnostic/gl-v15-codex-recovery.log` (93,526 ring bytes).
The photo shows `phystokv_range: illegal PA 0x269e4000` during GL startup with `gpu_cached=5`.
The ring was flushed before the panic, so the photo is the panic evidence.

**Root cause found in XNU cache maintenance, not a low-pool GEM allocation:**
`xnu/osfmk/arm/pmap/pmap.c:pmap_sync_page_attributes_phys` passed
`(vm_offset_t)(pp << PAGE_SHIFT)` to `flush_dcache`. `ppnum_t` is uint32_t;
the shift truncated the physical address BEFORE the widening cast. For example,
0x2269e4000 (inside the configured RAM bank) became 0x269e4000 (the photographed invalid PA).
Fixed to `flush_dcache(ptoa(pp), PAGE_SIZE, TRUE)`, which widens before shifting.

Built kernel successfully with the section 3 command. Verified actual machine code:
old `lsl w0, w0, #14`; new `ubfiz x0, x0, #14, #32` (full 64-bit result).
This preserves Claude's mode 5 WCOMB allocation/restore experiment and every GL/userland change.
No userdata reflash. No weakening of RAM range checks or change to memory bank configuration.

New image: `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v16.img`.
Payload: `lisa-ios-gl-v16-payload.bin`; 11 segments; RAM 0x180000000+0x100000000.
SHA256 image: a80d7c64b6386018adfcb02c9cac25c5cf8a729af79af2f788b248a520973356.
Validated payload structure and exact preservation of command line, device tree, LISAGPUF firmware.
Temporary boot accepted by fastboot after product=lisa check (send OKAY, boot OKAY).
**Device result is pending.** User was asked to try scrolling, then return to recovery for GPU timings.
Next: pull `gl-v16-recovery.log`, check for GPU timeouts/panics and compare cache-clean/frame times
against v14safe (roughly 1.5–1.7 ms cache maintenance per submit, ~34 ms mean frame).
If mode 5 still hangs, preserve this 64-bit fix and use cached=1 until the remaining alias/coherency
issue is understood. Do not claim Metal acceleration: this image still uses the GL CA renderer.
Best previously confirmed fallback remains v14safe; v16 has not yet been confirmed by the user.

### v16 device update
User confirmed the corrected v16 image boots to iOS. Smoothness report and recovery log are still pending;
this confirms the startup panic is cleared, not sustained GPU stability or Metal presentation.
Reapplicable source patch: `lisa-tools/patches/0094-lisa-pmap-cache-physical-width.patch`.
Disassembly evidence: `lisa-tools/diagnostic/gl-v16-cache-address-disassembly.txt`.

## 8. v16 confirmed; v17 buffered rendering and stock-app repair (2026-10-26)

User: v16 booted, was smoother, but animations remain slow, random flicker; apps/wallpapers missing.
Pulled `diagnostic/gl-v16-recovery.log`: 389,906 ring bytes / flush14; no GPU timeouts. Sampled frame
median 18,994 us versus v14safe 28,028 us; median cache maintenance 826 us/submit versus 1,626 us.
Last cumulative renderer mean ~22.5 ms; timings depend on scene/workload, not a controlled benchmark.

Found 32 COMPLETE stock apps still under `/private/var/staged_system_apps`, not merely placeholders.
Upstream `research-hackios-userland/userland/build.sh` moves these into `/Applications`; Lisa's master
had never done so. `lisa-gl/repair-app-layout.py` now performs that relocation without editing app
executables/signatures; all 32 executable hashes validated before/after. Manifest:
`diagnostic/gl-v7-app-layout-applied.json`. Cached launch paths are relocated if present.
Wallpaper assets are present under `/Library/Wallpaper`, including default iOS16 collection 7400.
PosterBoard SIGABRT in v14safe symbolicates to uncaught Objective-C exception on main dispatch queue;
v16 also has assetsd SIGABRT. Added `liblisaexceptions.dylib`, scoped through DYLD_INSERT_LIBRARIES only
to PosterBoard and assetsd (loose plists AND precompiled launchd.plist). It logs first16 thrown exception
names/reasons, chains any previous ObjC preprocessor, never suppresses exceptions. Wallpaper fix remains
pending the actual exception reason; do NOT claim wallpaper is repaired yet.

Rendering: `lisa-gl/src/eagl.c` mirrored path had a concrete bug: target_to_surface clears drawn, so
frame_end's subsequent condition skipped panel_present. Preserve eligibility before copy. Also final
panel draw/copy now invalidates a preceding glFinish's finished flag so new work is flushed/waited.
Mirrored private target is initialized from its IOSurface by a GPU blit instead of full-screen CPU
readback/checksum/upload. Enable glblit=1 to avoid in-place framebuffer-fetch corruption/flicker.
Compiled all userland successfully; dylib ad-hoc signatures verified. build.sh now fails on compile
errors instead of silently retaining stale binaries. Prior GL library/launch plists backed up under
`ios-artifacts/gl-v16-userland-backup`.

New temporary boot: `diagnostic/out-ufs-xnu/lisa-ios-gl-v17.img` (same corrected v16 kernel, gpu_cached=5,
only glblit=0 -> glblit=1). Structure valid, 11 segments. New full userdata:
`ios-artifacts/lisa-ios-gl-v7.simg`, expanded 25,769,803,776 bytes,
expanded SHA256 e4c9f704ad3efba2be3cab06529f40d24c2c61bb8edd5cd4cd26a0d825cae486 (maker/verifier agree).
Flash in progress after product=lisa and userdata size 0x1a5e7fb000 checks. Must let the user restart
fastboot/reconnect AFTER flash and BEFORE temporary boot, per their prior instruction.
Next boot result not yet known. Read `LISA EXCEPTION` lines for wallpaper issue and confirm app icons.

### Flash transport interruption
The first full v7 userdata flash acknowledged chunks1–9, then stalled indefinitely while SENDING
chunk10/16 (782,338 KB). USB inventory still showed Android serial<device-serial>; user confirmed fastboot
screen. Host fastboot PID92408 was stopped with SIGTERM (no competing device command issued).
User asked to restart fastboot and replug. Retry must be a FULL flash of the same verified v7 image
with `fastboot -s <device-serial> -S 256M flash userdata .../lisa-ios-gl-v7.simg`. Do not boot the partially
flashed filesystem. After successful retry, user must restart fastboot/replug again before boot v17.

### v18 kernel prepared during the flash retry
The 256 MiB retry is progressing (diagnostic/gl-v7-flash-256m.log).
A separate animation-performance bug was found in bsd/dev/lisa_cpufreq.c: a saturated UI/render core
was averaged with idle cores in its clock domain, so e.g. one busy of four cores looked like25% and
selected only65% of the allowed frequency. The governor now chooses from the busiest core; logs show
both peak and mean. Idle scaling, existing EPSS rows/caps, thermal firmware and the100ms interval remain.
Built successfully, retained the phystokv 64-bit fix (disassembly verified). New intended boot image:
`diagnostic/out-ufs-xnu/lisa-ios-gl-v18.img` (gpu_cached=5, glblit=1, CPU governor fix), structurally valid.
Use v18 after successful userdata v7 flash, rather than the earlier v17 image. Build log compressed at
`diagnostic/gl-v18-build.log.gz`. Userland v7 is unchanged by this kernel-only change.

### Flash retry succeeded
Full v7 flash with `-S 256M` completed: all46 sending/writing pairs OKAY,331.975s,exit0.
The earlier stalled large-transfer attempt has now been fully overwritten. Exact log:
`diagnostic/gl-v7-flash-256m.log`. Prefer256MiB sparse chunks for future full flashes.
User asked to restart fastboot/replug after this success; next action is product check + temporary
boot of `lisa-ios-gl-v18.img`. New UI/app/wallpaper result still pending.
Post-package mount validation: all32 moved executable hashes match; no staged duplicates remain;
PosterBoard/assetsd cache jobs include the exception observer. iOS16 wallpaper assets include HEIC
images; if decoding errors appear, investigate ImageIO/software codec path before modifying assets.

### v18 temporary boot accepted
After successful v7 userdata flash and the user's fastboot restart/replug, product=lisa check passed;
`fastboot boot diagnostic/out-ufs-xnu/lisa-ios-gl-v18.img` send/boot both OKAY (0.613s).
User is checking stock-app icons, wallpaper, animation/flicker, then returning to recovery for logs.
Pending next pull: `bash lisa-tools/pull-lisa-log.sh <device-serial> lisa-tools/diagnostic/gl-v18-recovery.log`.
Look for `LISA EXCEPTION`, mirrored-target attachment/panel-present lines, GL errors/timeouts and the
new CPUFREQ peak/mean frequency decisions. v16 remains best confirmed until this device result.

## 9. v18 black display; mirror sampling fix built; v19 control boot (latest)

User reported black/panic and returned recovery. Pulled `diagnostic/gl-v18-recovery.log` (231,708 bytes,
flush9). Actual log shows NO kernel panic / GPU timeout: SpringBoard/backboardd and input stayed alive;
mirrored targets and GPU panel draws logged, >80 frames. Governor fix confirmed by log e.g. group0
peak100%(mean25%) ->1804MHz, while idle drops to300MHz; big/prime also scale correctly.

Two concrete mirror-path errors then found in eagl.c:
- Newly created imported mirror texture was never given sampling parameters. Default mipmapped min
  filter makes a single-level texture incomplete when panel shader samples it, silently yielding black.
  Now set MIN/MAG=NEAREST and MAX_LEVEL=0 in surface_mirrored.
- GPU full-surface seed/copy blits inherited CA's scissor. Now disable/restore scissor around both blits.
Built successfully in `/tmp/lisa-gl-v19-userland-build.log`; corrected GL lib is ONLY in lisa-gl/out,
SHA256 67fe7d8c421abc1ef57e4944e8ba7106d7577e6503433e52d645cde699035382. NOT YET on phone/master!
Phone/master still userdata v7 (old GL lib 7aa6a13d..., apps already moved). Do not claim this fix tested.

Wallpaper observer output was absent because stderr-to-ring defaults only backboardd,SpringBoard.
Added sweetnull=backboardd,SpringBoard,PosterBoard,assetsd,lsd,installd in the next control boot (565-byte
cmdline, within608). NSException library had already been scoped into PosterBoard/assetsd launch jobs.
Control boot `diagnostic/out-ufs-xnu/lisa-ios-gl-v19-control.img`: same v18 kernel and governor fix,
but glblit=0 (working in-place GPU path), plus sweetnull. Fastboot product check/send/boot all OKAY.
User is checking whether apps/wallpaper appear, then returning recovery for log. Pull as
`diagnostic/gl-v19-control-recovery.log`. Diagnose LISA EXCEPTION reason before preparing the next full
userdata image, so wallpaper and mirror fixes can be combined in one update. DO NOT just flash v7 again.

Temporary mass-storage experiment to avoid full flashes:
- Recovery CONFIG_USB_CONFIGFS_MASS_STORAGE=y; adding function mass_storage.lisa beside ffs.adb worked.
- Raw userdata /dev/block/sda33 (113254576128 bytes) exported over USB, but macOS saw no mountable HFS.
- UFS sectors4096. A512-sector loop limited to24GiB exported, but EVERY read from loop-backed blockdev
  returned EIO (raw sda33 reads work). Removed loop. Direct raw + hdiutil CRawDiskImage also unmountable.
- Host ejected exports, recovery restored to ADB-only, no loop mappings remain. No filesystem writes
  occurred through this experiment. Do not retry loop-on-block without solving its EIO.
- Guarded device-side script `lisa-tools/recovery-userdata-usb.sh` retained for research (raw export).
  It uses shell mountinfo parsing, NOT awk (recovery has no awk); echo newline to UDC/file, NOT zero-byte
  printf (sysfs requires a write). Temporary recovery copy is gone when booted. No boot/vendor writes.

If another full update needed: build.sh outputs are now the corrected GL lib; GLVER=v8 deploy-stage.sh
will install it into the master, keep all32 moved apps (layout helper idempotent), include exception
observer, produce/verify full simg. Flash with -S256M only, then USER fastboot restart/replug before boot.

## 10. v19 evidence; v20 app/widget/wallpaper fixes; v8 update (2026-10-26)

User reports waiting widgets, missing wallpaper, Music and some Settings pages crash,
flicker/lag. Connected recovery log pulled to `diagnostic/gl-v19-control-recovery.log`
(527439 bytes, flush31). UI has genuine Adreno GLES submissions/present, not CPU-only;
frame times vary ~8–50+ms. No claim of hardware Metal composition.

New linked repo inspected/cloned: `research-abhi-ios-lisa`, commit
1faa64d166a8b9cc5ffab876da4d7f8b87b17f44. Its README explicitly has no demonstrated
accelerated UI or working Wi-Fi/BT to import. Useful import diagnostics, same upstream.
Detailed findings: `lisa-tools/UI-FOLLOWUP-2026-10-26.md`.

Concrete fixes built:
1. `bsd/kern/sweet_amfi.c` policy was named SWEET AMFI and lacked policy_syscall.
   Music's embedded MusicApplication.framework exists but dyld refused @rpath expansion.
   Rename mpc_name to AMFI (libamfi ABI), implement request0x5a with flags0xff and
   request0x60 with0x86 (ExtensionKit's non-enforcing state), exactly HackiOS's policy
   convention. Existing signature/page checks/entitlements preserved. This enables
   DYLD_INSERT_LIBRARIES which the earlier exception observer never actually loaded.
2. Sandbox op4 now reads the actual CFFIXED_USER_HOME from the target's exec environment,
   using the HackiOS task-map copy implementation plus ARG_MAX bound/PID-version check.
   No fabricated shared container. Needed by PlugInKit/widgets. Other token ops unchanged.
   Reapplicable patch `patches/0095-lisa-amfi-dyld-widget-policy.patch`.
3. PosterBoard exception traced with actual disassembly: `_imageForView:` creates a Metal
   command queue with NO device (GL mode) and inserts nil into NSDictionary objects[1].
   New `lisa-gl/src/poster.m`, built liblisaposter.dylib, scoped into PosterBoard ONLY,
   replaces that still-preview method with CoreGraphics layer.renderInContext.
   Main backboardd remains GPU GLES. Static snapshot fallback does not promise every
   wallpaper layer/effect is supported. assetsd observer remains lightweight Foundation.
4. Buffering fixes from section9 now DEPLOYED into local master: mirror texture sampling,
   scissor disable/restore for full-surface seed/copy. Previous frame eligibility and
   completion fixes retained. New boot glblit=1, existing peak CPU governor retained.
5. Preferences Storage SIGTRAP symbolicates to PodcastsUI force-unwrapping nil
   MPMediaLibrary.databasePath. Local master lacks Media/iTunes_Control parents; create
   iTunes_Control/{iTunes,Music} (0750) for medialibraryd. No fake DB/storage capacity.
   Whether this alone fixes Settings needs a device result. About crash not separately
   captured. Added Preferences,medialibraryd to sweetnull for evidence.

Kernel build succeeded (correct cwd darwin-xnu-build), signatures verified, source syscall
implementation confirmed in actual kernel disassembly (0x5a/0x60;0xff/0x86). 64-bit pmap
PA fix retained (ubfiz x0,x0,#14,#32), all8 CPUs, 4GiB bank unchanged.
One intermediate build invoked from wrong cwd failed availability.pl/header setup; DO NOT
use it. Successful log `diagnostic/gl-v20-build.log.gz`, userland build log likewise saved.

New image `diagnostic/out-ufs-xnu/lisa-ios-gl-v20.img`, payload `lisa-ios-gl-v20-payload.bin`:
11 segments,591-byte bootargs, glblit=1, updated kernel. SHA256 image
7cf384d8ca0f803c5b8b4eeb1c052cf0ca6e91a12f6c9aafc23aa473a3a23ae8.
Full userdata `ios-artifacts/lisa-ios-gl-v8.simg`, expanded25769803776 bytes, independently
verified SHA256 ed74b817d97991f51acc97368286d1a908a97de377a15c588f41962750e2fb30.
Mounted package checked: exact new libraries, PosterBoard env has observer+poster libs,
assetsd has observer only, media parents and173 system app bundles present.

GL hash67fe7d8c421abc1ef57e4944e8ba7106d7577e6503433e52d645cde699035382.
Poster lib hashf73165aeb45821878406f59296ccb208e0105dd94fcbb1eab36becf408617fa4.
Observer hashc5f7d75309429b1cfeb4a75e51ec384144e5cd7502250b498d375a013ccf64b9.

Flash v8 currently IN PROGRESS via -S256M after product=lisa/size0x1A5E7FB000 check;
log `diagnostic/gl-v8-flash-256m.log`. All transfers so far acknowledged; must confirm
full46/46 exit0. Then ask USER to restart fastboot/reconnect BEFORE temporary v20 boot,
per their explicit instruction. Do not issue boot during the stale post-flash USB session.
After boot: check wallpaper,widgets,Music,Settings Storage/About,flicker; then recovery log
as `gl-v20-recovery.log`. No Wi-Fi/BT/hardware brightness implementation claimed.

### v8 transfer interruption and full retry
First v8 flash acknowledged1–44/46 then USB disconnected while sending45
(error e00002ed, hostexit1). User then reported Connected in fastboot.
Fresh product=lisa request answered. Started a FULL -S256M retry, log
`diagnostic/gl-v8-flash-256m-retry.log`; do not boot until all46 succeed.
The prior incomplete image is not considered usable. An offline v20-control
image (same new kernel/policies but glblit=0) is prepared if buffering still
fails; it needs no additional userdata flash and preserves all other fixes.

Local v8 filesystem additionally passed a forced read-only HFS check (`fsck_hfs -fn`,
exit0, iOSRoot appears OK); full catalog/extents/bitmap check rather than the
journaled-volume early exit. Evidence `diagnostic/gl-v8-full-fsck.log`.

### v8 FULL RETRY SUCCEEDED
`diagnostic/gl-v8-flash-256m-retry.log`: all46 sending/writing pairs OKAY,
Finished332.311s, hostexit0. First incomplete attempt fully superseded.
User asked to restart fastboot/reconnect after flash. NEXT: after that reply,
product=lisa check and `fastboot -s <device-serial> boot` the v20 image. No further
userdata flash needed. UI/wallpaper/widget/Settings outcomes not yet known.

### v20 boot accepted
User restarted fastboot/reconnected. product=lisa answered; temporary boot
`lisa-ios-gl-v20.img` sendOKAY0.391s/bootOKAY0.219s, total0.617s,exit0.
User asked to check wallpaper/widgets/Music/Settings About+Storage/flicker,
then return recovery withADB. Pull next as `diagnostic/gl-v20-recovery.log`.
Check AMFI registration, LISA POSTER fallback installation/previews,
LISA EXCEPTION, embedded Music framework loads, PlugInKit extension/container
messages, Preferences/medialibraryd errors, mirrored GLES timing and GPU timeouts.
No device UI result yet; do not claim wallpaper/widgets/settings fixed.

## 11. v20 device result and v21 / userdata v9 (latest)

User: still Storage crashes, some apps fail, rendering lag/input stalls/flicker,
no brightness, asks full RAM/Wi-Fi/BT and clipped side borders fixed.
Pulled `diagnostic/gl-v20-recovery.log` (832226 bytes,flush54). Findings:
- Main GLES ran >7500 frames,0 GL errors, ~21ms cumulative mean. Mirrored path
  now shows UI; still too slow/tearing, not a fully resolved graphics pipeline.
- Music's missing-framework crash is absent in this run (AMFI path fix progress).
- Preferences Storage SAME PodcastsUI nil databasePath force-unwrap.
- PosterBoard SAME nil Metal queue dictionary crash. No LISA POSTER/EXCEPTION
  startup lines: launchd job environment insertion is not reaching this app.
- assetsd repeatedly aborts: `assetsd requires TCCPhotosService access`.
  Its signature DOES contain com.apple.private.tcc.allow [Photos,AddressBook].
  Do NOT blanket-grant TCC or suppress all assertions; investigate tccd/entitlement
  IPC. AegirPoster also repeatedly aborts (not a working Metal wallpaper).

Verified recovery memory: MemTotal7447304kB (~7.10GiB Linux-managed RAM).
`diagnostic/gl-v20-recovery-iomem.txt`, meminfo alongside; parsed ranges in
`memory/lisa-recovery-20261026-ranges.json`. The current4GiB XNU span already
contains the high2GiB bank; much additional free RAM is BELOW gPhysBase.
Old memory plan's 1792MiB limit is STALE. Crucially the allocator had no hole
exclusion even for firmware tail0x1f8400000..0x200000000. v21 excludes this
124MiB from pmap free-page count and skips it in pmap_next_page. No more total
RAM was enabled; full low-bank support still requires sparse physmap/metadata.
Never just inflate dram-size across reserved memory or shift gPhysBase without
updating kernel VA mapping and pmap metadata indexing.

v21 changes BUILT/PACKAGED:
1. GPU present bilinearly fits the whole1125x2436 canvas into1080x2400 instead
   of centered cropping. HID normalizes x/1080,y/2400 across the entire canvas.
   CPU full-canvas fallback also scales; partial-layer clipping unchanged.
2. Functional SOFTWARE dimming adapter (NOT CSOT DCS hardware brightness):
   `lisa-gl/src/brightness.m` hooks backboardd BKDisplayBrightnessController
   setDisplayBrightness:permanently: only if runtime ABI is void,float/double,BOOL.
   Original setter/state/notifications retained. Writes root-writable sysctl
   kern.lisa_brightness20..1000; GLES samples at most10Hz and multiplies finalRGB.
   LisaMetal constructor dlopens adapter explicitly before GLES. Runtime hook
   availability/slider behaviour still unverified. Panel hardware brightness
   remains recovery2047/4095 (read-only observed), no DSI/PMIC writes implemented.
3. Poster fallback is now a NORMAL LC_LOAD_DYLIB in PosterBoard's executable,
   so it does not depend on RunningBoard inheriting a launchd DYLD environment.
4. New `src/preferences.m`, linked normally into Preferences, guards ONLY the
   captured PodcastsUI storage-capacity query: when original databasePath=nil
   AND caller lies in verified iOS16.7.16 function1cdc47c58..1cdc47dac, use the
   existing /var/mobile/Media directory for volume lookup. No fake SQLite DB
   or capacity; other database consumers unchanged. This is a scoped workaround,
   not repair of medialibraryd. Check LISA STORAGE log to see whether callsite
   guard hit and whether Storage becomes usable.
5. assetsd has exception observer normally linked for next evidence.

`lisa-gl/link-compat.py` adds LC_LOAD_DYLIB using ZERO existing header padding
only. Extracts existing XML entitlements and signature identifier, backs up
executables under ios-artifacts/v8-executable-backup, re-signs with unchanged
entitlement dict/identifier, verifies signatures and exact preserved entitlements.
Two-run temporary-copy test passed for initial targets (including idempotence);
actual stage link validated all3 targets, manifest `diagnostic/gl-v9-compat-linked.json`.
Unlike earlier app relocation, these3 executable hashes intentionally change.

New boot `diagnostic/out-ufs-xnu/lisa-ios-gl-v21.img`,595-byte bootargs,
11segments, configured span4GiB, excludes firmware VM pages in kernel.
ImageSHA256124f493085f719e45cd14768dcb78ac1186e5a8196ed615e1c2b6de9338c93ce.
Added tccd,AegirPoster to sweetnull, removed lsd,installd from it to fit.
New full userdata `ios-artifacts/lisa-ios-gl-v9.simg`, verified25769803776bytes,
expandedSHA256f96bd63d7af17e89bb5197245006300719ced6548968db1bfe42c2e31d3f221b.
GLhash6e33e0312849d1b4712d4e3d58ee63815c3e4eaaf26f3f7c863b9351775c8008.
Brightnesshash1625e6c6b20c995096b82912c2f7463c706ef29ebd397bd40de9addb18c10227.
Preferenceshash009537438420772af6b93b852fd1d16fbabff39beaa6484c95fb989e7d2cb644.

v9 FULL FLASH IS CURRENTLY RUNNING: diagnostic/gl-v9-flash-256m.log, -S256M.
User replied Connected in fastboot BEFORE flash started (avoid replug race
fromv8). Confirm46/46exit0 then USER restart/replug before temporaryv21 boot.
User has not yet tested these changes. Do not claim brightness/Settings fixed.

Radio research during transfer: independent Abhi DTB confirms WCN6750 Wi-Fi
and BT, WPSS0x9ae00000+0x1900000, PAS6, SMEMremote13, IPCC24,
SMP2Pitems0x269/0x268, BluetoothGPIOreset85/swctrl86/WLANreset84,
UARTalias0x99c000. Matches upstream redwood_wifi.c's primary constants.
No actual Wi-Fi or BT driver linked/enabled in v21. Local vendor checkout
search did not find wpss.* / bdwlan board files; next recovery should inventory
/vendor/firmware_mnt/image and firmware partitions read-only. Preserve board
calibration/persist; do not dump secrets or write radio partitions. Full radio
bring-up also needs the ath11k transport/QRTR and iOS network interface, plus
separate HCI stack for BT. Source reference is research-hackios-kernel/wifi/lx.

2026-10-26 Codex continuation: see HANDOFF-CLAUDE-TO-CODEX.md newest entry.
Pulled v26 recovery log; corrected 6G DSI offsets/software trigger, narrowed
brightness hooks, added screen-off compositor/governor policy; pending build/test.
No radio/Metal/90Hz/full-suspend success claimed. No phone flash yet this turn.

v27 built and validated, boot SHA256807c3876b84c8445e69f451d476af0dcb50c5e4bc00b176e99c1b13aa8dee755.
v13 userdata expandedSHA256a3027a747d7c812a63c1ebef16c22d5207517087a25309188517b6a503f013a6.
Flash in progress gl-v13-flash-256m.log after user-confirmed fastboot reconnect.
Added build-lisa-kernel.sh and build-lisa-wlfw.sh as reproducible local entry points.
Full ath11k object compilation started separately; not part of v27 yet.

v13 flash completed46/46 in333.189s; user restartedfastboot/reconnected; v27
boot accepted0.614s. Awaiting on-phone brightness/off-wake/heat report/recovery.
v28 real ath11k kernel link also succeeded. Matching unstrippedkernel archived;
bootimagev28-wifi packaged, notbooted. See main handoff for options and hashes.

2026-10-26 nextcontinuation:v27hardwarebrightness+WPSS/GLINK/QRTRconfirmed.
v29+v14built/verified withADSPbattery,fullath11k,fadefix,sleeppolling/UFSwait,
CSOT90Hzrequest+IOMFBcadence; untestedonphone. VMDvisualvoicemailCPUburntemporarily
Disabledinuserdata. Full details/hashes in HANDOFF-CLAUDE-TO-CODEX.md.

2026-10-26: v14 userdata flash succeeded 47/47, 341.917 s. User restarted
fastboot and reconnected; v29 temporary boot accepted, 0.625 s. Awaiting
on-phone Wi-Fi/battery/wake report and recovery ADB log; no repeat boot needed.

2026-10-26: v29 recovery log confirms battery live and ath11k radio up. v30 kernel + v15 userdata built/verified: Wi-Fi daemon Apple DT gate removed; battery events refresh promptly; USB ECM adapted to Lisa clocks/exclusive SMMU; blur function dispatch corrected; O2 renderer/less console traffic. Not yet phone tested. See latest main handoff and diagnostic/gl-v30-artifacts.json. Camera module tables extracted; camera/torch/BT/cellular/sensors/native Metal unfinished.


## 2026-10-27 native Metal v17 (supersedes historical state above)

Phone returned in Lineage recovery after v30: user still sees lag/rendering errors.
Pulled diagnostic/gl-v30-recovery.log (828080 bytes, flush62). Native Metal is globally
suppressed by lisagl=1; GLES on Adreno is active. No observed GPU/kernel panic in that log.
Implemented real ordered GPU buffer fill/copy, retained buffer/device lifetime, viewport
and scissor state with default-state restoration, serialized backend loader initialization.
Native test verifies bytes and actual stock AIR shader pixels; only after all tests pass
does it opt AegirPoster into native Metal. System compositor stays GLES pending evidence.
Build SUCCESS. v17 full userdata sparse verified: expanded24GiB SHA256
0a55ef4e6c08e6ef1d4222af30b4baa338facf6d5a7df1aa4d898c46c6025419.
Kernel unchanged: diagnostic/out-ufs-xnu/lisa-ios-gl-v30.img.
Manifest diagnostic/gl-v30-metal-v17-artifacts.json. NOT hardware tested yet.
Full source/build/test details and immediate steps: HANDOFF-CLAUDE-TO-CODEX.md newest entry.
Awaiting recovery->fastboot reconnect before flash. After flash, user must restart fastboot
and reconnect before temporary boot. Let run3minutes, recoveryADB, pull guarded log and
search LISA METAL TEST. Do not claim complete Metal acceleration from build or service logs.

v17 FLASH SUCCESS:47/47,341.737s,fastboot exit0. Await user restart fastboot/reconnect, then temporary v30 boot. No boot partition writes. Native Metal test still pending.

User restarted fastboot/reconnected. Temporary v30 boot accepted exit0 with v17 userdata; log diagnostic/gl-v17-v30-boot.log. Awaiting3minute native test and recovery log.


## 2026-10-27 v18 native Metal / launchd cache correction

User returned recovery after v17: lag/render errors, CC brightness failure, remaining
Wi-Fi/Bluetooth/audio/cellular/sensors missing. Pulled diagnostic/gl-v17-metal-recovery.log:
347287bytes,flush25. NO native test launch/exec/PASS markers. This was NOT a native test
failure: launchd_cache_loader reads /System/Library/xpc/launchd.plist. Standalone job edits
were ignored. Cached wifid still had _LimitLoadToDeviceTree and cached voicemail.vmd still
ran. Critical correction: native test must be added to consolidated LaunchDaemons dictionary.

Implemented:
- lisa-gl/sync-launchd-jobs.py updates ONLY the three explicit changed jobs from their
  standalone plists into consolidated cache, verifies labels and serialized results. Preserves
  all other cache sections/jobs. deploy-stage.sh runs it after other cache/app layout repairs.
  Real mounted cache verified: test RunAtLoad/env1 present, wifid Apple DT gate absent, vmd
  Disabled true. This fixes loading, not Wi-Fi association by itself.
- Metal GPU colour texture-region blits via rwa_texture_copy -> Gallium resource_copy_region.
  Records resources/regions until commit; validates device, mip/slice/coordinates/format,
  rejects overlapping ranges, multisample/depth/compressed formats. Fixed unsigned-addition
  overflow in shared texture_box bounds checks. Textures retain owner device through destroy.
  Contract mirrored by build-lisa-air.sh; original draw ABI unchanged.
- Native smoke now reports launch/open stages, checks buffer blits, native AIR draw, viewport/
  scissor/default reset, and actual texture-copy region pixels before opting in AegirPoster.
  Global UI remains GLES; complete Metal API/UI is NOT established.
- Control Center: actual iOS16.7 DisplayModule ObjC metadata saved in diagnostic/
  gl-v18-displaymodule-objc.txt. CCUIDisplayModuleViewController _setBacklightLevel: is its
  separate BrightnessSystemClient path. brightness.m hooks exact float/double ABI after runtime
  validation, preserves original and publishes user level to kern.lisa_brightness. Loaded in
  SpringBoard as well as backboardd; NSBundle load notification and bounded retries cover lazy
  module load. _canAdjustBrightness only adds YES when kernel's real DCS backend reports1.
  Last log showed backboard setters and DCS sends working, including DBV2047; CC needs this
  separate bridge. Needs on-phone slider verification, no claim yet.
- FDT manifest fixed: GPIO cells were misread as empty strings. BT reset now verified numeric
  [0x3c,0x55,0x0]. Expanded lisa-peripherals.json47nodes with audio/UART. Lisa actually uses
  TFA98xx amps at /soc/i2c@98c000 addresses0x34/0x35 (resetGPIO2/6, IRQ102/24, switch101),
  active WCD938x codec; WCD937x disabled. POCO redwood_speaker.c is AW88261, incompatible
  register programming. ADSP battery transport works but PCM/amp path still absent.
  BT WCN6750 HCI transport, modem/CommCenter, SSC sensors still NOT implemented.

Build /tmp/lisa-metal-v18-build.log SUCCESS (existing ObjC/deprecation warnings). Full v18
sparse staging verified24GiB expanded hash107567aee2dfc30ac3ef55cfa1e3d3760bed3c7f74e21048ec40a4c44ea0324b.
Manifest diagnostic/gl-v30-metal-v18-artifacts.json. Deleted ONLY unused unflashed v16 sparse
to free12GB; retained v17/master DMG. User reconnected fastboot; product=lisa; v18 flash now
running256MiB transfers, log diagnostic/gl-v18-flash-256m.log. Same tested v30 kernel.
After flash must ask user restart fastboot/reconnect before temporary v30 boot. Then >=3min,
recoveryADB, retrieve guarded tail. FIRST require native test actually launched; do not mistake
missing output for driver refusal. Look for GPU/pixel failure reason, wifid IOCTL requests, CC
backlight hook and changed DCS level, plus absence of vmd exec.

v18 flash SUCCESS47/47,341.418s,exit0. Asked user restart fastboot/reconnect before temporary v30 boot; awaiting reply.

v18 temporary v30 boot accepted after user restart/reconnect, fastboot exit0. Asked >=3min run, Control Center brightness and Wi-Fi network-list checks, then recoveryADB. Native Metal results still pending; next pull diagnostic/gl-v18-metal-recovery.log.


## 2026-10-27 v19 / kernel v31 continuation

User confirms Control Center brightness WORKS on v18. Still laggy; white dot/log near
bottom. Wi-Fi page CRASHES; Control Center Wi-Fi GREYED (explicit clarification).
Other Bluetooth/audio/NFC/cellular/sensors still unavailable. Pulled recovery root log:
diagnostic/gl-v18-metal-recovery.log420509bytes,flush30.

Native test NOW LAUNCHES (exec and launched marker), but killed before opening device:
CRASH lisa-metal-smoke reason ns1 code7 (SIGKILL). Local XNU reason.h namespace1=JETSAM,
kern_memorystatus.h code7=MEMORY_PERPROCESSLIMIT. Stock D22/D221 Version4.Daemon.Override.Global
limits active/inactive to6MB. No GPU PASS or GPU failure result was produced.
Fix: only dev.lisa.metal-smoke gets bounded512MB active/inactive/soft profile overrides and
JetsamProperties.JetsamMemoryLimit512; deploy-stage.sh preserves global limits. Test queries
and, if needed, sets its OWN512MB limit via memorystatus_control syscalls8/7 before Foundation
allocation. Logs actual current limits and set result. GPU checks themselves unchanged.

Kernel console bug: kprintf -> PE_kputc -> fbcon_transmit_data bypassed _cnputs' mute and
could still draw glyphs over the GPU frame. pe_serial.c now checks sweet_console_muted
at the lowest framebuffer write point; panic_active still draws. Normal guarded ring logging
unchanged. This fixes a real overlay path; user must confirm whether their mark disappears.
Preferences compatibility library now dlopens scoped exception observer so actual Wi-Fi
page exceptions can be captured; no exception behaviour/driver results are spoofed.

Wi-Fi: v18 cache correction makes wifid execute and call actual Apple80211 IOCTL bridge.
No scan10 request seen. Unsupported96/97/106,PHY14,country51, plus repeated set383(4104bytes).
Do NOT blindly ACK383 or invent its ABI. Queried actual iOS16.7 IO80211 library metadata:
diagnostic/gl-v19-io80211-symbols.txt, gl-v19-scanasync.dis.txt,gl-v19-setioctl.dis.txt.
Apple80211ScanAsync still creates genuine5456byte scan10;383 falls through generic raw CFData
set path. Its meaning remains unknown. No Preferences CRASH stack in captured log, despite
several execs; scoped ObjC observer added to get actual page error.

Native/userland build /tmp/lisa-metal-v19-build.log SUCCESS. Kernel console update built
with LISA_WIFI_DRIVER=1 bash lisa-tools/build-lisa-kernel.sh (/tmp/lisa-gl-v31-kernel-build.log),
SUCCESS. kernel.gl-v31.unstripped saved for SYMBOLS ONLY. Use stripped kernel.gl-v31.macho
for repack: unstripped __LINKEDIT overlaps firmware (packer correctly rejected it).
Payload repack11segments validated. v31 image17,920,000bytes, RAM/ADT/bootargs/shim/firmware
verified unchanged from v30 (4GiB span); all GPU firmware byte-identical.
Manifest diagnostic/gl-v31-metal-v19-artifacts.json.

v19 full sparse staging24GiB hash25b9e6f6f69d5da9a959db5cb53c8e878a41e86957c024eb207a3581b2dc846f,
verified RAW/FILL expansion. Deleted ONLY superseded generated v17 sparse to free12GB;
v18 and master DMG retained. Resolved local staging mount busy by closing obsolete ipsw
symbol readers; no phone impact. Avoid ipsw dyld symaddr broad lookup (expensive); use bounded
ipsw dyld macho --symbols per library. Temporary cache overlay /private/tmp/lisa-dyld-v19.

User reconnected fastboot; product=lisa. v19 flash started256MiB chunks, log
diagnostic/gl-v19-flash-256m.log. After success ask fastboot restart/reconnect, then boot
diagnostic/out-ufs-xnu/lisa-ios-gl-v31.img. Run >=3min; native device/buffer/pixel checks
now need to finish. Preserve working CC brightness. Full Metal UI and remaining radios,
audio/NFC/cellular/sensors still NOT complete.

Final v19/v32 boot accepted exit0 after user restart/reconnect. Artifact manifest
diagnostic/gl-v32-metal-v19-artifacts.json; bootlog diagnostic/gl-v19-v32-boot.log.
v32 includes real country-query strong symbol (nm-m: external __TEXT_EXEC,__text,
not weak fallback). ath11k0stubs.11segments validated; RAM4GiB span/ADT/bootargs/shim/
196608byte firmware unchanged. Asked user >=3min run, open Wi-Fi Settings once and note
white mark, then recoveryADB. Next pull diagnostic/gl-v19-metal-recovery.log.
FIRST read LISA METAL TEST memory limits/device/blits/pixels result. Scope512MB to testonly.
User-confirmed Control Center brightness works on v18; preserve it. Other drivers incomplete.


## Final collection for Claude — 2026-10-27
Read `/Users/arjun/IOS-LISA/CLAUDE-NEXT-2026-10-27.md` FIRST; it supersedes earlier pending-result notes. Fresh v19 decoded log423254bytes,flush30 (ring header total423715). Metal limit512MB confirmed, but no opening-device/PASS/FAIL marker or native-test crash recorded. Country get51 now answered; Wi-Fi not working. User says white mark, rendering lag and Night Shift failure persist. Phone remains recoveryADB; no additional flash/boot/source edits in final collection turn.
