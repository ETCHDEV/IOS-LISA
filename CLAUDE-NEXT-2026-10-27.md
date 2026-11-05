# Claude handoff — Lisa iOS, 27 October 2026

Read this first. The user is switching agents because Codex usage is nearly exhausted.
Latest instruction: **collect logs and document investigation only**. No new build, flash,
boot or source modification was performed during this final collection turn. Phone remains
in Lineage recovery with root ADB connected, serial `<device-serial>`, workspace `/Users/arjun/IOS-LISA`.

## Actual current result

User booted iOS with userdata **v19** and temporary kernel **v32**. iOS UI boots, but lag,
rendering errors and the white dot/log-like mark below screen centre remain. Night Shift,
Wi-Fi, Bluetooth, audio, NFC, cellular and sensors do not work. Earlier user explicitly
confirmed Control Center brightness works after v18; preserve that implementation.
Wi-Fi was clarified as: Settings network page crashes, Control Center Wi-Fi is grey.
Do not describe this as merely an empty network list.

**Adreno GLES rendering is active. Full native Metal UI acceleration is NOT established.**
Do not equate GPU firmware startup, command tests, IOAccelerator publication or a booted UI
with completed Metal support. System backboardd/SpringBoard still intentionally use GLES.

## Fresh evidence collected this turn

All paths below are relative to `/Users/arjun/IOS-LISA/`:

- `lisa-tools/diagnostic/gl-v19-metal-recovery.log`: **423254 decoded bytes, flush30** (ring header total423715), decoded UFS
  guarded-tail log; matching `.log.bin` retained. Full SHA and structured findings:
  `lisa-tools/diagnostic/gl-v19-metal-result.json`.
- `lisa-tools/diagnostic/gl-v19-recovery-dmesg.txt`: Linux recovery dmesg, not an iOS log.
- `lisa-tools/diagnostic/gl-v19-recovery-memory.txt`: recovery `/proc/meminfo` and `/proc/iomem`.
- Prior comparisons: `gl-v17-metal-recovery.log`, `gl-v18-metal-recovery.log`, and their result JSON.

### Native Metal: next blocker is BEFORE the GPU query

Latest log lines1416/1454/1455:

```
exec lisa-metal-smoke
LISA METAL TEST: memory limits query=0 active=512MB inactive=512MB
LISA METAL TEST: launched; native device and GPU pixel checks begin in 25 seconds
```

No `opening native device`, no native device name, no PASS/FAIL, and no `CRASH lisa-metal-smoke`
record appears later in this captured log. The memory fix is observed working; the rendering
test itself is still incomplete. Its next source steps are `sleep(25)`, `alarm(90)`,
`@autoreleasepool`, then the missing opening-device marker. Investigate exact progress across
those boundaries (including sleep/timer behaviour and Foundation/ObjC pool initialization).
**Do not infer a GPU shader failure or success from this absence.** Alarm is currently armed
AFTER sleep; it cannot bound a stall in sleep itself. Process liveness/stack was not collected
from live iOS because USB data transport is unavailable. Recovery cannot inspect the old task.

History: v17 test did not launch because standalone LaunchDaemon edits were ignored by the
consolidated launchd cache. v18 fixed cache and launched it, but jetsam killed it: namespace1,
code7 = per-process memory limit (local XNU headers). Stock D22/D221 daemon default is6MB.
v19 sets a scoped512MB allowance for **only** `dev.lisa.metal-smoke`, both in launch job and
D22/D221 Version4.Daemon.Override. Test reads/raises only its own limit before Foundation.
Do not disable global jetsam or increase all service limits.

### Wi-Fi: real control interface now reached, association still absent

WPSS/ath11k radio starts; `wifid` now runs because cached Apple DT gate was removed.
Latest log has get51 COUNTRY_CODE **answered** at lines6345/6359/6382/6393. v32's actual
firmware-domain query is therefore reached. Repeated set383,4104bytes remains REFUSED; station
reports NOT joined, zero traffic/handshake. No successful scan/join is demonstrated.
Do not blindly ACK383, invent country codes or assume its payload is a scan request.

