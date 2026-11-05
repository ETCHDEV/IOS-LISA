# Lisa UI follow-up — 2026-10-26

## External port reviewed

Repository: https://github.com/Abhi-Flex1/ios-lisa
Pinned checkout: `research-abhi-ios-lisa`, commit
`1faa64d166a8b9cc5ffab876da4d7f8b87b17f44`.

The README explicitly distinguishes demonstrated GPU firmware/command bring-up
from accelerated UI: its recorded SpringBoard run used software rendering.
No demonstrated Wi-Fi, Bluetooth or brightness implementation is supplied.
The useful GLES patch adds reasons why an IOSurface import falls back to a
copy; it does not provide a different accelerated compositor to replace ours.
The DTB is a useful independent hardware reference. Do not apply its old
userdata/Android restore procedures or CI instructions to this working port.

## Current device evidence (v19-control)

`diagnostic/gl-v19-control-recovery.log`: 527439 ring bytes, flush31.
Adreno receives real draws/presents. Simple sampled frames are ~8–16ms, complex
ones ~20–50ms+, so this is not a claim of sustained 60fps. In-place rendering
still has corruption/flicker; CPU submission work and texture import cost are
substantial. Metal remains disabled for the compositor in GLES mode.

- Music: dyld refuses @rpath expansion; its embedded MusicApplication.framework
  actually exists. Current kernel policy was registered as SWEET AMFI, not AMFI,
  and did not implement the dyld syscall ABI.
- Widgets: no extension-point verification service; the AMFI bootarg query and
  Sandbox container-path ABI are missing. HackiOS implements these explicitly.
- PosterBoard: nil object in renderer options dictionary. Symbolication and
  disassembly identify `_imageForView:` creating a Metal command queue with no
  device, then inserting that nil queue into a dictionary. This is not a missing
  wallpaper asset. v20 uses a scoped CoreGraphics still-preview fallback only.
- Preferences: the captured Storage crash is a Swift trap in PodcastsUI because
  MPMediaLibrary.databasePath is nil. The master lacks iTunes_Control parents;
  create those writable directories for medialibraryd, do not fabricate a DB.
  Whether this repairs Storage/About requires the next device run.

## v20 changes to validate

- New kernel AMFI policy name and syscall0x5a (dyld flags0xff), syscall0x60
  (non-enforcing state0x86), matched to HackiOS kern_minimac.c. This preserves
  existing signature/page validation. This research port is not security hardened.
- Sandbox op4 returns the actual CFFIXED_USER_HOME saved in the target's exec
  environment, with PID-version checks and bounded copy. No shared fake container.
- Buffered GLES target: imported texture sampling and full-blit scissor handling
  fixed; previous frame-end eligibility/flush fixes retained.
- PosterBoard-only liblisaposter.dylib replaces `_imageForView:` with a CPU
  still-image preview. Main backboardd rendering stays on Adreno. This avoids
  claiming an unimplemented Metal device; it does not guarantee every layer
  can be represented by CoreGraphics or every wallpaper feature works.
- Media-directory parents created; no fake storage values or SQLite contents.

Full local userdata image v8 is verified. Do not reflash v7. Use -S256M, restart
fastboot and reconnect after flash before temporary v20 boot as the user requires.

## Remaining hardware work

Wi-Fi needs the WPSS/SMEM/SMP2P/GLINK/QRTR startup plus WCN6750 ath11k transport,
board firmware/calibration and an iOS network interface. HackiOS's redwood_wifi.c
and wifi/lx are the actual source reference; Abhi's lisa.conf enables that work
but is not a confirmed working implementation. Bluetooth needs its own HCI
transport/firmware/power control and iOS stack integration; Windows drivers are
not XNU drivers. Brightness needs the CSOT panel's DCS brightness operation via
a working DSI command transport, then the iOS brightness interface. csot.txt has
panel init data; do not replay its entire init sequence into a running panel.
None of those features is claimed implemented in v20.
