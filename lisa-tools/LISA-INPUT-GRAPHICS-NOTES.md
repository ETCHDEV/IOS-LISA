# Lisa input and graphics bring-up

Status: 2026-10-23. The user observed an iOS lock screen and reported smoother animation at a 60 Hz framebuffer copy limit. This is software presentation, not evidence of GPU acceleration. All eight CPU initialization was logged, but workload utilization and clocks have not been measured.

## Physical inputs

The Lisa device tree in `research-mu/Resources/DTBs/lisa.dts` places PMK8350 PON HLOS at SID 0, register base 0x1300, with power and volume-down interrupts 7 and 6 respectively. Linux's [PMIC power key driver](https://github.com/torvalds/linux/blob/master/drivers/input/misc/pm8941-pwrkey.c) defines generation-3 real-time status offset 0x10 and corresponding bits 7 and 6. An opt-in observer-read-only implementation is in XNU patch `../lisa-build/xnu/patches/0101-lisa-pon-keys.patch`; its phone behavior is pending verification. It sends SPMI read requests using MMIO and does not configure PMIC registers. It cannot guarantee an ungated or accessible observer channel on Lisa.

Volume up is active low, on PM7325 SID 1, GPIO6, phandle 0x59c; the device tree identifies the GPIO controller at 0x8800. Its exact GPIO6 peripheral and status address must be confirmed against the PM7325 driver before implementing reads. Do not reuse the Sweet volume-up addresses.

The Xiaomi touch node is on GENI SPI at 0x00a94000, chip-select 0, 6 MHz maximum, IRQ GPIO81, reset GPIO105, 1080x2400, and references GT9897T firmware variants. A host-only Berlin-A decoder and XNU adapter exist; tests pass with synthetic data, not hardware. The transport is absent. Qualcomm's [GENI SPI driver](https://github.com/torvalds/linux/blob/master/drivers/spi/spi-geni-qcom.c) and [GENI serial-engine driver](https://github.com/torvalds/linux/blob/master/drivers/soc/qcom/qcom-geni-se.c) show the need for correct clocks, pinctrl, firmware/protocol mode, FIFO and interrupt handling. Verify ownership and live hardware state before issuing SPI commands; do not reuse the Sweet I2C driver or blindly write regulators or reset GPIOs.

## Graphics

Lisa's device tree identifies `qcom,adreno-gpu-a642l`, GPU registers at 0x03d00000, a separate GMU, SMMU contexts, clocks, interconnects, power domains and thermal constraints. The current XNU framebuffer copies CPU-rendered IOSurface data into the inherited display buffer; raising `sweetfb_hz` only raises the maximum copy frequency. There is no Adreno XNU driver, Metal implementation, or demonstrated GPU workload. Functional acceleration would require safe GPU/GMU power and firmware management, memory mapping and synchronization, a working command-submission stack and a compatible iOS graphics interface; Linux KGSL/MSM code is a hardware reference, not a drop-in Metal driver. Do not claim acceleration or force GPU power/clock settings without an implementation and hardware validation.

## Practical next checks

1. Observe the button-test boot; recover logs via `pull-lisa-log.sh` and verify `LISA KEYS` initialization and button events against physical presses and screen wake.
2. Correlate Lisa's GT9897T vendor driver and firmware with the live DTB, then implement and validate GENI SPI transport incrementally, keeping reset/regulator writes out until ownership and sequencing are established.
3. Measure actual frame generation and copying, CPU scheduling and boot milestones; 60 Hz alone does not establish rendering throughput, all-core utilization or CPU frequency.

## 2026-10-23: touchscreen and battery follow-up

