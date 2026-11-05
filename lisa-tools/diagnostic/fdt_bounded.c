#include "fdt_bounded.h"

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
        (uint32_t)p[2] << 8 | p[3];
}

static int bounded_string(const unsigned char *p, size_t size, const char *want)
{
    size_t i = 0;
    while (i < size && p[i] && want[i] && p[i] == (unsigned char)want[i]) ++i;
    return i < size && p[i] == 0 && want[i] == 0;
}

int lisa_fdt_chosen_u64(const void *blob, size_t accessible,
    const char *property, uint64_t *result)
{
    const unsigned char *b = blob;
    size_t total, so, ss, no, ns, pos = 0;
    unsigned depth = 0;
    int chosen = 0;
    if (!b || !property || !result || accessible < 40 || be32(b) != 0xd00dfeed) return 0;
    total = be32(b + 4); so = be32(b + 8); no = be32(b + 12);
    ns = be32(b + 32); ss = be32(b + 36);
    if (total < 40 || total > accessible || so < 40 || no < 40 ||
        (so & 3) || so > total || ss > total - so || no > total || ns > total - no)
        return 0;
    while (pos <= ss && ss - pos >= 4) {
        uint32_t token = be32(b + so + pos); pos += 4;
        if (token == 1) {
            size_t start = pos;
            while (pos < ss && b[so + pos]) ++pos;
            if (pos == ss || pos > SIZE_MAX - 4 || depth == UINT32_MAX) return 0;
            ++depth;
            if (depth == 2)
                chosen = bounded_string(b + so + start, pos - start + 1, "chosen");
            pos = (pos + 4) & ~(size_t)3;
            if (pos > ss) return 0;
        } else if (token == 2) {
            if (!depth) return 0;
            if (chosen && depth == 2) return 0;
            --depth;
        } else if (token == 3) {
            if (ss - pos < 8 || !depth) return 0;
            uint32_t len = be32(b + so + pos), name = be32(b + so + pos + 4);
            pos += 8;
            if (name >= ns || len > ss - pos || (size_t)len > SIZE_MAX - pos - 3) return 0;
            if (chosen && depth == 2 && bounded_string(b + no + name, ns - name, property)) {
                if (len != 4 && len != 8) return 0;
                uint64_t value = be32(b + so + pos);
                if (len == 8) value = value << 32 | be32(b + so + pos + 4);
                *result = value;
                return 1;
            }
            pos = (pos + len + 3) & ~(size_t)3;
            if (pos > ss) return 0;
        } else if (token == 4) {
            continue;
        } else {
            return 0;
        }
    }
    return 0;
}
