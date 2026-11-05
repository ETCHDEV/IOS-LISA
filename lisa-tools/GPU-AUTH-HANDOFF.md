# Lisa Adreno 642L GPU authentication blocker

## Proven on physical Lisa
- XNU/iOS boots with 8 CPUs and the safe contiguous 4 GiB high RAM bank (`0x180000000 + 0x100000000`).
- GPU CX domain powers on and stays stable.
- GPU clocks measured: GMU 200 MHz, hub/AHB 150 MHz.
- GMU always-on counter is 19.2 MHz.
- RPMh and SMMU registers are accessible.
- GMU DTCM writes/readbacks succeed.
- TrustZone accepts capability queries for PAS init/auth functions.

## Blocker
`step_zap_signature()` in `bsd/dev/lisa_port/lisa_adreno_reference.c` panics when calling Qualcomm PAS INIT_IMAGE for `a660_zap.mdt`. Standard (`0x42000201`) and fast (`0xc2000201`) 64-bit SCM conduits both panic before the persistent logger flushes.

This prevents loading the signed zap shader, so GMU firmware, GX power, queues, `LisaGPUStart`, GPU CoreAnimation, and Metal cannot be enabled.

## Files
- Driver: `lisa-tools/darwin-xnu-build/xnu/bsd/dev/lisa_port/lisa_adreno_reference.c`
- Lisa signed firmware: `lisa-tools/lisa-firmware/gpu/a660_zap.mdt`, `.b00`, `.b01`, `.b02`
- Poco/Redwood reference: `lisa-tools/research-hackios-kernel/xnu/files/bsd/dev/redwood_adreno.c`
- Full-stage log: `lisa-tools/diagnostic/logs/gpu-full-v2-panic.log`

## Next implementation task
Replace `zap_header()`'s generic `kmem_alloc` buffer with a Qualcomm EL2/TrustZone-approved shared-memory allocation for Lisa, then pass that physical address through PAS INIT_IMAGE. Do not add more XNU RAM ranges: the unexposed Lisa RAM is fragmented/reserved by firmware, modem, DSP, and hypervisor.
