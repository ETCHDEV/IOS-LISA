# Lisa storage research

References inspected on 2026-10-21:

- `research-mu` revision `42fe563791e51bf5500c2e470ed294ca0c94b97c` (Project-Silicium/Mu-Silicium).
- `research-woa-lisa` revision `68ff581b3a0f4479f82c623d62339af769ee3a67` (n00b69/woa-lisa).

Mu's Lisa platform includes Qualcomm `HALIOMMUDxe`, `UFSDxe` and `UsbMsdDxe` binaries. Its open-source MassStorage application obtains the UFS LUN's UEFI Block I/O protocol, passes it to the firmware USB mass-storage protocol and services events. This is not a standalone UFS transport or an XNU driver. The Binaries submodule was not fetched, and even with it the UEFI protocol implementation cannot simply be called by XNU after a conventional handoff. Mu's Lisa status marks internal storage working but mass storage unstable on large transfers.

Kodiak's `PlatformInitialize()` invokes `ArmSmmuDetach`, preserving display stream IDs 0x900, 0x901 and 0xd01. That routine iterates stream-matching entries, changes non-skipped entries to fault, invalidates them, and clears context banks except those associated with skipped streams. It operates during Mu's own firmware initialization, before its Qualcomm UEFI drivers take ownership; it does not demonstrate that blindly using physical addresses in XNU is safe. The live Android tree identifies UFS stream 0x80, but the ABL stream table, firmware-owned DMA activity, fault policy, and reinitialization sequence are not established. In particular the detach routine is not evidence that the resulting SMMU default is bypass. Do not apply this broad detach to a running Android system or copy it into XNU without a controlled, target-specific design.

Mu's Lisa memory map lists SMMU 0x15000000 and display reservation 0xe1000000 + 0x02400000; these are corroborating firmware configuration, not an authoritative map of the running Android or XNU boot. Kodiak describes 8 CPUs in 3 clusters and uses the virtual timer. The current generated XNU ADT's homogeneous topology is not verified against this.

Read-only Android sysfs on the connected device reported `/dev/block/sda33` for userdata, `sda` logical block size 4096, sda size 244940800, partition start 23740416 and partition size 221200344. Linux sysfs block `start` and `size` values are 512-byte sectors, not 4096-byte logical blocks. These readings describe the current boot only; they are not a verified GPT backup or a proposed edit. Existing locally saved Lineage boot_a, vendor_boot_a, dtbo_a and vbmeta_a are in `ios-sweet/backup`; no current full-LUN or GPT backup has been verified.

The woa-lisa partition guide removes/recreates userdata and changes the GPT for Windows/ESP; those specific instructions are not an iOS layout and do not supply the missing XNU UFS driver. Existing Sweet UFS code finds a GPT partition by name, default `userdata`, and restricts its I/O to that partition. Any split would need an explicitly selected iOS partition name and validated boundary arithmetic. Do not modify GPT or flash the local iOS research image before the storage path is validated and a concrete layout and recovery backup exist.
