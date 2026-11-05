#pragma once
#include <stddef.h>
#include <stdint.h>

/* A half-open physical range. All protected ranges must be supplied by the
 * caller after independently establishing the current boot memory map. */
struct lisa_range { uint64_t base, size; };
struct lisa_payload_layout { uint64_t entry_pa, adt_pa, bootargs_pa, top_pa; };

/* Parses only the supplied bytes. No copies, device access, or kernel jump.
 * Returns 0 on success, -1 on malformed input or an unsafe overlap. This does
 * not prove the caller's chosen RAM or protected list is complete or safe. */
int lisa_validate_payload(const void *data, size_t length, uint64_t payload_pa,
    struct lisa_range initrd, struct lisa_range allowed_ram,
    const struct lisa_range *protected_ranges, size_t protected_count,
    struct lisa_payload_layout *layout);
