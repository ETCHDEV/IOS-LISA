# Handoff Claude -> Codex, 27 Oct 2026 ~12:56 IST (READ FIRST; supersedes CLAUDE-NEXT-2026-10-27.md)

Workspace /Users/arjun/IOS-LISA. Phone serial <device-serial>. Job runner: `lisa-tools/agent-queue/*.job`
(runs with cwd ~/IOS-LISA, one at a time; it globs at the START of a pass, so a job added mid-pass runs
next pass; finished jobs become .done/.out/.rc). Do NOT use `set -o pipefail` in jobs that poll fastboot
(`P=$(timeout 5 fastboot ... | awk ...)` then aborts with 124). User must unplug/replug after every
mode change; after a userdata flash the job reboots to bootloader and waits for the replug.

## State right now
- Userdata v25 flashed (lisa-tools/ios-artifacts/lisa-ios-gl-v25.simg). Kernel v40m is being booted
  (Metal compositor test). Job 152 pulls its log to lisa-tools/diagnostic/gl-v40m2-recovery.log when the
  phone is back in recovery.
- Queued AFTER that (not yet run): 153-kernel-v41.job (builds kernel, makes lisa-ios-gl-v41.img = GL and
  lisa-ios-gl-v41m.img = Metal compositor), 154-userland-v26.job (build.sh + deploy -> v26 simg).
  NOT queued: flash v26 + boot v41m, and pull. Copy 151-flash-boot-v40m.done -> new job with
  v25->v26 and v40m->v41m, and 152 -> pull v41m.
- Boot images: GL daily driver = v36/v39/v40/v41 (cmdline of v36). Metal compositor = *m images
  (cmdline v38 = v36 - lisagpu_probe=1 + lisa_native=backboardd). NEVER remove
  `unentitled_ios_sim_launch=1` (I did once: launchd CS_KILLED panic). Cmdline limit 606 chars.
- Build: kernel `LISA_WIFI_DRIVER=1 bash lisa-tools/build-lisa-kernel.sh` (stripped result is
  darwin-xnu-build/fakeroot/System/Library/Kernels/kernel.development.sweet), repack with
  diagnostic/repack-sweet-kernel.py onto an existing image, then replace-payload.py. Userland:
  `bash lisa-tools/lisa-gl/build.sh` then `GLVER=vN bash lisa-tools/lisa-gl/deploy-stage.sh`.
  Mesa AIR frontend that is ACTUALLY compiled: research-hackios-userland/xnu-deps/build/mesa/mesa-25.0.7/
  src/compiler/air/ (build-lisa-air.sh copies only air_metal.c/.h from tools/mesa/air).
- Disk: the Claude Cowork Linux VM (Virtualization.framework process) held deleted simgs open
  (133 GB). If free space does not come back after deletes, `lsof +L1` and kill that VM process.
  Keep only the current simg + master DMG (lisa-ios-uncompressed-metal-stage.dmg) + IPSW.

## What was fixed today (verified on phone unless noted)
1. LAG, Codex bug: LisaAdrenoUserClient submit used MiniIOSurface::drawn (the GPU fence the same code
   sets) as the CPU-write counter -> every surface cache-cleaned on EVERY submission. Now uses the
   IOSurface state-page seed; physical/WC panel never; poolable display buffers only on seed change
   (`gpu_seedfix=0` = old). 25 MB -> 19 MB per submit.
2. LAG: all cores CLUSTER_TYPE_SMP -> render thread on Cortex-A55s. osfmk/arm64/lisa_bind.c binds
   backboardd's GPU-submitting thread to core 7 (`gpu_bind=N`, -1 off). Frame avg 14 ms (was 16-19).
   Real fix still TODO: cluster-type E/P in ADT + an AMP-aware scheduler config.
3. SweetSurface pool: reuse released 1125x2436 backboardd surfaces with the same id (`lisa_surf_pool=0`).
4. White 1-2 px mark = XNU boot progress meter timer: vc_progress_set(FALSE) at first full-screen layer.
5. Night Shift: eagl.c colour matrix is s31.32 fixed point (fixed). BUT CoreBrightness never sends
   SetMatrix after boot and the per-swap record has no colour (words 0x250-0x260 are the damage rect).
   Night Shift STILL NOT WORKING: find what capability CoreBrightness checks (likely IOMFB property).
6. Diagnostics (kernel log): LISA EXIT (every signal death), LISA JETSAM, LISA SAY (kern.lisa_say: any
   process writes a line), LISA CRASH (exceptions.m signal backtraces; also linked into wifid via
   lisa-gl/link-compat.py), LISA HANG (Preferences/SpringBoard main-thread sampler after 2.5 s),
   LISA FB swap-record word changes, MISSING METHOD (Metal).