The user confirmed power and volume down work; volume up is not verified. The earlier transport-absent note above has been superseded by experimental opt-in patches 0103–0105. These add GT9897 Berlin-A framing, GENI SPI and a `lisatouch=1` worker. Recovery reported pm8350c_l2 at 1.8 V and pm8350c_l7 at 3.3 V, both enabled; the worker votes only those values and follows the Linux reset and rail order. The new code builds and passes simulated transport tests, but touch hardware is not yet verified. The temporary candidate is `diagnostic/out-ufs-xnu/lisa-ios-touch-0105-v3.img`; it retains the earlier iOS root and 60 Hz settings. It changes GPIO, SPI and PMIC regulator state during the temporary boot, and safe hardware behavior cannot be guaranteed by offline tests.

The displayed battery remains a static fallback, not a measured gauge. Recovery's Lisa ADSP firmware has been copied to `lisa-firmware/adsp` and checked against the Lisa DTS carveout; ADSP boot, GLINK and real battery property transport remain unimplemented and unverified. The 4250 mAh fallback value does not make percentage or charging state real. Preserve the known-working `diagnostic/out-ufs-xnu/lisa-ios-pon-keys-v3.img` as a fallback.

## 2026-10-23: touch clock fallback

0105 was booted; the user reported no touch. `diagnostic/touch-0105-log.txt` records completed ldoc2/ldoc7 RPMh votes followed by `LISA TOUCH: clocks unavailable`, before any SPI transaction. The linked GT9xx Windows project points to AistopGit/GT9897T-Windows-driver, which specifically describes Lisa and uses four SPI dummy bytes and an event address of 0x10338. Patch 0106 adds opt-in GPIO SPI fallback using input GPIO52, output GPIO53, clock GPIO54 and chip select GPIO55, preserving checksummed IC-info discovery before writes. Build and host adapter tests passed; the temporary `lisa-ios-touch-0106-v3.img` was accepted by fastboot, but touch remains unverified. Avoid swapping input and output because contention is possible. The Redwood project uses Adreno via Mesa/freedreno for OpenGL ES; its Metal work is described upstream as incomplete. Lisa-specific GPU firmware and integration remain outstanding.

## 2026-10-23: verified touch, memory and GPU assessment

The user reports working touch with 0106. The recovered `diagnostic/touch-0106-log.txt` reports GPIO SPI input 52 identified, GT9897 event address 0x10338, and 8/8 CPUs initialized with SGI1 verified. This is not a multicore performance or frequency measurement. Payload 0106 declares 512 MiB at 0x180000000; recovery `/proc/meminfo` reports 7447304 KiB total and `lisa-recovery-iomem-20261023.txt` records RAM and reservations. `memory/iomem-ranges.py` subtracts reported reservations for analysis, but the recovery map alone does not establish safe RAM ownership under the iOS loader. The existing preflight hard-limits XNU to 512 MiB; full RAM requires a new range/loader strategy and live reservations handling, not merely changing the ADT.

Lisa and Redwood both report chipid 0x06030500 and GPU MMIO 0x03d00000, but Redwood's `redwood_adreno.c` expects its own firmware, allocator, SMMU and power environment and userland Mesa bridge. Recovery exposes no mounted vendor firmware; Lisa's super partition remains present but its vendor logical partition is not mapped. No GPU firmware was retrieved or GPU MMIO enabled, and acceleration is not implemented. Preserve working touch image `diagnostic/out-ufs-xnu/lisa-ios-touch-0106-v3.img`.

## 2026-10-24 — memory and GPU bring-up checkpoint

- Lisa’s verified temporary XNU interval remains `0x180000000..0x1f0000000` (1792 MiB). The recovery map shows an additional, discontiguous 2 GiB bank at `0x200000000..0x280000000`; it cannot be added by extending `dram-size` because `arm_vm_init` currently assumes one `gPhysBase + memSize` region.
- Added `bsd/dev/lisa_adreno_probe.c`, gated by `lisagpu_probe=1`. It only maps and reads Lisa GPUCC/SMMU identity registers. It contains no MMIO store, clock/rail vote, GMU firmware load, SMMU update, or GPU submission.
- Staged hash-checked Lisa vendor `a660` GPU firmware in `lisa-firmware/gpu/` for a later explicit GMU/SMMU bring-up. This staging does not load it.
