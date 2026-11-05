# Lisa Metal bring-up, 2026-10-26

> Latest continuation: v27 is confirmed to boot with Adreno GLES rendering and hardware DSI brightness. Native Metal remains unfinished. v29/v14 integrate Wi-Fi/ADSP battery and wake/90Hz tests, not a Metal fix. See HANDOFF-CLAUDE-TO-CODEX.md for current files and observations; the older trace history below is historical.

Lisa boots iOS with eight CPUs, UFS, touch, buttons and a CPU framebuffer. Metal acceleration is **not working**: the display goes black in the tested Metal boots. Do not describe GPU initialization, a successful test command, or SpringBoard starting as working iOS acceleration.

## Latest controlled result

- `v10-recover-recovery.log`: SpringBoard and backboardd start. Chronod's GPU submissions 1 and 2 complete. SpringBoard submission 3 stalls with ring read/write 80/87 and zero IB words remaining; GPU reset succeeds. Submission 4 stalls similarly at 32/39 and reset succeeds again. No GPU MMU fault is logged there. This suggests a draw/dispatch pipeline or synchronization problem rather than failure to power the GPU.
- Source: `lisa-metal-userland/src/lisametal.m`, kernel `darwin-xnu-build/xnu/bsd/dev/lisa_port/lisa_adreno_reference.c` and `darwin-xnu-build/xnu/iokit/Kernel/LisaGPU.cpp`.
- New userland build fixes recorded work owning pipeline/resource objects, makes a late completed handler wait for a GPU fence, and makes `RW_AGAIN`'s counter sequencing defined. A same-format full-range texture-view alias was also added earlier but has no demonstrated relation to the black screen. The driver builds with warnings only.
- Full 24 GiB HFS image `ios-artifacts/lisa-ios-uncompressed-metal-stage.dmg` contains the rebuilt driver. `ios-artifacts/lisa-ios-metal-retained-resources.simg` was generated with RAW and FILL chunks (no skipped DONT_CARE blocks) and verified against the raw image: expanded SHA-256 `8c31ec71f2be8bf7d3434030c9af8cc206ac015936d16ca2f7a4b75c440a9abc`.
- The full sparse image flashed successfully to Lisa `userdata` on 2026-10-26 (46/46 fastboot chunks, exit 0). The boot image is `diagnostic/out-ufs-xnu/lisa-ios-metal-v10-recover.img` and is temporary (`fastboot boot`), not flashed to boot partitions.
- First boot after this flash was black after about two minutes. Recovery log `diagnostic/v11-retained-recovery.log` shows GPU initialization, SpringBoard launch and backboardd framebuffer swaps, but no GPU submissions yet.
- The repeated five-minute boot is in `diagnostic/v11-retained-long-recovery.log` (205295 bytes). The GPU completed nine submissions, then submission 10 stalled with ring read/write 256/263, status `0xe51085`, no CP or SMMU fault, and both IBs at zero words remaining. Recovery succeeded, then submission 11 stalled at 32/39, status `0xe59085`, also no fault and zero IB words remaining. Retaining resources improved progress from two completed submissions to nine, but the display remained black. This is still not working Metal acceleration.
- A bounded packet trace was added to `darwin-xnu-build/xnu/iokit/Kernel/LisaAdrenoUserClient.cpp` behind boot argument `gpu_dump=1`, logging command buffer words for fences 8–12. The modified kernel built successfully on the Mac.
- The first trace image, `lisa-ios-metal-v11-dump.img`, had a packaging regression: `repack-sweet-kernel.py` omitted the extra `LISAGPUF` segment because it is outside the Mach-O. Its recovery log `diagnostic/v11-gpu-packet-recovery.log` shows Adreno step 12 failing with no `fw/a660_zap.mdt`, no LisaGPU service, and repeated backboardd pipeline failures. It contains no usable GPU packet trace. Do not treat those failures as evidence about the original GPU stall.
- The packer now preserves the extra firmware segment and rejects overlap with rebuilt Mach-O segments. `diagnostic/out-ufs-xnu/lisa-ios-metal-v12-dump-firmware.img` contains the rebuilt trace kernel and a byte-for-byte copy of the proven 196608-byte firmware segment. It was accepted by `fastboot boot` after the user's reconnect. Awaiting its five-minute recovery log. No additional userdata flash was done.

## Safe next steps

1. Let the v12 trace image run for at least five minutes, then return to Lineage recovery with ADB enabled.
2. Pull the guarded tail log with `bash lisa-tools/pull-lisa-log.sh <device-serial> <output-path>`. Inspect `LisaGPU dump:` lines for fences 8–12 and compare fence 10 with earlier completed submissions. `python3 lisa-tools/diagnostic/decode-gpu-dump.py <log>` decodes the bounded packet trace.
3. Identify the exact packet or draw/dispatch behind the stall. The previous `gpu_exact=0` experiment still stalled, so removing `CP_WAIT_FOR_IDLE` alone does not solve it. Do not infer Metal acceleration from GPU clocks, firmware commands or a black display.
4. For future userland-only edits, recovery lacks HFS support. Do not apply `ios-artifacts/lisa-ios-metal-view.delta.simg` to userdata: its DONT_CARE chunks can erase skipped regions, and a prior block comparison found no changed chunk matching its old baseline. Use a verified full flash or devise an exact HFS-aware patch with read-back verification.

The phone needs physical USB reconnection when switching recovery and fastboot. After a full userdata flash, its fastboot session may also stop responding until fastboot itself is restarted.


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

2026-10-27 execution update: v17 full userdata flash SUCCESS47/47 (341.737s).
User restarted fastboot/reconnected; v30 temporary boot returned OKAY, exit0.
Await recovery log after at least3minutes. Test markers:
- LISA METAL TEST: PASS ordered buffer fill/copy,4096bytes verified
- PASS native AIR vertex/fragment rendering,all16pixels verified
- PASS viewport,scissor and default-state restoration
Only after these should AegirPoster be added to native-metal-processes. The global UI
remains GLES; this is partial native Metal implementation, not complete Metal support.


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