7. 64-byte surface rows (SweetSurface create; `lisa_surf_align=0`): freedreno refuses other pitches.
8. Kernel CPU copy of swapped frames now `dc civac` first (Metal frames came out black from stale cache).
9. Metal driver (lisa-metal-userland/src/lisametal.m):
   - generateMipmapsForTexture: (Mesa util_gen_mipmap via new rwa_texture_generate_mipmaps in
     tools/mesa/air/air_metal.c) - it killed backboardd 28x.
   - unknown protocol methods answered with nil/0 and logged once (`lisa_strict=1` = exception).
   - newTextureWithBytesNoCopy: now COPIES the caller's memory (layout = LisaMetalTextureLayout),
     deallocator runs when the texture dies (was nil -> icons/wallpaper missing).
   - lisa_native=a,b boot-arg selects native Metal per process (plus native-metal-processes file).
   - device name "Adreno 642L (chip ..., Lisa)"; IORegistry LisaWLANController/LisaWLAN.
   - metal-smoke test (dev.lisa.metal-smoke): v36 PASS device, buffer fill/copy, stock AIR render 16
     pixels. Its viewport FAIL was a TEST bug (default clearColor alpha 1): fixed in v23+.
   "Metal test stops after 5 s" in v19-v34 was LOST STDOUT, not a kill.
10. AIR->NIR: insertelement with a run-time index via nir_vector_insert (in the Mesa-tree copy).

