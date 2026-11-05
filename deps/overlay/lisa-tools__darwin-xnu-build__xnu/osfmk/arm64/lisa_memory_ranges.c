/* Lisa physical-memory map derived from sm7325-xiaomi-lisa.dts.
 *
 * The map intentionally excludes every `reserved-memory` range marked
 * no-map.  It is an input to the arm64 pmap range iterator; it must never be
 * flattened into physBase + memSize because that would hand modem/DSP/firmware
 * pages to XNU.
 */
#define LISA_MEMORY_RANGES_DEFINE
#include <arm64/lisa_memory_ranges.h>

uint64_t
lisa_memory_ranges_total(void)
{
	uint64_t total = 0;
	for (unsigned int i = 0; i < LISA_MEMORY_RANGE_COUNT; i++) {
		total += lisa_memory_ranges[i].size;
	}
	return total;
}

boolean_t
lisa_memory_range_contains(uint64_t pa, uint64_t size)
{
	if (size == 0 || pa + size < pa) {
		return FALSE;
	}
	for (unsigned int i = 0; i < LISA_MEMORY_RANGE_COUNT; i++) {
		uint64_t begin = lisa_memory_ranges[i].base;
		uint64_t end = begin + lisa_memory_ranges[i].size;
		if (pa >= begin && pa + size <= end) {
			return TRUE;
		}
	}
	return FALSE;
}
