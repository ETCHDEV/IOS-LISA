// SPDX-License-Identifier: GPL-2.0-or-later
/* Protocol layout derived from Linux goodix_berlin_core.c.
 * That driver credits Goodix, Inc. and Linaro Ltd. */
#include "gt9897_frame.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static int checksum_ok(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    if (n < 2) return 0;
    for (size_t i = 0; i < n - 2; ++i) sum += p[i];
    return (uint16_t)sum == le16(p + n - 2);
}

int gt9897_decode(const uint8_t *h, size_t hn, const uint8_t *p,
    size_t pn, struct gt9897_frame *out)
{
    if (!h || !out || hn != 8 || !checksum_ok(h, hn)) return -1;
    if (!(h[0] & 0x80)) return 0;
    unsigned n = h[2] & 15;
    if (n > GT9897_MAX_CONTACTS || (n && (!p || pn != n * 8 + 2))) return -1;
    if (n && !checksum_ok(p, pn)) return -1;
    struct gt9897_frame result = {0};
    unsigned seen = 0;
    for (unsigned i = 0; i < n; ++i) {
        const uint8_t *q = p + 8 * i;
        unsigned id = q[0] >> 4, type = q[0] & 15;
        if (id >= GT9897_MAX_CONTACTS || (seen & (1u << id))) return -1;
        seen |= 1u << id;
        if (type == 1 || type == 3) continue; /* stylus unsupported */
        struct gt9897_contact *c = &result.contacts[result.count++];
        c->id = id; c->x = le16(q + 2); c->y = le16(q + 4); c->width = le16(q + 6);
    }
    *out = result;
    return 1;
}
