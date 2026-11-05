# Lisa iOS port — handoff from Claude to Codex (2026-10-26)

**Latest final collection 2026-10-27: read `CLAUDE-NEXT-2026-10-27.md` FIRST.
Fresh v19/v32 recovery log collected. Native test confirms512MB allowance but stops reporting
before GPU opening; no native rendering PASS/FAIL result. Country query51 now succeeds,
Wi-Fi still fails. White mark, lag/render errors and Night Shift failure persist. User is
switching to Claude and requested collection/documentation only; phone left in recovery.**

Read this first, then LISA-HANDOFF.md (dated log) and lisa-tools/METAL-STATUS-2026-10-26.md.
The phone is a Xiaomi Mi 11 Lite 5G NE (`lisa`, SM7325 / Snapdragon 778G, Adreno 642L), serial <device-serial>.
Upstream reference: HakiOS (github.com/oosexgang/hackios-xnu-kernel, being renamed hakios-os) runs iOS 16.5
on the POCO X5 Pro `redwood` — THE SAME SoC. Its drivers are the first place to look for any SoC block.

## 1. Earlier baseline (historical; newest continuation and summary above take precedence)

| Item | State |
|---|---|
| iOS 16.7.16 boots to lock/home screen | works |
| GPU UI: CoreAnimation OpenGL ES renderer on the Adreno (Mesa freedreno) | **works**, ~30 Hz, some layers wrong (Control Center glyphs white) |
| GPU present (GPU draws frames straight onto the panel, no CPU copy) | works (`lisa_gpu_present=1`) |
| Metal (Codex's lisametal driver) | renders on the GPU without hangs, but almost no frames reach the screen: black |
| Touch, power, volume-down, **volume-up** | work |
| All 8 CPUs; CPU clocks raised (EPSS) + load governor | work (governor new in v12, verify heat) |
| Icons on home screen | work (client-memory IOSurfaces implemented) |
| Missing apps / wallpaper (PosterBoard crashes: uncaught ObjC exception) | open |
| RAM: only the 4 GiB bank at 0x180000000 | open (multi-range physmap needed) |
| Brightness, Wi-Fi, BT, NFC, audio, sensors, USB, real battery | not done |

Current best boot image: `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v13.img` (GL + GPU present +
lisacpufreq=2 governor + gpu_cached=1). v12 (gpu_cached=3) HUNG the GPU: per-submit cleaning is required.
Fallback: `lisa-ios-gl-v11.img` (gpu_cached=1, lisacpufreq=1). Metal test image: `lisa-ios-metal-v21.img`.
Userdata currently flashed: built from `ios-artifacts/lisa-ios-uncompressed-metal-stage.dmg` (the MASTER image).

## 2. Bugs found and fixed today (several were Codex regressions — do not reintroduce)

1. **GPU hang (also Codex's Metal "ring stall"): GEM buffers are cacheable, never cleaned.**
   Codex moved GEM allocations into the client task (pageable) to avoid a low-pool panic, which made them
   ordinary cacheable memory, but left the default `gpu_cached=0` path, which skips cache cleaning
   (`LisaAdrenoUserClient.cpp`, submit loop). The GPU executed stale/zeroed shader memory: CP stops at
   CP_WAIT_FOR_IDLE, RBBM_STATUS 0xe59085 (SP/HLSQ/VFD busy), no SMMU fault.
   RULE: boot with `gpu_cached=1` (clean every CPU-mapped buffer each submit). TESTED and HANG (status 0x800005):
   `gpu_cached=3` (allocation-only clean, v12) and `gpu_cached=4` (kIOMapWriteCombineCache in
   clientMemoryForType + no submit clean, v14) — a WC *mapping* request does not make pageable task memory
   uncached. `gpu_cached=5` (v15, being tested): pmap_set_cache_attributes(page, VM_WIMG_WCOMB) on every GEM
   page at allocation, reset to VM_WIMG_DEFAULT at every release path (lisaSetPagesWimg), no submit clean.
   To make it faster, make the user mapping really write-combined, or clean only dirty ranges.
2. **Cache maintenance on user VAs panics.** `b->kernel` for GEM buffers is a USER address
   (`getBytesNoCopy()` of a task buffer). `dc civac` on it faulted (esr 0x96000147). RULE: clean GEM memory
   by physical page via `lisaFlushPhysical()` (phystokv). Only surfaces have kernel VAs.
3. **IOSurface layout mismatch.** `surfaceBuffer()` assumed upstream MiniIOSurface (pixels one page after a
   shared-state page). SweetSurface pixels start at 0 -> GPU view shifted 4 KiB and over-ran. Fixed with
   `MiniIOSurface.pixelStart` (= 0) + bounds clamp. RULE: never hardcode PAGE_SIZE offsets for Sweet surfaces.
4. **'b3a8' screen surfaces.** CoreAnimation's GPU renderers ask for 'b3a8' (10-bit XR + alpha plane).
   Upstream substitutes one-plane BGRA (also w30r/l10r/w30R). Added to `SweetSurface.cpp`
   (`lisa_surf_bgra=0` disables). The panel is 8-bit BGRA.
5. **IOSurface selectors 6/7 answered "success" with a zeroed result** -> GLES crash
   (IOSurfaceClientGetBulkAttachments, far 0x30). Now kIOReturnUnsupported, as upstream.
   RULE: unknown IOSurface selectors that return a lock result must FAIL, not succeed with zeros.
6. **Client-memory surfaces (IOSurfaceAddress) got blank new memory** -> icons missing. Now wrapped
   (withAddressRange + prepare, complete() in free). GPU import refuses wrapped surfaces (not page-aligned).
7. **Metal driver looked for its compiler at `/lisa/lib/libredwoodair.dylib`** (sed rename artefact, with
   RTLD_NOLOAD so it silently failed) -> no drawable ever -> black. Now `/usr/local/lib/lisa/liblisair.dylib`.
   RULE: after any redwood->lisa rename, grep for "/lisa/lib" and "/redwood/".
8. **Metal texture-layout copies assumed 4 bytes/pixel** -> SpringBoard crashed in ASTCTwiddler (memmove
   past the source). Now block-aware (ASTC/ETC/BC) with the layout's own pitch; the old format table also had
   wrong MTLPixelFormat numbers. RG8 drawables use IOSurface 'LA08'.
9. **Codex's flash/packaging hazards (keep doing what fixed them):** `repack-sweet-kernel.py` must keep the
   LISAGPUF firmware segment (fixed by Codex); never flash a DONT_CARE delta simg onto userdata (live FS
   diverges); always full-flash a verified simg made by `make-android-sparse.py`.

## 3. How the work is run (the user's Mac)

- Claude/Codex shells may be sandboxed. The user starts `bash ~/IOS-LISA/lisa-tools/agent-runner.sh` in
  Terminal; it executes `lisa-tools/agent-queue/NNN-name.job` in order (cwd = ~/IOS-LISA), writing
  `.out`/`.rc`. It is SEQUENTIAL: a job waiting for the phone blocks everything after it.
- Pitfalls hit: `source xnu-env.sh` puts GNU coreutils first (macOS `df -g` then fails under `set -e`);
  `grep -c` returns 1 on zero matches (kills `set -e` jobs); `strings | grep -q` + pipefail = SIGPIPE failure.
- USB must be unplugged/replugged after EVERY mode change (recovery <-> fastboot, and after a userdata flash
  `fastboot reboot bootloader` + replug). Product check before any flash: `fastboot getvar product` == lisa.
- Logs: boot images write a guarded ring at the end of userdata. Pull from Lineage recovery (root adb):
  `bash lisa-tools/pull-lisa-log.sh <device-serial> lisa-tools/diagnostic/NAME.log`.
- Symbolicate: `bash lisa-tools/sym-mount.sh` then
  `lisa-tools/ipsw dyld a2s lisa-tools/dyld-symbols/dyld_shared_cache_arm64 <unslid addr>`
  (unslid = address − slide from the CRASH line). Detach `ios-artifacts/inspect-root` before deploying.

### Build / package / boot
- Kernel: `cd lisa-tools/darwin-xnu-build && DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  MACOS_VERSION=13.5 KERNEL_CONFIG=DEVELOPMENT MACHINE_CONFIG=SWEET LISA_FORCE_KERNEL_BUILD=1
  KDKROOT=$PWD/../kdk-expanded/KDK.pkg/Payload bash lisa-resume-build.sh` (log is huge: gzip it).
- New boot image from a kernel: `python3 diagnostic/repack-sweet-kernel.py BASE.img KERNEL payload.bin`
  (copies BASE's command line), then `python3 diagnostic/replace-payload.py lisa-ios-gl-v1.img payload.bin
  OUT.img`. Change boot-args by editing the NUL-terminated string at payload offset 0x48..0x2a8 (see the
  python snippets in agent-queue/*.job), then validate with `diagnostic/validate-payload.py`.
- Userland (GL lib, OSMesa, LisaMetal plugin): `bash lisa-tools/lisa-gl/build.sh`, then
  `GLVER=vN bash lisa-tools/lisa-gl/deploy-stage.sh` (installs into the master dmg, writes + verifies
  `ios-artifacts/lisa-ios-gl-vN.simg`), then `fastboot flash userdata` that simg (~5.5 min). Delete the simg
  afterwards (12 GB; regenerable). Flashing resets the phone's iOS filesystem to the master image.
- Boot: `fastboot -s <device-serial> boot <img>` (RAM only; nothing else is ever flashed).

### Key boot-args (current GL line)
`gpu=1 adreno=21 adreno_keep=1 gpu_recover=1 metal=1 lisametal=1 lisagl=1 gpu_cached=3 glblit=0
lisacpufreq=2 lisa_gpu_present=1 lisakeys=1 lisatouch=1 sweetfb_hz=60 ...`
- `lisagl=1`: LisaMetal plugin hides the Metal device and loads /usr/local/lib/lisa/liblisagl.dylib into
  backboardd (CoreAnimation falls back to OpenGL ES). Remove it for Metal mode.
- `lisa_sample=1`: kernel user-stack sampler for SpringBoard/backboardd (use it to see where Metal stalls).

## 3b. Rendering hooks (added 2026-10-26 late)
eagl.c now hooks CA::OGL::GLContext::function_supported (vtable slot 0x378, 16.7.16 addr 0x1884b2164) and
function_uniform (slot 0x388, 0x1884b21ac) of __ZTVN2CA3OGL11GLESContextE (0x1d5b2f6e8) in backboardd, like
upstream inject.c: colour-matrix numbers reach eagl's own vibrancy programs, and missing CA functions are
logged ("has no function N"). Boot-arg lisa_cahooks=0 disables. Slots are replaced only if they hold exactly
those addresses (re-derive with ipsw symaddr + vtable dump if the iOS build changes).
Apps missing: App-cryptex apps are copied to /private/preboot/Cryptexes/App but iOS looks in
/System/Cryptexes/App (cryptexd mount needs Image4) — job agent-queue/0825-inspect-cryptex.job checks this.

## 4. Next steps, in order

1. 60 Hz: per-submit cache cleaning (~1.5 ms x ~280 submissions/s) dominates frame time. Options: map GEM
   buffers WC for the CPU for real (check IOConnectMapMemory cache mode on pageable task memory), allocate
   GEM from a kernel WC pool that is inside XNU's DRAM bank, or fewer submissions per frame.
2. Metal: boot a Metal image with `lisa_sample=1`; find where backboardd waits (completion handlers?
   drawable presentation?). Long-term option: port github.com/nullmoth/nvidia-macos-driver (Metal 3 plugin ->
   AIR->SPIR-V -> Mesa Vulkan) onto Turnip over the LisaGPU msm shim (PolyForm Noncommercial + LGPL).
3. Missing apps / PosterBoard: get the uncaught-exception reason (PosterBoard stderr) and check App cryptex
   registration (Safari etc. live in /System/Cryptexes/App).
4. RAM > 4 GiB: lisa-tools/memory/LISA-MULTIRANGE-PLAN.md (arm_vm_init assumes one bank; second bank
   0x200000000..0x280000000; never pass the 0x1f0000000 hole as RAM).
5. Ports from upstream (same SoC): Wi-Fi (hakios.wifi.c, hakios.wlfw.c, hakios.apple80211.c), audio
   (hakios.adsp.c, hakios.sound.c, hakios.speaker.c, hakios.mic.c — needs Lisa codec/amp differences
   checked against the Lisa DTS), battery (hakios.battery.c over ADSP GLINK), haptics. DDR bandwidth vote
   (upstream ddr_raise) only after serialising RPMh TCS use with Lisa's sweet_rpmh/Adreno code.
6. Brightness: AMOLED via DSI DCS 0x51 needs an MDSS DSI command path; Bluetooth/NFC/sensors/USB are not
   in upstream either.

## 5. Do not
- Do not flash boot/vendor/modem/persist; only `fastboot boot` images and userdata.
- Do not claim a feature works until the user saw it on the phone.
- Do not delete `ios-artifacts/lisa-ios-uncompressed-metal-stage.dmg` (master), the IPSW, `kdk-expanded`,
  `lisa-firmware/`, sources, or `diagnostic/*.log`.
- Do not enable gpu_cached=0, PAGE_SIZE surface offsets, or "success" answers for unimplemented selectors.

## 6. LAST STATE when Claude stopped (2026-10-26 16:10 IST) — START HERE

- Best confirmed image: `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v14safe.img` (gpu_cached=1, CA hooks,
  GPU present, lisacpufreq=2). User saw: iOS boots, **Control Center renders correctly now**; still ~30 Hz with
  occasional flicker; Phone app black; Settings > About and Settings > Storage crash; no apps/wallpapers.
- In flight: `lisa-ios-gl-v15.img` = same + **gpu_cached=5** (GEM pages WCOMB in the pmap, no per-submit clean).
  Queued as agent-queue/089-boot-v15.job (needs the phone in recovery/fastboot + USB replug) and
  090-pull-v15.job. Check diagnostic/gl-v15-recovery.log: if "adreno: gpu: no answer" appears, mode 5 is wrong
  -> stay on v14safe. If clean, compare the "handing on" time in "redwood-gl: frame N" lines (v13: 5-20 ms).
  Watch for stability after long use (pages must return to VM_WIMG_DEFAULT on every release path).
- Userdata flashed = v6 image (master dmg + liblisagl with CA hooks + GPU present + LisaMetal fixes).
- Crash evidence in diagnostic/gl-v14safe-recovery.log: Preferences SIGTRAP (About/Storage: Swift/ObjC trap,
  likely MobileGestalt/storage-service answers; symbolicate the bt with sym-mount.sh + ipsw a2s, slide in the
  CRASH line), PosterBoard (uncaught ObjC exception), assetsd, mediaserverd. CA renderer still lacks functions
  40 "sb3" and 60 "p3c" (upstream inject.c give_the_renderer_a_shader shows how to add sb3).
- /System/Cryptexes/{App,OS} are symlinks to /private/preboot/Cryptexes/{App,OS} (present). App-cryptex apps
  live under .../App/System/Applications? — verify path; missing home-screen apps are more likely
  LaunchServices/installcoordination registration (look at lsd/installd output with sweetsample on lsd).
- Flicker: eagl renders in place (glblit=0) while the GPU present reads the same surface; if flicker persists,
  try glblit=1 with GPU present (mirror texture), or present from a completed frame only.
- 16:10 IST: v15 (gpu_cached=5) was booted via fastboot (OKAY); log pull job 090 is waiting for recovery. Codex: after the user enters recovery, read diagnostic/gl-v15-recovery.log and decide v15 vs v14safe per section 6.

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

### 2026-10-26 18:40 IST (Claude resumed after Codex's stop)
- v15 (gpu_cached=5) PANICKED: `phystokv_range: illegal PA 0x269e4000; phys base 0x180000000, size 0x100000000`
  right after liblisagl loaded. Some GEM (client-task) pages are BELOW XNU's bank; pmap_sync_wimg ->
  phystokv cannot map them. gpu_cached=5 is unusable as written (would need to skip/handle such pages, but
  then those pages are neither WC nor cleanable by phystokv). NOTE: lisaFlushPhysical() SKIPS such pages
  (pmap_valid_address), so in gpu_cached=1 they are never cleaned either — a plausible cause of the
  flicker/corruption. Real fix: GEM memory must come only from XNU's bank (or be mapped for cleaning another way).
- Codex's v9 userdata flash finished (46/46, diagnostic/gl-v9-flash-256m.log). Claude booted
  diagnostic/out-ufs-xnu/lisa-ios-gl-v21.img (sha 124f4930...) at 18:40; log job 092 pulls to
  diagnostic/gl-v21-recovery.log when the phone is in recovery.
- 18:55 IST: **v21 panicked**: `vm_resident.c:2627 phys_page == VM_PAGE_GET_PHYS_PAGE(mem)` (user photo).
  Causes: (a) v21's pmap_next_page skipped 0x1f8400000..0x200000000 — on arm64 a vm_page's phys page is
  derived from its INDEX in vm_pages[] so pmap_next_page MUST be contiguous; (b) v21's cmdline had
  gpu_cached=5 (the v15 panic mode). FIX (in source now): pmap.c skip + avail_page_count subtraction removed;
  vm_resident.c `lisa_page_blocked()` — such pages are vm_page_init'ed but never released
  (vm_page_release_startup returns early; vm_get_delayed_page initializes them, leaves them wired, takes the
  next). Backups: lisa-tools/vm_resident.c.pre-lisahole, pmap.c.pre-lisahole.
- Flicker fix: GEM buffers cleaned via a per-buffer kernel IOMemoryMap (lisaKmap/lisaFlushGem; released with
  the memory in lisaReleaseGem at every GEM release site). Old physical path skipped pages below gPhysBase
  (they are never cleaned -> stale GPU reads). gpu_flushmap=0 restores the old path.
- New image `diagnostic/out-ufs-xnu/lisa-ios-gl-v23.img` = v21 cmdline with gpu_cached=1 + kernel with both
  fixes. Booted 18:57 IST; log job agent-queue/096 -> diagnostic/gl-v23-recovery.log.

## Update 2026-10-26 evening (Claude): v24 / v25, flicker + Night Shift + brightness discovery

**v24** (`diagnostic/out-ufs-xnu/lisa-ios-gl-v24.img`) = v23 + boot-arg `glwait=0` (no glFinish at frame end).
Log `diagnostic/gl-v24-recovery.log`: frame time fell from ~21 ms (v23) to ~11-14 ms; "waiting" from
5-12 ms to ~0.3 ms. GPU present is ordered on the GPU queue after CA's draws, so the wait is not needed
for the GPU path. Keep `glwait=0` unless frames show half drawn.

**Flicker cause (v23 "choosing a wallpaper flickers through all wallpapers")**: `eagl.c frame_end()` called
`panel_present()` for EVERY drawn 1125x2436 target, including CoreAnimation's offscreen full-screen
targets (poster/wallpaper snapshots, blurs). Each was drawn onto the panel. Fix in v25:
- kernel `bsd/dev/lisa_cpufreq.c`: sysctls `kern.lisa_swap_ids` (last 8 distinct surfaces swapped onto the
  display, filled by `AppleCLCD::swap`), `kern.lisa_gpu_presented` (write-only-ish handshake, ring of 4),
  `kern.lisa_color_matrix` (IOMFB selector 55 SetMatrix, up to 4 keyed 3x3 double matrices).
  `kern.lisa_brightness` is now CTLFLAG_ANYBODY (backboardd runs as mobile).
- `SweetFramebuffer.cpp present()`: a full-screen layer is skipped (no CPU copy) only if liblisagl said it
  GPU-drew that surface (`lisa_gpu_shown`); otherwise the CPU scaled copy runs (logs "LISA FB: surface N
  was not drawn by the GPU"). Selectors 17/50/66/68/69/84 are logged ("LISA FB: sel ...") to find the
  brightness path; selector 55 stores the matrix.
- `eagl.c`: `panel_is_display(id)` - only surfaces in `kern.lisa_swap_ids` are presented; after presenting,
  writes `kern.lisa_gpu_presented`. Panel shader multiplies by `uniform mat3 cm` (Night Shift / white
  point) before brightness. Backups: `eagl.c.pre-handshake`, `lisa_cpufreq.c.pre-display`,
  `SweetFramebuffer.cpp.pre-display` (in lisa-tools/).
- `brightness.m`: v23 log said "controller method unavailable" (16.7 has no
  `-[BKDisplayBrightnessController setDisplayBrightness:permanently:]`). Now lists every brightness/backlight
  method in backboardd's brightness classes ("LISA BRIGHTNESS: -[Class sel] types") and generically hooks
  safe-looking setters (1-2 float/double args). Backup `brightness.m.pre-generic`.
  NEXT: read the v25 log's LISA BRIGHTNESS + "LISA FB: sel 68/50" lines and pin the real setter.

Builds: kernel log `local-build-0118.log.gz`; boot image `lisa-ios-gl-v25.img` (v24 cmdline + new kernel);
userdata `ios-artifacts/lisa-ios-gl-v10.simg`. Jobs 099-102 in `lisa-tools/agent-queue` (NOTE: the queue is
`lisa-tools/agent-queue`, jobs run with cwd ~/IOS-LISA).
If v25 shows a stale/frozen screen: the handshake is failing -> check "kern.lisa_gpu_presented not
written" in the log; the CPU fallback still copies at sweetfb_hz.
Not done yet: Wi-Fi (WPSS/WCN6750), Bluetooth, audio, real DSI brightness, more RAM (sparse physmap), 90 Hz.

## Update 2026-10-26 19:55 (Claude, stopped for usage) - v26 + Wi-Fi port in progress
- v26 (lisa-ios-gl-v26.img + userdata lisa-ios-gl-v12.simg): SweetSurface pool (reuse 14MB backboardd
  surfaces, same id; lisa_surf_pool=0 off), console muted on first full-screen layer, touch 4ms poll while
  touched, lisa_dsi.c (lisadsi=1: DCS 0x51 brightness via DSI0 0xae94000, cmd buffer 0xe3200000 in splash
  tail; lisa_hz=90 sends DCS 6c 01; kern.lisa_hw_brightness=1 turns GPU dimming off). Queue: 111 = flash+boot
  v26 (misnamed), 112 = pull log -> diagnostic/gl-v26-recovery.log. CHECK "LISA DSI:" lines first.
- Panel = CSOT k9d_42_0d_0b (live DTB: lisa-firmware/live/lisa-live.dts).
- Wi-Fi port (NOT built yet): upstream redwood_* copied as bsd/dev/lisa_{wifi,wlfw,qmi,lx,sta,net,apple80211}.c,
  iokit/Kernel/LisaWLAN.cpp, glue bsd/dev/lisa_wifi_glue.c (fw from /usr/local/lib/lisa/fw, options also from
  fw/bootargs file, MAC from /chosen/mac-address-wifi0), board file bd_k9din.elf. Added to bsd/conf/files,
  iokit/conf/files.arm64, sweet_ufs.c (lisa_wifi_start). Firmware in lisa-firmware/wifi (deploy-stage copies it).
  lisa_sta.c / lisa_apple80211.c not shimmed (no PE_parse calls). NEXT: compile kernel, fix errors; first test
  fw/bootargs "wifi_step=14 wifi_new=15"; then build ath11k (research-hackios-kernel/tools/mk-ath11k.sh, mk-wlfw.sh).

## 2026-10-26 Codex continuation: v27 work in progress (NOT phone-tested)
Recovery connected; pulled `diagnostic/gl-v26-codex-recovery.log` and
`diagnostic/gl-v26-recovery-hardware.txt`. No phone writes/reboots yet this turn.
- v26 DSI command timed out (intr0x500/status5), leaving software dimming active.
  Found mixed shifted/unshifted 6G registers. Linux dsi_cfg.h defines io_offset4;
  corrected TRIG_CTRL84/TRIG_DMA90/ACK68/PHYb4/TIMEOUTc0/EOTcc/CLK11c/VERSION1f4.
  Previously +8c triggered BTA, not DMA. Temporarily select software DMA trigger4,
  preserve/restore trigger + masks + command config; refuse busy controller.
  90 Hz DCS-only switch removed: panel AND host timing must change together.
  Normal CSOT DBV max2047 is intentional;4095 requires explicit HBM sequence.
- Narrowed brightness.m to the TWO exact measured setters/ABIs, eliminating
  keyboard/backlight/curve hooks which could overwrite the display request.
- Added kern.lisa_display_awake (read-only). Framebuffer off stops vsync delivery
  after fade; timer polls100ms, wake restores60Hz. Governor chooses minimum row
  while off, clears/ignores input boosts. GLES skips final panel writes while off.
  This is reduced screen-off work, NOT complete SoC suspend/GPU power collapse.
- Claude Wi-Fi port baseline build failed at lisa_wifi_glue rootvnode/vnode_size:
  fixed missing vnode_internal.h. Further compile/link validation pending.
Preserve cached1 + per-GEM kernel cache-flush map + contiguous vm_pages handling.
Source references: official Linux drivers/gpu/drm/msm/{dsi/dsi_cfg.h,dsi/dsi_host.c,
registers/display/dsi.xml}; downloaded copies /tmp/lisa-linux-dsi-*.
Next: finish build/link, stage userland, test brightness/off-wake first; Wi-Fi
firmware bring-up remains unverified. Bluetooth, cellular and native Metal are
NOT implemented by the above changes. Keep current working GL/freedreno path.

Build progress: userland build succeeded (`/tmp/lisa-gl-v27-userland.log`),
liblisagl SHA256e12fd8be1062e1a0d017ea1cc90213ba0e4ed803f67b70f7a2a3bd1ffc96003d.
Kernel initially hit protected fVsync access; removed that call (wake at most100ms
before normal60Hz resumes). Awaiting first make completion, then incremental rebuild.
Checksum-verified Linux7.0.14 ath11k fetched by upstream mk-ath11k.sh (use GNU
sha256sum PATH, macOS /sbin/sha256sum does not accept upstream --quiet option).
Added local `lisa-tools/build-lisa-wlfw.sh` to generate QMI tables in the correct
Darwin build output; generated62908-byte redwood_wlfw_ath11k.h, no longer WLFW
stub on NEXT compile. Full ath11k LinuxKPI object still not built/linked; do not
claim association. `fw/bootargs` now requests first14 firmware/transport steps,
NOT full wifi=1. Local v13 staging running, /tmp/lisa-gl-v13-deploy.log.
DSI worker now requests DBV0 while display state is off and restores requested
DBV on wake, contingent on corrected transport actually completing.
Recovery remains connected; no phone writes at this point.

v27 kernel LINK SUCCEEDED: `/tmp/lisa-gl-v27-build3.log`, after removing duplicate
mdevlookup stub (memdev.c already defines it). Generated WLFW exchange included.
Boot image `diagnostic/out-ufs-xnu/lisa-ios-gl-v27.img`
SHA256807c3876b84c8445e69f451d476af0dcb50c5e4bc00b176e99c1b13aa8dee755.
Validated11segments, RAMspan4GiB, unchanged known VM layout. cached1/glwait0/
lisacpufreq2 retained. No unsafe90Hz DCS-only command.
Userland adapter SHA256581f4dd85c8ac08d4f83fa038ec5864e3bdf05468e56472c8628dfdacad0f078.
v13 userdata verified25769803776expandedbytes,
SHA256a3027a747d7c812a63c1ebef16c22d5207517087a25309188517b6a503f013a6.
User confirmed reconnect in fastboot. Productlisa/serial<device-serial>,
userdata size0x1A5E7FB000 confirmed. Flash STARTED with -S256M:
`diagnostic/gl-v13-flash-256m.log`. Await success, then user restart fastboot
and reconnect BEFORE temporary v27 boot. Only userdata written.
Test requested next: brightness slider in CC, full normal brightness, off/wake,
heat during screen-off. In recovery pull `gl-v27-recovery.log`, inspect DSI DMA
completion and brightness hooks, screen-off CPU MHz and Wi-Fi step failure/result.

During flash, started full ath11k object build in parallel (LOCAL Mac only),
`/tmp/lisa-ath11k-build.log`. Upstream uses Linux7.0.14 + FreeBSD15.1 archives
with pinnedSHA256; needs PATHllvm/bin + GNUcoreutils. Missing research kernel
host/bin/ld64 supplied as symlink to /usr/bin/ld (no phone changes).
XNU_SRC points to darwin-xnu-build/xnu. NOT linked into v27: v27 is already built.
Even a successful ath11k.o build auto-generates stubs for missing ath11k functions;
inspect out/stubs.txt before claiming association. v27 fw/bootargs runs14steps
through QRTR only and then shuts down WPSS cleanly, no lasting Wi-Fi link.
Control Center volume still lacks real audio output route/driver: live DTB has
TFA98xx amps at0x34/0x35 (different board audio wiring), cannot fix by fake volume
sysctl alone. Bluetooth needs UART/HCI + firmware + IOBluetooth stack. Cellular
needs MPSS + QMI + RmNet + CommCenter; do not overwrite modem/persist/calibration.

Optional NEXT kernel ath11k link support added AFTER building v27 (not in its
image): MakeInc.kernel unstripped target now accepts LISA_OBJECTS in prerequisites
and normal Mach-O object link list. `LISA_WIFI_DRIVER=1 bash lisa-tools/build-lisa-kernel.sh`
requires existing ath11k.o and links strong lx_start/etc over lisa_lx.c weak fallback.
`build-lisa-ath11k.sh` wraps upstream Mac tools/environment and prints stubs.txt.
No object, no implicit Wi-Fi driver enable. v27 still only14-step firmware test.

FLASH v13 SUCCEEDED:46/46,333.189s. Await user restart/reconnect of fastboot.
First real ath11k.o build succeeded:511016code/data bytes,18driverfiles,
0auto-generated missing-function stubs;51undefined names all found in v27kernel.
Kernel link with that object then failed platform check: object targetiOS16.5
versus kernel buildmacOS. Added upstream mk-ath11k.sh optional LISA_LX_TARGET
(default remains iOS); local wrapper now builds arm64-apple-macos13.5. Rebuilding
/tmp/lisa-ath11k-build3.log. Do not just relabel old object load commands.
Actual Xcode linker binary used (symlink /usr/bin/ld invoked as ld64 hits a
command-line-tools selector error; wrapper now fixes that managed symlink).
Bootv27 is already saved and valid despite the subsequent optional Wi-Fi link
failure. Don't repack from an unsuccessfully linked kernel.

v27 TEMPORARY BOOT ACCEPTED after user fastboot restart/reconnect:
`diagnostic/gl-v27-boot.log` send0.394s,boot0.214s,total0.614s.
User testing CCbrightness/max/off-wake/2minscreen-offheat, then recoveryADB.
No user observation yet. Do NOT claim hardware brightness or sleep fixed yet.
Correct-target ath11k build succeeded510132code/data bytes,18files,0auto-generated
missing-function stubs,51kernelimports. Kernel v28 link now running:
`/tmp/lisa-gl-v28-wifi-link2.log`. Only build on Mac; current phone runsv27.

v28 REAL ATH11K KERNEL LINK SUCCEEDED (`/tmp/lisa-gl-v28-wifi-link2.log`).
`llvm-nm -m` confirms lx_prepare/start/stop/transmit are strong external symbols,
not weak fallback. Kernel includes ath11k.o with51resolved imports and0generated
missing-ath11k-function stubs (LinuxKPI is still a limited implementation).
Packaged `diagnostic/out-ufs-xnu/lisa-ios-gl-v28-wifi.img`, structurally valid,
SHA2567e07b78952d6e2f353b9a08df6f8a46d8bc7f27f7e043978eb19c1409533dd13.
Matching symbol file `diagnostic/out-ufs-xnu/kernel.gl-v28-wifi.unstripped` saved.
Hashes in `diagnostic/gl-v27-v28-artifacts.json`. v28 NOT BOOTED. It has same
14-step options from userdata fw/bootargs; real driver not automatically started.
Do not confuse linked driver with associated Wi-Fi. Need v27 firmware/DSI result
from recovery before activating driver/mission mode. To override staged step14
without reflash, a new cmdline may set wifi_step=15, or wifi=1 wifi_step=0 for full
async mode; cmdline only608bytes (v27 uses599), remove an optional nonessential
argument such as -noprogress first, validate length and preserve cached1/glwait0.
Full mode defaultswifi_irq1; IRQ attachment glue returns unsupported, so verify
upstream fallback or explicitlywifi_irq0 and account for polling's idle heat.

Recovery-read decision tree for v27 (pending user result):
1. `bash lisa-tools/pull-lisa-log.sh <device-serial> lisa-tools/diagnostic/gl-v27-recovery.log`.
2. DSI: check corrected trigger/DMA_DONE, ack errors, DBV changes incl0off. If it
   still times out, do NOT raise timeout/HBM blindly: verify inherited DPU SMMU
   actually maps command IOVA0xe3200000 (DT splash reservation alone is not proof).
   v26 never triggered DMA, so that mapping is still an assumption, not measured.
3. Brightness adapter should log exactly2hooks and selected display levels, not
  15keyboard/backlight hooks. No dimming success claim until phone observation.
4. Screen-off should reduce governor rows to0 and stop FBvsync delivery afterfade.
   Final GPU panelwrites skip whileoff; still no GPU rail collapse/CPUdeepidle.
5. Wi-Fi: first failing step, TrustZone/PAS result, GLINK/QRTR firmware service.
   v28 with driver is prepared, but enable mission only with actual result.
6. Full-brightness here is normal DBV2047.4095isHBM, requires panel sequence and
   thermal policy, not unconditional max. 90Hz requires DPU/DSI host timing too.
7. Control Center volume: no actual audio route driver yet; Lisa's TFA98xx amps
   differ from redwood wiring. Do not fake hardware-volume success. Native Metal
   remains unfinished; current proven graphics is GLES/Mesa freedreno on Adreno.

Preserve v28 archived symbols. v27 image was saved before v28 kernel relink;
matching v27 unstripped symbols were NOT separately archived before overwrite.
Do not symbolicate a v27 panic with v28 addresses without accounting for layout.

## 2026-10-26 v27 phone result and next integration
User confirms iOS boots; hardware brightness now correct. CCtoggle/volume still
not working; rendering lag/stalls, occasional flicker, pink Safari tabs background.
Off/on flickers; sleep heat reduced but still warm. Requests radios/USB/charging,
90Hz/nativeMetal/sensors/moreRAM. RecoveryADB88connected. Pulled
`diagnostic/gl-v27-recovery.log` (682084bytes/flush43).
Confirmed DSI DMA_DONE/noACKerrors for brightness inclDBV0 and2047. Keep offsetsfix.
Confirmed Wi-Fi steps1..14 SUCCESS: PASauth/start returns0, DSPup36.559ms,
GLINK/QRTR sees WLFWservice69(v1,inst3,node13port10); clean shutdown0.
Real GPU submissions, frames~10..30ms (average21ms); cacheflush~1.4ms/submission.
`vmd` burns>50%CPU for180s (do not assume this is the virtual-memory daemon).
Recovery battery driver is `qti_battery_charger` via
/soc/qcom,pmic_glink/qcom,battery_charger, modelK9_COSMX_4250MAH;
actual100%,4464812uV,current-1009840uA,USBPDonline. OldPMIC/QGpolling cannot
provide this state. Evidence gl-v27-battery-driver.txt and recovery-power-sensors.txt.

NEXT v29 sources in progress:
- Removed CPU framebuffer fade when DSIbrightness active; it raced GPU and left
  half-dark content onwake. HardwareDBV0 blanks, pixels preserved. PublicresumeVsync
  restores60Hz promptly onwake. Softwarefallback blacks twice, no continuingfade.
- Ported current upstream redwood_adsp.c into bsd/dev/lisa_adsp.c, Lisa root
  firmware/options shim, actual Lisa-only platform guard (no xnuloader soc marker).
  ADSP36segments validated against64MB carveout using check_lisa_adsp.py.
- Added lisa_battery_state.c/.h: validate battmgr sample ABI/ranges, locked cache,
  rejectstale>45s. SweetBattery consumesADSP snapshot/SOC/temp/current/charging,
  Linuxnegativechargingcurrent convertedforiOS; polls5s. Still readonlycharger.
- Firmwarecache now96files andmutexserialized forparallelWPSS/ADSP loads.
- fw/bootargs requests fullwifi1/step0/IRQ0/file0 + adsp1. No credentials.
  deploy-stage now includesADSP firmware. Missionmode NOT yet tested onLisa.
- Arm ASTC5.2.0 officialsource pinned2319d9c4d4af53a7fc7c52985e264ce6e8a02a9b
  fetchedthird_party; hostdecoder testof capturedfirstblock gives7,122,202,255
  inBOTH LDR/HDR. No justification to blanketHDR-fallback or RGBtintcorrectyet.
  Do not ship unnecessaryCPUtexturedecoder; inspectactualSafari sampling next.

v29 FINAL BUILD SUCCEEDED (`/tmp/lisa-gl-v29-build3.log`), strong ADSPinit,
batteryreport/snapshot, real lx_start and refresh-hz accessor verifiedwithnm.
Shared header exported via bsd/sys/Makefile PRIVATE_KERNELFILES. Upstream ADSP
has audio/mic hooks; linked explicit inactive callbacks (battery transport only),
no PCM or TFA amp claim. Firmwarecache lock serializes loads,96slots.
Kernel and exactsymbols saved:
- diagnostic/out-ufs-xnu/lisa-ios-gl-v29.img
  SHA2568ef960709a35877bc5d2901a868aee62c6cb6e1e595560a30c0dd9f35de0da4b
- diagnostic/out-ufs-xnu/kernel.gl-v29.unstripped
-11segments structurallyvalid; sameRAMspan0x180000000+0x100000000.
-598-bytecmdline: optional-noprogress removed, lisa_hz90added; cached1/glwait0kept.

90Hz refinement from actualCSOTDTB (diagnostic/csot-live-panel.dts): timing0=60Hz,
VFP1212; timing1=90Hz,VFP4; SAME link682000000, SAME PHY bytes andDSCgeometry.
This iscommandmodewithTEpin, notvideo. Added guardedDCS6c01 and coupled synthetic
IOMFBtimer11111us (was16667), onlyafterDCScompletion. Logs explicitlysayphysicalTE
notmeasured. NEEDphonevalidation; notclaim90FPSorconfirmedphysicalrefresh.
NoPLL/PHYguessing. v27image remainsknown60Hzfallback.

Screen-off: removeCPUfade whenhardwareDBVworks, preservepanelpixels; immediate
publicresumeVsync onwake. PolledWPSS/ADSP1ms loops coalesce10ms whenoff; ath11k
poller useshostwifi_poll_ms(1awake/10off), rebuiltrealobject0generatedstubs.
UFS defaultsufswait1(sleepeligiblecallerafter50us,canrestoreufswait0), noflush/
writeorderingremoval. This reducesCPUbusywait, notprovenrawstoragethroughputgain.

VisualVoicemailvmd identified fromstockplist/binary, notvirtualmemorydaemon.
TemporaryDisabledtrue in v14 whilemodembackendabsent; Phone/CommCenterretained.
Originalplist savedios-artifacts/com.apple.voicemail.vmd.before-v14.plist.
This removesa measured>50%CPUfor180s loop; restorewhenmodemworkisavailable.

v14userdata staging/verificationSUCCEEDED (`/tmp/lisa-gl-v14-deploy.log`),
25769803776expandedbytes,
SHA256806043be6db832cb6e4ba34ce504d29e05d8113b079c34e044368af62927116e.
Adds36ADSPsegments+metadata fromvalidatedcarveout. fw/bootargs:
wifi1 wifi_step0 wifi_irq0 wifi_file0 adsp1 (storedwith=syntax).
NoWiFicredentialsread/provided. Musttestscan/associationafterfirmwaremission.
Notflashedyetatthisentry. Nextflashv14-S256M, USERrestart/reconnectbeforev29boot.

Metalresearchprimarysources:
https://docs.mesa3d.org/drivers/freedreno.html (Turnip=VulkanAdreno driver)
https://github.com/KhronosGroup/MoltenVK (Vulkan OVER Metal, opposite direction)
https://github.com/Nishad-Sharma/MoltenMTL (Metal-likeSwiftwrapperOVER Vulkan,
Windows-onlytested; requiresSPIRV, explicitlyNOshadertranslation). Cannotdropthis
intoCoreAnimation'sAppleprecompiledAIR/metallibpath. NeedXNUVulkanbackend + AIR/
MSLtranslation + binary/ObjCABI + IOSurface/fenceinterop, orcompleteexisting
LisaMetalbackend. No newnativeMetalcapabilityadvertisedforthisboot.
USBdata,DWC3gadget,cellularCommCentertranslation,BluetoothHCI,andmotion/proximity
sensorprovidersremainunimplemented. Batterygauge/temperatureisthenewsensorpath.

v14flash STARTED after userconfirmedfastbootreconnect; productlisa verified.
Log diagnostic/gl-v14-flash-256m.log (47chunks,ADSPadded). No boot untiluser
restartsfastboot/reconnectsafterflashsuccess. Working master/v14simg not modified
whileflashisreadingit.

Upstreamuserland freshnesscheckduringflash: localresearchcheckouta79ff07 with
modifications air_metal.c/air_to_nir.c/redwood_drm.c/mk-mesa.sh (PRESERVE).
RemoteHEAD2573c4a8d80ed143e534d16b5a386efa5b06ccbb. Cloningseparatelyto
research-hackios-userland-20261026 forinspection, notpullingoverlocalchanges.
UpstreamkernelREADME explicitlydescribesCoreAnimationGLESonAdreno asworking;
Metalapps areinprogress (293/456systemshaders). Don'tassumeitoffersfinished
MetalCoreAnimationtoenablebyaflag.

v14 FLASH SUCCEEDED47/47,341.917s. User restartedfastboot/reconnected;
v29 temporarybootstarted (diagnostic/gl-v29-boot.log). Artifacts/hashes saved
`diagnostic/gl-v29-artifacts.json`; matching v29symbols retained. Awaitonphone
result and nextrecoverylog. No confirmedWi-Fiassociation/ADSPsamples/90Hzyet.
Currentupstreamcombinedrepo8851317 at hackios-xnu-kernel (hakios-os name returns
404fornow), clonedresearch-hakios-os-20261026. EAGLdiff666lines reduces11lines
(onlyuppercaseCAfunctionmacrorename) afterredwood->hakiosnormalization: noactual
rendererfixsincea79ff07. Saveddiagnostic/upstream-eagl-*-20261026.diff.
Do notoverwriteLisa adaptationswithupstreamrenamechurn.

v29 boot acceptance confirmed from diagnostic/gl-v29-boot.log: Sending OKAY
0.406 s; Booting OKAY 0.211 s; total 0.625 s. No boot command needs repeating.
User has been asked to check Wi-Fi network discovery, live battery/charging,
and wake flicker, then return to Lineage recovery with ADB for log retrieval.
Next: pull-lisa-log.sh <device-serial> diagnostic/gl-v29-recovery.log; inspect ath11k,
ADSP battmgr samples, DSI refresh completion, GPU timeouts and service crashes.
Do not claim native Metal, physical 90 Hz, Wi-Fi association or charging success
from boot acceptance alone. Preserve v27 as the previous tested fallback.


## 2026-10-26 v29 recovery / v30 and v15 preparation

User: battery gauge and charging confirmed working; charging cable state delayed; display seems
90 Hz but animations lag, blur missing, Safari color/render errors, CC controls fail. Asked USB
data, all radios/sensors and newly camera/flashlight. Device initially recovery ADB connected.

Recovery log: lisa-tools/diagnostic/gl-v29-recovery.log (1,169,116 bytes, flush 78).
- Wi-Fi firmware/ath11k reports THE RADIO IS UP after 9.9 s; en0 present. No wifid exec or ioctl
  requests except configd. Stock com.apple.wifid.plist has _LimitLoadToDeviceTree wlan/marconi-wifi;
  current ADT has neither. v15 removes only this gate, backs up original, preserves stock daemon
  and entitlements. Network discovery/association still needs phone test; never claim connected.
- ADSP live SOC/current/temp and USB online transitions confirmed. Firmware sends opcode7 notices.
  v30 samples promptly when notice sequence changes (50 ms check), publishes cached source every
  1 s instead of 5 s. Ten-second periodic fallback retained. No charger settings changed.
- DCS6c01 command completed; physical TE remains unmeasured. GPU active, most submissions ~1.5–3 ms;
  cache cleaning ~1.3–1.6 ms/submission, full frames ~18–27 ms, so cannot promise 90 FPS.
- Renderer fix: plain_draw_begin previously treated ANY programless in-place pass as variable blur
  once a blur uniform had ever been seen. Added exact FUNCTION_VARIABLE_DOWNSAMPLE guard. This
  prevents unrelated EDR/color passes mutating blur chains; visual outcome untested. Libraries O2
  instead O1, frame summaries every600/3000 rather than20/200; glframes=1 diagnostic retained.
- assetsd repeatedly aborts: assetsd requires TCCPhotosService access. Its original signature has
  com.apple.private.tcc.allow Photos; no global TCC bypass applied. AegirPoster abort: nil Metal
  uniform buffer in NUNIAegirRenderer getOrCreateUniformBufferforFrameBufferIndex. Native Metal
  still incomplete. Several widget extension check-in timeouts. These are NOT fixed by O2.

USB v30:
- Original sweet_usb.c was compiled but never called. It used wrong Sweet GCC/Femto assumptions.
- Now called before WPSS startup from UFS init; default sweetusb=2, 0 disables, 1 read-only.
- Xiaomi official lisa-r-oss gcc-yupik.c: sleep0xf018,UTMI0xf01c,cfgNoC0xf07c,aggreAXI0xf080;
  master0xf010/GDSC0xf004 retained. Removed QUSB2 reads (Lisa uses Femto0x88e3000).
- After DWC3 soft reset/halt, require SID0xe0 sole exact match, exclusive context, stage1 CBAR,
  disable only inherited USB stage1 translation for physical DMA (like UFS). No global SMMU,
  other stream, PHY or clock reprogramming. If preflight fails, gadget stays off and iOS continues.
- ECM high-speed network ue0 192.168.7.2; host192.168.7.1; existing UDP5140 log transport.
  Not Apple USB pairing/usbmux/MTP. USB data success not yet observed on phone.

Build logs / images:
/tmp/lisa-gl-v30-build.log succeeded; /tmp/lisa-gl-v15-libs.log succeeded.
lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v30.img, matching kernel.gl-v30.unstripped,
payload lisa-ios-gl-v30-payload.bin structurally validated11segments, unchanged4GiB RAM span.
v15 deploy /tmp/lisa-gl-v15-deploy.log running/verify pending at this entry.

Camera/flashlight new work:
- DT CSI0wide S5KGW3, CSI1front S5K3T2, CSI2macro S5K5E9YX04, CSI3ultra IMX355 per vendor filenames.
  CCI0master1/reset21/MCLK65/CSIPHY1 wide; CCI0master0/reset20/MCLK64/CSIPHY0 front;
  CCI1master0/reset77/MCLK66/CSIPHY2 macro; CCI1master1/reset78/MCLK67/CSIPHY3 ultra.
- Extracted vendor tables with upstream camsensor.py into lisa-firmware/camera:
  camera-s5k5e9yx04.tab (module reports ID0x559b at0, slave0x5a; verify chip before writing),
  camera-imx355.tab (ID0x355 at0x16,slave0x34). Logs diagnostic/lisa-camera-*-tables.txt.
- Lisa camera supplies differ from POCO, including front GPIO106=VANA2.8V, GPIO108=VDIG1.05V.
  Do not blindly copy POCO sensor table or start its regulators.
- DT flashlight PM8350C SID2 base0xee00; rear camera torch-source led:torch_0/channel0,
  not all four LEDs. Official driver downloaded /tmp/lisa-leds-qti-flash.c (MiCode lisa-r-oss),
  /tmp/lisa-qti-battery.c, /tmp/lisa-gcc-yupik.c. Torch programming not enabled yet; needs dedicated
  SPMI write ownership, current limits, timeout/off and iOS AVCapture/flashlight control bridge.
- Recovery lacks camera/LED/sensor class devices, except ADC iio:device0. No invented sensor data.
- Bluetooth HCI/UART, modem registration+CommCenter, sensors SSC/SLPI and camera capture/ISP remain.

v15 staging/verification SUCCESS, expanded hash e151c299d6f52bc9e6ae2cf44136fe81bddfb7853c4f96a641688a2e640eccd2, 24 GiB expanded. Artifact manifest diagnostic/gl-v30-artifacts.json. Awaiting fastboot reconnect before flash.

Additional preparation while v15 flashed (no image changes after verification):
- diagnostic/lisa-peripheral-map.py extracts 33 exact camera/CCI/CSIPHY/BT/FastRPC/flash
  nodes into diagnostic/lisa-peripherals.json; source hash included, supply phandles resolved.
- Upstream soc-args.py on Lisa FDT produced diagnostic/lisa-modem-soc.args:
  IPA0x1e40000/GSI0x1e04000, IPCC0x408000, RSC0x18220000, MPSS0x8b800000,
  IPA firmware0x8b710000, streams0x480/0x482, PAS4/15, yupik_ipa_fws.
  These are preparation only, not a linked/started cellular driver.
- BT: WCN6750 resetGPIO85, swctrl86, shared WLANreset84. Do not reset shared Wi-Fi
  while ath11k is active. Supplies exact in peripheral manifest. No HCI transport yet.
- assetsd disassembly diagnostic/assetsd-tcc-v29-symbols.txt: scope7 query through
  PLPrivacy photosAccessAllowedWithScope returns nonzero denial/error; asserts at0x1000162c0.
  Scope implementation and call trace saved diagnostic/plprivacy-tcc.dis.txt. No permission
  spoof or global privacy bypass was applied. Needs actual TCC service/error investigation.
- AegirPoster stack symbolized: NUNIAegirRenderer getOrCreateUniformBufferforFrameBufferIndex
  and CLKUIMetalQuadView _renderQuads:toScreenWithCommandBuffer:passDescriptor. Missing native
  Metal buffers, not panel brightness.
- Mesa optimized liblisagl hash ec51434ec9ed06a50af2473f223f3f977d91d54b9e234254a67361df9c1e5184
  matches staged library exactly.

Primary register references:
https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/lisa-r-oss/drivers/clk/qcom/gcc-yupik.c
https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/lisa-r-oss/drivers/leds/leds-qti-flash.c
https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/lisa-r-oss/drivers/power/supply/qti_battery_charger.c
https://raw.githubusercontent.com/torvalds/linux/master/drivers/usb/dwc3/dwc3-qcom.c

Flash v15 started only after user replied connected; getvar product=lisa. Log
 diagnostic/gl-v15-flash-256m.log. Do not boot until user restarts fastboot/reconnects
 following successful flash (explicit persistent user preference).


## 2026-10-27 native Metal continuation

User personally booted v30 and returned to recovery: persistent lag/render errors, wants Metal.
v15 previous flash confirmed47/47,341.436s. v30 new log diagnostic/gl-v30-recovery.log:
828,080bytes,flush62. GLES active on Adreno; no observed kernel/GPU panic. Metal unavailable to
all processes due lisagl=1 selection, NOT because buffer allocation itself was observed failing.
Aegir nil uniform buffer occurs with no offered Metal device. Driverkitd also repeatedly SIGKILLs;
Music null deref and assetsd TCC issue persist. Don't conflate those with a shader compiler error.
USB v30 clocks on, DWC3 softreset succeeds; USB-only DMA preflight rejects. No USB ECM yet.
Wi-Fi radio UP10s but no wifid exec in captured log; launch gating issue not fully resolved.

Implemented native Metal code (not hardware-tested yet):
- lisa-metal-userland/src/lisametal.m: real buffer blit encoder records copy/fill commands;
  keeps source/destination until completion, enforces encoder lifetime and device identity.
  Commit executes work in order through new rwa_buffer_copy/fill. Range errors set NSError5;
  earlier blits fenced before error resources can be released. No pretend texture blits/events.
- AIR air_metal.c/h in research-hackios-userland/tools/mesa/air and actual Mesa build tree:
  buffer bounds (including signed32 coordinates) and overlap validation; uses Gallium
  resource_copy_region and clear_buffer (fd6 has GPU path for one-byte clear values).
- Native viewport/scissor now implemented. Separate rwa_render_draw_region entrypoint and
  rwa_raster_region leave old rwa_draw ABI untouched. Per-draw geometry recorded in work;
  backend validates finite viewport/range, scissors bounded to attachment, sets negative-Y
  Metal viewport plus origin and explicit default full scissor each draw. Rasterizer enables
  scissor. Legacy rwa_render_draw passes null region, so old callers retain full viewport.
- Mesa dynamic loader function table initialization serialized with pthread mutex; verifies
  buffer create/map/destroy/device close/name/finish before accepting loaded library.
- Metal buffers retain owning device; failed CPU map returns nil/cleans up instead of handing
  callers a resource with null contents. Existing imported IOSurface/texture paths retained.
- Per-process native selection: LISA_NATIVE_METAL=1 environment or exact process name line in
  /usr/local/lib/lisa/native-metal-processes bypasses GLES suppression for that process only.
- New test src/metal-smoke.m runs25s after launch, alarm90s. Native LisaMetalDevice required.
  Fills two4KiB buffers, copies subrange, releases source BEFOREcommit to verify retained work,
  checks4096bytes. Loads stock AppleMetalGLRenderer.bundle/default.metallib clearVertex and
  clearFragment20; actual native AIR vertex/fragment pipeline on three4x4RGBA32Uint targets;
  index14 position/constants, checks16pixels against uniform bytes16..31. Then tests clipped
  viewport1,1,2,2 + scissor1,1,1,2 and next fresh default pass; every pixel checked.
  Only if all pass, appends AegirPoster to native allowlist. No GUI global Metal switch.
  Test cannot prove complete Metal semantics or UI acceleration. Failure output is evidence,
  not success. Full GUI stays GLES until broader shader/object/sync support is verified.
- build-lisa-air.sh rebuilds targeted libredwoodair from existing patched Mesa tree, strips to
  lisa-gl/out/liblisair.dylib with correctinstallname. lisa-gl/build.sh now builds matching
  AIR backend and test. deploy-stage.sh installs/signatures via normal build, copies/verifies
  both, adds RunAtLoad dev.lisa.metal-smoke with envnative1, resets processfile testonly.
- Build /tmp/lisa-metal-v31-build.log SUCCESS; warnings only deprecated newLibraryWithFile.
  v16 localstaging was an intermediate without viewport tests and was NOT flashed.
  Final v17 staging /tmp/lisa-metal-v17-deploy.log. Same tested v30 kernel/image (no new kernel).
  Preserve all Claude AIR frontend/drm-shim changes; targeted source diff inspected beforecopy.

Next: verify v17 staging/sparse hash; get recovery->fastboot reconnect before userdata flash,
then user's fastboot restart/reconnect before temporary v30 boot. Let run3minutes; recover
ADB log and search LISA METAL TEST / AegirPoster / compiler library / GPU stalls. Require
PASS bytes, PASS native AIR pixels, PASS viewport/scissor before claiming any rendering result.


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

Flashing v17 started after explicit fastboot reconnect; getvar product=lisa.
Flash log diagnostic/gl-v17-flash-256m.log; temporary boot not sent yet.
GLES baseline extracted to diagnostic/gl-v30-render-baseline.txt: averaged21.406ms
at3000frames,18.153ms at6000,17.720ms at9000. GPU waits around0.37ms average,
submission/driver overhead6.5-8ms, plus other frame work. GPU GLES is active;
these timings do not establish native Metal or full90Hz performance.

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

v19 FLASH SUCCESS48/48,344.575s,exit0. User restarted fastboot/reconnected.
Before boot, adding read-only actual country query (Apple80211 get51) from copied firmware
regdomain alpha2. lx_80211.c caches atomic packed2characters only after registered firmware
regdomain update, clears on unregister; no pointer lifetime problem and no RF setting change.
New lx_regulatory_country export added to mk-ath11k.sh exported_symbols_list (verified uppercaseT
with llvm-nm); weak no-driver fallback returns0. Header prototype in bothredwood_lx.h copies;
Apple80211get51 returns real3byte country orENXIO, no guessed country/unsupported success.
ath11k build0auto-generated stubs; final kernelv32 link currently completing. Will use v32
image instead of v31 if validation passes. No unrelated Wi-Fi operations are fabricated.

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

## 2026-10-27 (Claude) v33-v37: what was wrong and what changed
- LAG 1 (Codex bug): LisaAdrenoUserClient submit used MiniIOSurface::drawn (the GPU fence it sets itself)
  as the CPU-write seed -> every surface named was cache-cleaned at EVERY submission (~25 MB, 3-4x/frame).
  Now: IOSurface state-page seed (+0xc); physical (panel, WC) never; poolable display buffers only when
  the seed moves. gpu_seedfix=0 = old. Result: 25 MB -> ~19 MB, 0.66 ms per submission, ~1 submit/frame.
- LAG 2: topology all CLUSTER_TYPE_SMP (no cluster-type in ADT) -> render work on Cortex-A55s at 90 %
  while A78s idle. osfmk/arm64/lisa_bind.c + submit(): backboardd's rendering thread bound to core 7
  (gpu_bind=N / -1). v36: avg frame 14 ms (was 16-19). Next: real AMP (cluster-type E/P in ADT + sched).
- White mark = XNU boot progress meter timer redrawing into panel memory: vc_progress_set(FALSE) at first
  full-screen layer (SweetFramebuffer.cpp). User confirmed gone? (pending)
- Night Shift: my eagl.c matrix read was wrong (s31.32 fixed, not double) - fixed; but CoreBrightness never
  sends SetMatrix after boot; swap-record words 0x250-0x260 are the damage rect (not colour). Not solved.
- Metal: v19-v34 "stops after 5 s" was LOST STDOUT, not a kill. kern.lisa_say (lisa_cpufreq.c) +
  metal-smoke printf/puts/NSLog -> kernel log. v36: device opens, PASS buffer fill/copy, PASS stock AIR
  vertex/fragment 16 pixels. viewport FAIL was a TEST bug (default clearColor alpha=1) - fixed.
- lisa_native=a,b boot-arg (lisametal.m lisa_gl_wanted) selects native Metal per process per boot image.
  v37 = v36 + lisa_native=backboardd,AegirPoster (CA Metal compositor first try); v36 = GL fallback,
  same userdata v23. In Metal mode no GPU present: kernel CPU-copy fallback shows frames.
- Diagnostics added: LISA EXIT (every signal death, kern_exit.c), LISA JETSAM (memorystatus_do_kill),
  LISA CRASH (signal backtraces, exceptions.m, also linked into wifid), LISA HANG (Preferences main-thread
  sampler; v36 Preferences killed 13x 0x8badf00d watchdog = Wi-Fi page stall).
- Wi-Fi: v36 scans, list shows in Control Center. Settings page/password stalls (watchdog) -> read LISA HANG.
- Disk: the Cowork Linux VM (Virtualization.framework PID) held deleted simgs open (133 GB). Killing it freed.
- build-lisa-air.sh used rg (not installed) -> grep. Queue: lisa-tools/agent-queue (runner globs at pass start).
