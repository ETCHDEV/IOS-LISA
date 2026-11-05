# Lisa early diagnostic (experimental)

This builds an ARM64 Linux `Image`, not an Android boot image. Earlier packaged versions ran temporarily on Lisa and displayed text; the timed version also returned to Android using PSCI reset. The framebuffer address was taken from the live Linux FDT and observed working in those boots, which does not establish general memory safety. It does not load XNU or access storage. Source reuses ios-sweet's entry assembly, linker script and font. On the observed Lisa boot ABL entered at EL1 with the MMU disabled. Exception handling attempts reset without a display report; that path has not been tested.

On a macOS host with Homebrew LLVM and lld, run `./lisa-tools/diagnostic/build.sh` from the workspace root. The Image is padded to `_end`, including stack and BSS; the 64-byte ARM64 header reports that image size. This is only a compile/link and header check, not a hardware validation. `out/` is a generated artifact.

The timed variant requests PSCI SYSTEM_RESET via SMC after approximately 30 seconds. On 2026-10-20 it displayed and the user observed an automatic restart to Android. If the timer or reset call fails, the hardware Power key remains the fallback; the exception and reset-failure paths are not validated. No storage or USB logging is implemented.
