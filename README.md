# IOS-LISA

**iOS 26 on the Xiaomi 11 Lite 5G NE (`lisa`, Qualcomm SM7325, Adreno 642L).**

This is an experimental port. It runs Apple's open-source XNU kernel (xnu-12377.121.6) on `lisa`, with the iPhone18,2 iOS 26.5.2 (23F84) userland rewritten for a CPU without pointer authentication. The kernel is RAM-booted with `fastboot boot`, so the boot partitions stay untouched.

> This is a research bring-up, not a usable phone OS. Installing a root image **overwrites the `userdata` partition**. Not affiliated with Apple or Xiaomi.

## Status

From the project's notes, last updated 2026-11-04:

| Area | State |
| --- | --- |
| Boot | `fastboot boot` of a framed image; no UEFI, 4 KiB kernel |
| Storage | UFS read/write through a ported host driver; Apple's APFS driver from the KDK mounts the root read-only |
| Userland | launchd runs, 170+ programs start, SpringBoard starts |
| Display | CPU copy to the firmware framebuffer, about 105 ms per frame; output confirmed on the OLED |
| CPUs and memory | 8-core device tree in use on recent images; 4 GiB window |
| Data volume | Lives in RAM, resets on every boot |
| Touch | Lisa's Goodix driver is being ported; not confirmed working on iOS 26 |
| Metal and GPU | Rebuild in progress (NativeMetal, metal2vulkan, Turnip). Not confirmed driving the UI |
| Wi-Fi, Bluetooth | Not ported to iOS 26 |
| Camera, flash | No driver exists |

Scripts and logs continued to change through 2026-11-05 (touch patch, GPU firmware, Metal diagnostics), after the last written note. The newest logs are not in this repo.

An older iOS 16.7 path (xnu-8796, `lisa-build`, `darwin-xnu-build`) reached the UI and is kept for reference and as a source of Lisa drivers.

## Repository layout

| Path | Contents |
| --- | --- |
| [`BUILDING.md`](BUILDING.md) | Setup, inputs, build and test loop, rules |
| `IOS26-PORT-PLAN.md`, `CODEX-HANDOFF-IOS26.md`, `CODEX-TO-CLAUDE-2026-11-04.md` | Current plan, how the pieces fit, latest handoff |
| `STATUS-2026-11-03.md`, `LISA-*.md`, `HANDOFF-*.md` | History and iOS 16 notes |
| `lisa-tools/ios26/lisa/` | iOS 26 build, image and test scripts |
| `lisa-tools/diagnostic/` | Boot payload and loader sources, image tools |
| `lisa-tools/` (other) | iOS 16 kernel, GL, Vulkan and Metal tooling, firmware helpers |
| `lisa-build/` | Lisa bring-up fork of ios-sweet: shim, tools, XNU patches. Git history in `deps/history/lisa-build.bundle` |
| `deps/lock.tsv` | Every upstream checkout, pinned to a commit |
| `deps/patches/`, `deps/overlay/` | Our uncommitted changes to those checkouts; `scripts/bootstrap.sh` reapplies them |
| `scripts/` | `bootstrap.sh`, `snapshot.py`, `backup-before-reset.sh` |

## What is not in this repository

Apple firmware and KDK contents, decrypted images, built kernels and root images, Xiaomi and Qualcomm firmware, device dumps, logs, and the private owner signing key. See [`BUILDING.md`](BUILDING.md) for where each comes from.

## Contributors

- [ETCHDEV](https://github.com/ETCHDEV): the Lisa port and project owner
- AI assistants used during development: Claude (Anthropic) and Codex (OpenAI), as recorded in the handoff notes

This port builds directly on the upstream projects below and their authors.

## References and credits

Each project keeps its own license, so check its repository before reusing code. The exact commit of each is in `deps/lock.tsv`.

**Kernel and iOS 26 route**
- [none24smu/UniXNU-zeta](https://github.com/none24smu/UniXNU-zeta): reference iOS 26 port (SM8350); userland rewrite, APFS and prelink tools
- [none24smu/UniXNU](https://github.com/none24smu/UniXNU) and [UniXNU/UniXNU](https://github.com/UniXNU/UniXNU), with the [UniXNU](https://github.com/UniXNU) driver and kit repos; authors include none24smu, rpone, bluseliu50, xVanTuring and LiangKam
- [apple-oss-distributions](https://github.com/apple-oss-distributions): xnu, dtrace, bootstrap_cmds, AvailabilityVersions, libdispatch, libplatform, libpthread, IOStorageFamily
- [apple/corecrypto](https://github.com/apple/corecrypto) and [ONLYA/corecrypto-2023](https://github.com/ONLYA/corecrypto-2023)
- [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet): iOS 16 on `sweet`, the base of the boot shim and early Lisa work; authors include damjankakak3, dipa-studio and legendxdevil
- [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build) and [blacktop/ipsw](https://github.com/blacktop/ipsw)
- [dortania/KdkSupportPkg](https://github.com/dortania/KdkSupportPkg): KDK mirror

**GPU and Metal**
- [steelbrain/metal2vulkan](https://github.com/steelbrain/metal2vulkan)
- [UniXNU/AdrenoMetal](https://github.com/UniXNU/AdrenoMetal) and [RigoLigoRLC/unixnu-adrenometal](https://github.com/RigoLigoRLC/unixnu-adrenometal)
- [ilertnost/Adrenoline](https://github.com/ilertnost/Adrenoline)
- [oosexgang/hakios-metal](https://github.com/oosexgang/hakios-metal)
- [Mesa](https://mesa3d.org/) (Turnip, freedreno, IR3)

**Lisa hardware and other ports**
- [oosexgang/hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel) and [oosexgang/hackios-xnu-userland](https://github.com/oosexgang/hackios-xnu-userland)
- [Abhi-Flex1/ios-lisa](https://github.com/Abhi-Flex1/ios-lisa)
- [n00b69/woa-lisa](https://github.com/n00b69/woa-lisa)
- [Project-Silicium/Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)
- [AistopGit/GT9897T-Windows-driver](https://github.com/AistopGit/GT9897T-Windows-driver)
- [xiaomi-lisa-resources/proprietary_vendor_xiaomi_lisa](https://github.com/xiaomi-lisa-resources/proprietary_vendor_xiaomi_lisa)
- [Linux `goodix_berlin` driver](https://github.com/torvalds/linux/blob/master/drivers/input/touchscreen/goodix_berlin_core.c) (protocol reference; GPL-2.0-or-later)
- [theos/sdks](https://github.com/theos/sdks)

## License

No project-wide license has been chosen yet. Upstream code and patches to it stay under their original licenses (APSL for XNU, BSD-2-Clause-Patent for UniXNU, and so on).
