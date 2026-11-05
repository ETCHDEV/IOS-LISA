# Lisa USB logging investigation

The current Lisa XNU kernel cannot stream logs over USB. Sweet's logger is
started from `sweet_ufs_init`, which patch 0082 deliberately disables. The
console ring is still filled in RAM by `osfmk/console/serial_console.c`;
the persistent log thread must remain disabled, as it writes userdata.

The Lisa live FDT describes a `qcom,dwc-usb3-msm` wrapper at `0x0a600000`,
with a Synopsys DWC3 child at that base. This matches Sweet's controller
address, but the Lisa wrapper also has an IOMMU and an Android DMA address
pool at `0x90000000 + 0x60000000`. Sweet allocates contiguous physical RAM
and writes those addresses straight into DWC3 DMA registers. The candidate
Lisa XNU RAM begins above 4 GiB. The FDT alone does not establish how ABL
leaves the SMMU configured or whether the device can address that RAM.

Lisa's USB2 PHY is `qcom,usb-hsphy-snps-femto` at `0x088e3000`; Sweet's
probe assumes a QUSB2 block at `0x088e2000` and Sweet-specific clock offsets.
Do not enable the existing Sweet probe or gadget on Lisa without verifying
the power/clock state, SMMU domain and DMA addressability, and adapting the
PHY and wrapper handling. Its gadget writes DWC3 and QSCRATCH registers and
starts DMA; it is not merely a read-only logger.

An eventual port should start independently of UFS, after the networking
stack is ready, and stream the existing in-memory ring with `sweetlog=0`.
Test enumeration and DMA bounds before enabling network logging. The Sweet
UDP logger currently advances its ring cursor before a failed socket send;
fix that retry behavior to avoid dropping log chunks. The existing Linux
host setup script also needs a macOS-specific interface/address setup.
No logging or USB controller changes have been enabled by this investigation.

The Android live FDT also reserves a 2 MiB ramoops pmsg area at
`0xa9000000`; `/dev/pmsg0` exists in Android. This is a possible later
reboot-recovery channel, not a working XNU logger: the boot path may clear
or reuse the area, and its layout, contents, and ownership have not been
verified. No writes to this region have been attempted. The current ADB
shell is unprivileged and cannot list `/sys/fs/pstore`; this does not prove
whether any records are present.
