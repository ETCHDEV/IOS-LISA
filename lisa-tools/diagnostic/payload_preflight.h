#pragma once
#include "payload_validate.h"

/* Validates a complete, already readable initrd. Does not copy or execute.
 * The caller must independently establish that all input physical ranges
 * really describe accessible RAM and include all protected regions. */
int lisa_payload_preflight(const void *initrd_bytes, uint64_t initrd_pa,
    size_t initrd_size, struct lisa_range allowed_ram,
    const struct lisa_range *protected_ranges, size_t protected_count,
    struct lisa_payload_layout *layout);
