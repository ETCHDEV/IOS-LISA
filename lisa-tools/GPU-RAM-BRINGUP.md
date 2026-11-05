# Lisa GPU and RAM bring-up order

Lisa matches the Poco/X5-Pro reference at the critical A642L blocks: GPU `0x3d00000`, GMU `0x3d6a000`, GPUCC `0x3d90000`, and KGSL SMMU `0x3da0000`. The reference driver’s safe sequence is: read-only identity (steps 1–9), allocate GMU memory (10), validate TrustZone zap signature (11–12), create SMMU mappings (13), load GMU firmware (14), wait for GMU ready (15), create HFI queues (16), then power/start GX and submit a fence (17–21). Each stage must leave the block quiesced on timeout; never jump directly to GX power or a command submission.

The local payload now carries `a660_gmu.bin`, `a660_sqe.fw`, `a660_zap.mdt`, and zap segments in `/chosen/memory-map/LisaGPUFirmware`. `lisa_gpu_firmware_file()` validates that container before the staged driver uses it. GPU activation remains opt-in until all step boundaries link and the failure unwind is compiled.

Lisa RAM is not contiguous. The verified bootstrap bank is `0x180000000 + 0x70000000` (1.75 GiB); the second bank is `0x200000000 + 0x80000000`. The `0x1f0000000..0x200000000` hole must not be passed as ordinary RAM. Full RAM requires XNU physmap, PA/VA translation, pmap, and vm_page bootstrap to enumerate ranges before releasing any second-bank pages.
