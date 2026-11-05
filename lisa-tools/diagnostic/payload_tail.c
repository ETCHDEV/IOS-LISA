#include "payload_tail.h"
#define FOOTER_SIZE 16
#define PAYLOAD_MIN 0x2a8
#define PAYLOAD_MAX (512ULL * 1024 * 1024)
const void *lisa_find_payload(const void *initrd, uint64_t length, uint64_t *payload_size)
{
    const unsigned char *p = initrd;
    static const unsigned char footmagic[8] = {'L','I','S','A','E','N','D','1'};
    static const unsigned char magic[8] = {'S','W','E','E','T','X','N','U'};
    if (!p || !payload_size || length < FOOTER_SIZE + PAYLOAD_MIN || length > PAYLOAD_MAX)
        return 0;
    const unsigned char *foot = p + length - FOOTER_SIZE;
    for (unsigned i = 0; i < 8; i++) if (foot[i] != footmagic[i]) return 0;
    uint64_t size = 0;
    for (unsigned i = 0; i < 8; i++) size |= (uint64_t)foot[8+i] << (8*i);
    if (size < PAYLOAD_MIN || size > length - FOOTER_SIZE) return 0;
    const unsigned char *payload = foot - size;
    if ((uintptr_t)payload & 7) return 0;
    for (unsigned i = 0; i < 8; i++) if (payload[i] != magic[i]) return 0;
    *payload_size = size;
    return payload;
}