Bounded IO80211 disassembly and symbols: `gl-v19-io80211-symbols.txt`,
`gl-v19-setioctl.dis.txt`, `gl-v19-scanasync.dis.txt`. iOS16.7 Apple80211ScanAsync still uses
classic request10/5456bytes.383 goes through generic raw CFData handling; meaning unresolved.
Preferences exception observer IS loaded (`LISA EXCEPTION observer active in Preferences`),
but no Preferences exception reason or crash stack was captured. Need actual page failure,
including possible XPC-service failure, rather than assuming every unsupported ioctl is fatal.

### Display / performance / Night Shift

v31/v32 fixed a real direct-write path: `kprintf -> PE_kputc -> fbcon_transmit_data` bypassed
`_cnputs` console mute. Low-level framebuffer writes now honour mute except during panic.
User still sees white mark after this change, so this was NOT a confirmed fix for that mark.
Next distinguish system home-indicator/debug overlay/rendered layer from console corruption.
Latest log shows `first full-screen layer: console muted`.

GLES backend issues real Adreno submissions (GPU durations commonly1–2ms in summary lines).
This is not proof every layer is hardware-rendered or that frame pacing is correct.
Earlier v30 GLES averages18–21ms/frame, driver/submission work6–8ms, GPU wait~0.37ms;
see `gl-v30-render-baseline.txt`. Native Metal test has not reached its device query yet.
Night Shift remains unimplemented/ineffective per user. Latest log contains an ignored all-zero
`display matrix 9`; no proven valid Night Shift colour transform. Existing path is in
`lisa-gl/src/eagl.c` (`panel_matrix_refresh`) and kernel colour-matrix handling.

## Changes made during recent Codex continuation

1. `lisa-metal-userland/src/lisametal.m`: real ordered buffer blit encoder, GPU buffer fill/copy,
   GPU matching-colour texture-region copies, retained resources and device lifetime, finite
   viewport/scissor recording and default restoration, mutex-protected Mesa loader initialization.
   Bounds/device/overlap validation; errors are not reported as successful work.
2. AIR backend canonical `research-hackios-userland/tools/mesa/air/air_metal.c/h`: Gallium
   `resource_copy_region` / `clear_buffer`, new `rwa_render_draw_region` preserving old draw ABI,
   texture bounds overflow fix. Targeted `build-lisa-air.sh` copies to patched Mesa build tree
   without replacing Claude's AIR frontend/drm-shim work. Export checks include four new APIs.
3. `lisa-metal-userland/src/metal-smoke.m`: autonomous native test of4096 buffer bytes, stock
   AppleMetalGLRenderer clearVertex/clearFragment20 AIR, viewport/scissor/default state and
   texture-copy pixel regions. AegirPoster is added to native allowlist ONLY after all checks
   pass; this has NOT happened. GUI remains GLES. Native selection via environment
   `LISA_NATIVE_METAL=1` or `/usr/local/lib/lisa/native-metal-processes`.
4. `lisa-gl/sync-launchd-jobs.py` + deploy script: synchronizes ONLY native test, wifid and
   voicemail.vmd jobs into `/System/Library/xpc/launchd.plist`. Standalone plist edits alone
   are insufficient. Voicemail disable now actually applies, reducing pointless background work.
5. `lisa-gl/src/brightness.m`: exact ABI-validated Control Center DisplayModule
   `_setBacklightLevel:` bridge, float/double variants, preserves original, forwards to actual
   `kern.lisa_brightness`; lazy bundle loading handled. Availability is enabled only when real
   kernel DCS brightness backend reports active. Loaded in SpringBoard as well as backboardd.
6. `lisa-gl/src/preferences.m`: loads scoped exception observer; no exception swallowing.
7. Kernel `pexpert/arm/pe_serial.c`: console ownership guard described above.
8. Wi-Fi country API: `research-hackios-kernel/wifi/lx/src/lx_80211.c` copies firmware alpha2
   atomically, clears it on unregister; `lx_regulatory_country` exported in `tools/mk-ath11k.sh`.
   Prototype in both `redwood_lx.h` copies, weak no-driver fallback in `lisa_lx.c`, actual get51
   response in `lisa_apple80211.c`. No regulatory/RF programming added. Final linked symbol
   verified external, not weak fallback; ath11k build has0 autogenerated stubs.
