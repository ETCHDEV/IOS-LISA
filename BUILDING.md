# Building

> **Tested so far:** `scripts/bootstrap.sh` on a fresh clone. The build steps below are the ones used on the original machine, but they have **not** been rerun from a fresh clone. Expect to fix small things the first time.

## Rules

- Boot kernels with `fastboot boot` only. Never flash boot partitions, `dtbo` or `vbmeta`.
- Installing a root image overwrites `userdata`. Back the phone up first.
- Do not run the test scripts against a phone you are not willing to lose data on.

## 1. Requirements

- Apple Silicon Mac. Developed on macOS with Xcode 27.2 at `/Applications/Xcode.app`; the upstream port used Xcode 26.6
- About 120 GB free for the full tree (`lisa-tools/ios26` alone is about 60 GB)
- Xiaomi 11 Lite 5G NE (`lisa`) with an unlocked bootloader and a custom recovery (Lineage or OrangeFox)

```bash
brew install git llvm lld meson bison flex cmake e2fsprogs qemu rustup shaderc glslang vulkan-headers
```

```bash
brew install --cask android-platform-tools
```

## 2. Get the source

```bash
git clone https://github.com/ETCHDEV/IOS-LISA.git
```

```bash
cd IOS-LISA && scripts/bootstrap.sh
```

This fetches each upstream repo at the commit pinned in `deps/lock.tsv`, then reapplies our changes from `deps/patches` and `deps/overlay`. Add `--all` for the iOS 16 kernel tree, Mesa 25.0.7 and the reference repos.

**Keep these trees' local changes.** `research-UniXNU-ios26` and `ios26/gpu/U` are uncommitted on top of upstream by design. Never `git clean` or `git reset` them. To record new work in this repo, see step 7.

```bash
python3 -m venv lisa-tools/ios26/lisa/.venv
```

```bash
lisa-tools/ios26/lisa/.venv/bin/pip install -r lisa-tools/ios26/lisa/requirements.txt
```

```bash
export LISA_SERIAL=your-fastboot-serial
```

## 3. Get the inputs

These cannot be stored here. Paths are relative to `lisa-tools/`.

| Input | Path | Source |
| --- | --- | --- |
| iOS 26.5.2 IPSW | `ios26/iPhone18,2_26.5.2_23F84_Restore.ipsw` | [Apple](https://updates.cdn-apple.com/2026SpringFCS/fullrestores/140-26432/CF51E589-7C43-4B55-B909-CF585DAD0570/iPhone18,2_26.5.2_23F84_Restore.ipsw) |
| `ipsw` tool | `ipsw` | [blacktop/ipsw v3.1.739](https://github.com/blacktop/ipsw/releases/tag/v3.1.739) (`ipsw_3.1.739_macOS_arm64.tar.gz`) |
| Decrypted OS and SystemOS | `ios26/raw/OS.raw`, `SystemOS.raw` | From the IPSW: extract `094-54256-108.dmg.aea` and `094-54345-109.dmg.aea`, then `./ipsw fw aea -o DIR FILE` |
| KDK for xnu-12377 | `ios26/kdk/expanded` | `build-xnu-12377.sh` downloads it from a third-party mirror and checks a pinned SHA-256; expand the package to this path for `build-apfs.sh` |
| Lisa firmware | `lisa-firmware`, `ios26/stock-firmware` | Your own phone's partitions and vendor image |
| Live device tree | `../ios-sweet/dtb/lisa-live.dtb` | Your own phone, from recovery. Contains its serial, so keep it private |
| Boot image template | `diagnostic/out-ufs-xnu/lisa-ios-gl-v41ma.img` | `mk-lisa26-image.py` takes its header and ramdisk prefix from this iOS 16 image. Keep a copy; rebuilding needs the full iOS 16 path |
| Data volume seed | `ios26/work/data-seed.dbr`, `.json` | `ios26/lisa/make-lisa-data-seed.py`. Rebuild the kernel if the seed changes |
| Owner signing key | `ios26/lisa/.cache/owner-root` | **Private and never committed.** The public half is `ios26/lisa/lisa-owner-root-key.h` and `scripts/zeta-owner-root-key.h`. A new key means re-signing the root and rebuilding the kernel |

## 4. Kernel and image loop

This is the normal loop. It only needs a root already on the phone.

```bash
bash lisa-tools/ios26/lisa/build-kernel.sh NAME
```

```bash
bash lisa-tools/ios26/lisa/build-apfs.sh IMG "boot args"
```

```bash
bash lisa-tools/ios26/lisa/run.sh IMG
```

```bash
bash lisa-tools/ios26/lisa/brief.sh IMG
```

- `build-kernel.sh` applies the patch series to a generated copy of XNU, then builds it. Lisa's kernel changes are in `research-UniXNU-ios26/patches/xnu-12377/zzz*-lisa*.patch`. It needs normal macOS `sysctl` access, so it fails inside a restricted sandbox.
- `build-apfs.sh` prelinks the KDK's APFS and AMFI drivers as plain arm64, and writes `ios26/lisa/out/IMG.img`.
- `run.sh` boots it with `fastboot boot` and collects the log from recovery into `ios26/lisa/logs/`.
- Recent boot arguments are in `ios26/lisa/*-args.txt`. Keep them under the payload limit noted in `IOS26-PORT-PLAN.md`.
- `sym.sh SLIDE ADDR...` turns shared-cache addresses into names.

If `fastboot` wedges after `adb reboot bootloader`, restart with Power + Volume Down.

## 5. Rebuilding the root image

This takes much longer and needs extra disk. [`CODEX-HANDOFF-IOS26.md`](CODEX-HANDOFF-IOS26.md), section "Changing the root image", has the seven steps: decrypt, mount with `mount-roots.sh`, rewrite with `rewrite-userland.sh`, build the cache and trust bundle, grow the APFS image, write files through a Linux guest under QEMU, then `build-lisa-root.py`. The guest parts in `ios26/lisa/vm` are not stored here (`Image`, `vmlinuz`, `apfs.ko`, `busybox`).

Stream the result to `userdata` from recovery and compare hashes. Plain `fastboot flash` failed with this image.

## 6. Other paths

- **GPU work:** `lisa-tools/ios26/gpu/U` is a UniXNU tree with the Adreno and Metal changes, driven by `ios26/gpu/run-ci.sh`. It needs a Mesa work tree, which `bootstrap.sh` fetches.
- **iOS 16 kernel (xnu-8796):** `lisa-tools/build-lisa-kernel.sh` with `darwin-xnu-build`, after `bootstrap.sh --all`. Do not mix it with the iOS 26 tree. Turnip and metal2vulkan: `build-lisa-turnip.sh`, `build-lisa-m2v.sh`.
- **Rollback to iOS 16:** needs your own copy of `ios-artifacts/lisa-ios-gl-v41.simg`, which is not stored here.

## 7. Recording new work

Files owned by this repo are committed normally. Changes inside a pinned checkout must be captured into `deps/` before you commit:

```bash
python3 -I scripts/snapshot.py "$PWD"
```

Then review `git status` and check that nothing private was picked up.

## Where to read next

Start with `IOS26-PORT-PLAN.md` ("Resume here"), then `CODEX-TO-CLAUDE-2026-11-04.md`, which is the newest note.
