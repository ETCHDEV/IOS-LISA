#pragma once
#include <stddef.h>
#include <stdint.h>

/* The complete buffer must already be independently known to be readable.
 * Returns 1 if found, 0 for missing or malformed data. No device access. */
int lisa_fdt_chosen_u64(const void *blob, size_t accessible,
    const char *property, uint64_t *result);
