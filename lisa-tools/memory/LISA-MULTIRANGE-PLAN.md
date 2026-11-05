# Lisa multi-range XNU memory plan

## Verified state

The temporary iOS image owns one tested physical interval:

```
0x180000000 .. 0x1f0000000   1792 MiB
```

Linux recovery reports additional system RAM, but it is separated by protected
bootloader/DSP regions and an independent second bank:

```
0x200000000 .. 0x280000000   2048 MiB
```

The first reservation above the current test interval begins at `0x1f8400000`.
No XNU image may enlarge the existing `dram-size` through that reservation.

## Required implementation

`boot_args` and `arm_vm_init()` currently describe physical memory as one
`gPhysBase + memSize` interval. A correct multi-range port needs:

1. An appended, versioned loader-owned physical-range table. It must not change
   the existing `boot_args` fields used by unmodified XNU.
2. Early ARM64 pmap/physmap construction that maps each supplied range and
   rejects overlaps with the kernel, initrd, ADT, and reserved memory.
3. VM page initialization that adds the high-bank pages as an additional free
   range, with physical-to-virtual translation no longer assuming contiguous
   `gPhysBase`.
4. A boot-arg gate (`lisa_multiram=1`) and logging of every accepted range.
5. First tests with a small high-bank slice; no high-bank DMA until UFS/GPU
   mappings use the new translation helpers.

Until those changes exist and boot repeatedly, 1792 MiB is the maximum
validated iOS allocation. The Linux total must not be passed to `dram-size`.
