# HackiOS adaptation for Lisa — 2026-10-23

Sources: `research-hackios-kernel` and `research-hackios-userland` (local shallow clones). Upstream claims Adreno 642L acceleration for CoreAnimation through Mesa/freedreno OpenGL ES on redwood; Metal for apps remains in progress. Neither claim has been tested on Lisa. Existing Lisa iOS userdata and working fastboot XNU image must be preserved as the fallback.

## Touch

Redwood `xnu/files/bsd/dev/redwood_goodix.c` is a useful GENI FIFO, GPIO and SPI implementation for the same SoC. It uses SE13, matching Lisa's live DTB at `0xa94000`, plus IRQ GPIO81 and reset GPIO105. Lisa's panel is GT9897T, whereas redwood targets GT9916/FocalTech: adapt IC-info, event framing and HID delivery using `darwin-xnu-build/xnu/bsd/dev/lisa_goodix_berlin.h` and `sweet_touch.c`. Do not blindly copy redwood panel selection, GPIO107 analog-power control or reset and RPMh votes: Lisa's DTB names avdd/iovdd regulators, not GPIO107, and supplies/power sequencing need verification. Existing synthetic Goodix tests are not a hardware test.

## Battery

Redwood `xnu/files/bsd/dev/redwood_adsp.c` includes PMIC_RTR_ADSP_APPS GLINK and battery property handling, with `redwood_battery.c` and `iokit/Kernel/RedwoodBattery.cpp` publishing measurements. Lisa recovery independently exposes the same `soc:adsp.PMIC_RTR_ADSP_APPS` RPMSG service. This is a promising basis, not evidence that its ADSP boot, carveouts, firmware or ownership assumptions match Lisa. Redwood documents that charging under its kernel does not work. The current Lisa 4250 mAh fallback is static, not a live measurement. Do not enable PM6150 gauge code or issue charger controls on Lisa.

## Graphics and boot

Redwood `redwood_adreno.c`, userland `userland/src/eagl.c`, and the Mesa build/patches are a coordinated GPU stack, not a single kext. Lisa must provide its own a660 GMU/SQE/ZAP firmware, correct reserved memory and clock/SMMU setup, and compatible userland. The Redmi/Poco prebuilt UEFI and boot image are device-specific; do not flash or boot them on Lisa. Recovery did not expose `/vendor/firmware` at the time of inspection.

The upstream porting guide requires a Lisa-specific Mu-Silicium UEFI, shim, loader platform, and verified firmware. This workspace already has a Lisa Mu device tree and a working direct-boot XNU/iOS setup; preserve both while comparing approaches. The prebuilt redwood bootloader is not a Lisa image. Start with a source-level dependency and memory-map comparison before porting any hardware-writing code. All currently described integrations remain untested on Lisa.

## Local adaptation

`0103-lisa-goodix-spi-adapter.patch` builds GT9897 four-dummy-byte read framing, validated IC-info discovery at `0x10068`, and event-only acknowledgements into Sweet XNU. The simulated bus test exercises discovery, failed checksum, frame parsing and acknowledgement. `0104-lisa-geni-spi.patch` adds an adapted GENI FIFO transport; its fake-register tests cover FIFO transfers, bounds, busy checks and timeout. The two pieces are not connected to real MMIO or a worker yet: they do not enable clocks, regulators, pins or reset and cannot provide physical touch.

Lisa recovery exposed `/firmware/image/adsp.mdt` and `adsp.b00` through `adsp.b36` on a read-only mount; they were copied to `lisa-tools/lisa-firmware/adsp`. The metadata SHA-256 is `6313bf467ede94ee8b9b69a24e3f1e28fa7bf72e4c2d438b1fee26d7b1a95f2e`. Lisa DT and Redwood agree on an ADSP reserved region at `0x86700000` of 64 MiB, but this alone does not prove it is unused by the XNU loader or make DSP startup safe. Redwood's vendor battery properties differ from generic upstream Linux starting at temperature; verify Lisa's actual firmware ABI before using its IDs. Firmware is device-owned and must not be committed.
