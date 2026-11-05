#include "payload_preflight.h"
#include "payload_tail.h"
#include <stdint.h>

int lisa_payload_preflight(const void *initrd_bytes, uint64_t initrd_pa,
    size_t initrd_size, struct lisa_range allowed_ram,
    const struct lisa_range *protected_ranges, size_t protected_count,
    struct lisa_payload_layout *layout)
{
    uint64_t payload_size;
    if (!initrd_bytes || !layout || initrd_size > UINT64_MAX - initrd_pa) return -1;
    const unsigned char *base = initrd_bytes;
    const unsigned char *payload = lisa_find_payload(base, initrd_size, &payload_size);
    if (!payload) return -1;
    uint64_t offset = (uint64_t)(payload - base);
    if (offset > UINT64_MAX - initrd_pa || payload_size > SIZE_MAX) return -1;
    struct lisa_range initrd = {initrd_pa, initrd_size};
    return lisa_validate_payload(payload, (size_t)payload_size, initrd_pa + offset,
        initrd, allowed_ram, protected_ranges, protected_count, layout);
}