## Metal compositor status (v40m, 12:44): iOS boots and is interactive through Metal but: heavy flicker,
only some content (dock icons, text), Control Center broken, slideshow speed, then SpringBoard killed by
RunningBoard (ns 15). Fixes for missing content (#9 BytesNoCopy, #10 shader) are in v25 = the boot running
NOW - read gl-v40m2-recovery.log first: `grep -a "MISSING METHOD\|refused\|no drawable\|LISA EXIT"`.
Known still broken: RGBA16Float IOSurface import refused ("would not take IOSurface ... RGBA16Float");
synchronous command-buffer execution (-commit runs and waits) -> must become asynchronous;
display path.
Speed fix READY in source, built by 153/154 (not tested): liblisagl `redwood_gl_metal_presenter()`
(eagl.c, own GL context thread; polls new kern.lisa_swap_last, imports the swapped surface by id,
panel_present() draws it onto the panel by the GPU, writes kern.lisa_gpu_presented) started by
LisaMetal in backboardd when lisa_native includes backboardd; kernel waits up to 2 vsync ticks for it
before its CPU copy (SweetFramebuffer.cpp present()). Expect "Metal presenter: running" and
"swap N (surface M) drawn onto the panel" in the log.

## Other subsystems
- Wi-Fi: radio + ath11k up; scan list appears in Control Center (v36). Settings Wi-Fi page and the
  password page hang -> watchdog 0x8badf00d (Preferences killed 13x). Read `LISA HANG Preferences`
  stack (added v23) and fix that blocking call (likely a synchronous wifid/Apple80211 request; 383 is
  refused 187x, 103 CURRENT_NETWORK error 6).
- Brightness: Control Center works (DCS 0x51 in bsd/dev/lisa_dsi.c). lisa_hz=90 is in cmdline (panel 90 Hz).
- Still not started on Lisa: audio (Lisa amps TFA98xx + WCD938x, NOT redwood's AW88261; upstream
  redwood_adsp/sound/mic as reference), Bluetooth (WCN6750 over UART; firmware extracted to
  lisa-tools/lisa-firmware/bt), cellular, camera, sensors (SSC on ADSP), USB data (blocked at SMMU),
  HDR, NFC (not feasible). Lisa firmware extracted: lisa-firmware/{wifi,bt,adsp,modem,live (DTB/dts)}.
- Pink Safari background / black folder blur / grey wallpaper previews = CoreAnimation GLES renderer
  limits (function 60 p3c refused). Proper fix = Metal compositor.

## Next steps in order
1. Read gl-v40m2 log. 2. Queue flash v26 + boot v41m, test presenter speed. 3. Make Metal command buffers
asynchronous (completion handlers on a thread) and fix RGBA16Float import. 4. Settings hang from LISA HANG.
5. Night Shift capability. 6. AMP scheduling. 7. Audio port (TFA98xx), then Bluetooth.

## 2026-10-28 17:30 IST (Claude) — Metal black screen: root cause found
- Diagnosis chain (logs in lisa-tools/diagnostic/gl-v41m6..m8-recovery.log):
  - v32/v33 presenter probes: presenter GL context CAN write the panel (clear->readback 0080ffff),
    but Metal-rendered surfaces read back 00000000.
  - lisa-ios-gl-v41mnp.img (v41m with lisa_gpu_present=0, kernel CPU-copies every frame): still black
    => Metal output itself empty, not the presenter.
  - v34 LisaMetal DRAW/PIXELS log: EVERY draw of CA passes carried clear=yes (0,0,0,0).
- ROOT CAUSE: LisaMetalRenderEncoder handed the pass's clear (load action) to every recorded draw;
  air rwa_render_draw clears whenever clear_color != NULL => each draw wiped the previous ones.
- FIX (lisametal.m, end of recordDraw): _clears/_clearsDepth/_clearsStencil = NO after the first draw.
  Built into userdata v35 (job 175). TODO: a pass with Clear and zero draws now clears nothing
  (add a clear-only work at render endEncoding if needed).
- Diagnostics added (can be removed later for speed): lisametal.m DRAW/PIXELS logging in runDraw;
  eagl.c Metal presenter pixel probes + 60-frame heartbeat.

## 2026-10-28 18:50 IST (Claude) — Metal black screen: second root cause (framebuffer fetch)
- v37 log (gl-v41mnp4-recovery.log): boot frame = black fill + 194x237 white logo texture (texel ffffffff)
  + full-screen `_Tlub..._Isrc` pass that READS the destination ([[color(0)]]) -> result 00000000.
- air_to_nir.c had a "Lisa fallback" replacing [[color(n)]] with vec4(0) (txf_ms_fb hung the a660),
  so every CA pass that reads its destination wrote transparent black.
- FIX v38: air_to_nir.c (mesa tree + tools copy) emits txf from texture slot AIR_FBFETCH_SLOT(14)+n at
  ivec2(frag_coord.xy); nir_air.h defines AIR_FBFETCH_SLOT. tools/mesa/air/air_metal.c
  render_draw_region copies the attachment (scissor rect only) into a cached scratch texture before the
  draw and binds it (+default sampler) at slot 14+n; unbound after. Overlapping primitives within one
  draw see the pre-draw state (acceptable for CA quads).
- Test kernel for these runs: lisa-ios-gl-v41mnp.img (presenter off, kernel CPU copy). Once the picture
  is right, go back to lisa-ios-gl-v41m.img (GPU presenter) and debug presenter's panel draw writing
  zeros (eagl.c probes).

## 2026-10-28 19:50 IST (Claude) — Metal: present from the Metal driver itself (v41 userdata)
- v39/v40: CA frames now have real content (navbar/status icons visible), but liblisagl's OSMesa presenter
  (second Mesa instance in backboardd) wrote zeros to the panel, then stalled (v40: presenter clear read back 0).
- New design: tools/mesa/air/air_metal.c rwa_present_panel(device, frame, panel): two scaled pipe->blit
  (even panel rows -> left half, odd rows (src y+1) -> right half of the kernel's 2160x1200 wide view), then finish.
  Exported (build-lisa-air.sh checks the symbol).
- lisametal.m: LisaMetalTexture->_surfaceID (from newTextureWithDescriptor:iosurface:); -commit tracks the
  last 1125x2436 surface-backed render target; -lisaPresent: imports panel surface (IORegistry
  LisaPanelSurface, row 8640) once, presents, writes kern.lisa_gpu_presented=surfaceID.
  Boot-args: lisa_mpresent=0 off; lisa_glpresent=1 brings back the OSMesa presenter; lisa_mdiag=1 per-draw logs.
- air_metal.c: depth-only shaders in colour-only passes -> render->noop (draws skipped) instead of refusal
  (refusal aborted backboardd via QuartzCore).

## 2026-10-28 20:05 IST (Claude) — NO MORE USERDATA FLASHES: USB mass-storage push
- lisa-tools/ums-userdata.sh: in recovery (LineageOS-style, root adb, configfs gadget g1, UDC a600000.dwc3):
  setenforce 0 (SELinux denied file-storage -> block dev), losetup loop over /dev/block/sda33 (sda33 has
  4K blocks; the HFSX "iOSRoot" volume needs 512), adds mass_storage.ums lun -> loop, rebinds UDC (adb stays).
  Mac: diskutil mount (needs writable media even to mount read-only: journaled HFS), cp files, unmount,
  then gadget/loop/SELinux restored. MODE=check (compare) | MODE=push files... (MEDIA_RW=1) | MODE=hold.
- lisa-tools/push-and-boot.sh: build, push given lisa-gl/out files, reboot bootloader, fastboot boot
  $KERNEL, pull log as diagnostic/$LOG-recovery.log. Job 197: liblisair+LisaMetal (fbfetch scratch now
  PIPE_BIND_LINEAR: the red/blue checkerboard) on lisa-ios-gl-v41m.img.

## 2026-10-28 20:40 IST (Claude) — Metal compositor reaches the home screen
- State (LisaMetal + liblisair pushed via push-and-boot.sh, kernel lisa-ios-gl-v41m.img, userdata v41 base):
  boot logo correct, lock screen (black wallpaper: PosterBoard in sweetnull), swipe-up -> home screen with
  icons, widgets, dock; Safari opens. Artifacts: flickering box/blue outlines, half-rendered wallpaper; Safari
  appeared stuck. SCREEN grids in diagnostic/gl-v41m17-recovery.log (brightness digits from panel memory).
- Fixes today in order: per-draw clear (lisametal), fbfetch via scratch copy (air_to_nir + air_metal),
  depth-only pipelines noop, present from Metal driver (rwa_present_panel), FD_MESA_DEBUG=sysmem in
  backboardd (lisa_gmem=1 to keep GMEM: GMEM gave a red/blue block pattern), GPU blits instead of
  resource_copy_region (CPU fallback read stale cached memory) in fbfetch copy and rwa_texture_copy.
- Next: artifacts (outlines: likely AA/coverage or fbfetch per-scissor copy), performance (~450 submits/s,
  ~1 ms cache flush each; make command buffers async; drop lisaPresent probes PRESENT/SCREEN when done).
