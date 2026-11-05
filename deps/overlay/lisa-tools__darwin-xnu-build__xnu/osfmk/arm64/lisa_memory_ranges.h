/* Generated from Lisa DTS; do not flatten these ranges. */
#ifndef _OSFMK_ARM64_LISA_MEMORY_RANGES_H_
#define _OSFMK_ARM64_LISA_MEMORY_RANGES_H_

#include <mach/vm_types.h>

struct lisa_memory_range { uint64_t base; uint64_t size; };
extern const struct lisa_memory_range lisa_memory_ranges[];
extern const unsigned int lisa_memory_range_count;
uint64_t lisa_memory_ranges_total(void);
boolean_t lisa_memory_range_contains(uint64_t pa, uint64_t size);

#define LISA_MEMORY_RANGE_COUNT 8
#ifdef LISA_MEMORY_RANGES_DEFINE
const struct lisa_memory_range lisa_memory_ranges[] = {
	{ 0x0000000080894000ULL, 0x000000000006b000ULL },
	{ 0x0000000083600000ULL, 0x0000000002c00000ULL },
	{ 0x000000008a700000ULL, 0x0000000000600000ULL },
	{ 0x000000008b71c000ULL, 0x00000000000e4000ULL },
	{ 0x000000009e500000ULL, 0x000000001b200000ULL },
	{ 0x00000000c5100000ULL, 0x000000000b700000ULL },
	{ 0x00000000d8000000ULL, 0x00000000a8000000ULL },
	{ 0x0000000180000000ULL, 0x0000000100000000ULL },
};
const unsigned int lisa_memory_range_count = LISA_MEMORY_RANGE_COUNT;
#endif

#endif