9. `diagnostic/lisa-peripheral-map.py`: corrected GPIO cells misparsed as NUL-separated strings;
   regenerated47-node manifest `lisa-peripherals.json`. Lisa active audio hardware is TFA98xx
   amps at I2C98c000 addresses34/35 and WCD938x. POCO AW88261 speaker programming is incompatible.

## Current artifacts and reproduction

- Manifest: `lisa-tools/diagnostic/gl-v32-metal-v19-artifacts.json` (SHA256/byte sizes).
- Boot: `lisa-tools/diagnostic/out-ufs-xnu/lisa-ios-gl-v32.img`,17,920,000bytes;
  SHA256 `38244931eac6a783074b00c5c8c599f67155e4e293e437933ed148e867a08ea1`.
- Symbols: `kernel.gl-v32.unstripped`; loadable kernel: `kernel.gl-v32.macho` in same directory.
- Userdata: `lisa-tools/ios-artifacts/lisa-ios-gl-v19.simg`,24GiB expanded;
  expanded SHA256 `25b9e6f6f69d5da9a959db5cb53c8e878a41e86957c024eb207a3581b2dc846f`.
  Full RAW/FILL verified; flash48/48 succeeded344.575s; boot accepted0.624s.
- Master image **preserve**: `lisa-tools/ios-artifacts/lisa-ios-uncompressed-metal-stage.dmg`.
- Build: `bash lisa-tools/lisa-gl/build.sh`; `GLVER=vNEW bash lisa-tools/lisa-gl/deploy-stage.sh`.
  Kernel: `LISA_WIFI_DRIVER=1 bash lisa-tools/build-lisa-kernel.sh`; Wi-Fi object first via
  `bash lisa-tools/build-lisa-ath11k.sh` when changed. All local Mac builds, no GitHub Actions.
- Repack using STRIPPED Mach-O, not unstripped __LINKEDIT (overlaps extra firmware).
  `diagnostic/repack-sweet-kernel.py` preserves LISAGPUF; validate payload afterward. v32 was
  checked to retain byte-identical196608byte firmware, ADT, bootargs, shim and4GiB RAM span.
- Do not resume full-RAM experiments while diagnosing Metal. Tested span remains
  `0x180000000 + 0x100000000`, with existing reserved-hole treatment.
- UFS log pull (read-only, recovery):
  `bash lisa-tools/pull-lisa-log.sh <device-serial> lisa-tools/diagnostic/NAME.log`.

## Hardware and workflow constraints

Only userdata was flashed; boot uses temporary fastboot boot. Preserve bootloaders, recovery,
modem, persist, GPT and calibration. User authorizes userdata erase/flash, but explicitly needs
USB unplug/replug after every mode switch, and fastboot restart/replug AFTER each userdata
flash BEFORE boot. Wait for their reconnect reply. Use256MiB fastboot transfers. No more
phone actions were requested/performed in this handoff turn.

USB ECM is still blocked at USB-only SMMU DMA handover preflight, so do not promise live USB
logs or SSH. Bluetooth needs WCN6750 HCI/UART; NFC has no usable driver; cellular needs
MPSS/QMI/RmNet/CommCenter; audio needs Lisa PCM/codec/amp support; sensors need actual SSC/HID
integration. These are not enabled by registering service names. See `lisa-peripherals.json`,
`lisa-modem-soc.args` and the older comprehensive handoff for hardware evidence.

## Recommended next work (not performed this turn)

1. Establish native-test liveness and exact stop boundary before GPU opening. Bound sleep
   itself, distinguish C delay, Foundation/ObjC initialization and loader/device calls. Require
   actual byte/pixel PASS results before switching the system compositor to native Metal.
2. Capture the real Wi-Fi page/XPC failure; identify requests96/97/383 from this exact iOS
   version. Preserve actual regulatory-domain reporting and unsupported-error semantics.
3. Investigate the persistent mark and frame pacing independently; the low-level console fix
   did not remove the observed mark. Verify Night Shift matrix/control path instead of assuming
   an all-zero ignored matrix is a working transform.
4. Implement remaining hardware against Lisa's own DTB and firmware, especially different
   audio amps. Avoid another broad simultaneous RAM/GPU/peripheral experiment.
