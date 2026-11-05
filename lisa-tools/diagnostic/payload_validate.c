#include "payload_validate.h"
#include <limits.h>

#ifndef LISA_PAYLOAD_LINE
#define LISA_PAYLOAD_LINE 608
#endif
#define HEADER (0x48u + LISA_PAYLOAD_LINE)
#define PAGE 0x4000u
#define MAX_SEGS 64u
#define MAX_PROTECTED 128u

static uint64_t u64(const unsigned char *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
    return v;
}
static uint32_t u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int add(uint64_t a, uint64_t b, uint64_t *out)
{
    if (b > UINT64_MAX - a) return 0;
    *out = a + b;
    return 1;
}
static int overlaps(uint64_t a, uint64_t ae, uint64_t b, uint64_t be)
{
    return a < be && b < ae;
}

int lisa_validate_payload(const void *data, size_t length, uint64_t payload_pa,
    struct lisa_range initrd, struct lisa_range allowed,
    const struct lisa_range *protected, size_t protected_count,
    struct lisa_payload_layout *layout)
{
    const unsigned char *p = data;
    static const unsigned char magic[] = "SWEETXNU";
    uint64_t rdend, ramend, pend, table, virt, entry, adtoff, adtsize, kend;
    uint64_t phys, mem, adtpa, padded, bapa, top, vas[MAX_SEGS], vaends[MAX_SEGS];
    uint64_t srcs[MAX_SEGS + 1], srcends[MAX_SEGS + 1];
    unsigned count, sources = 0, entry_found = 0;
    if (!p || !layout || (protected_count && !protected) || protected_count > MAX_PROTECTED ||
        length < HEADER || (payload_pa & 7) || !initrd.size || !allowed.size ||
        !add(initrd.base, initrd.size, &rdend) || !add(allowed.base, allowed.size, &ramend) ||
        !add(payload_pa, length, &pend) || payload_pa < initrd.base || pend > rdend ||
        overlaps(initrd.base, rdend, allowed.base, ramend)) return -1;
    for (size_t i = 0; i < protected_count; ++i) {
        uint64_t end;
        if (!protected[i].size || !add(protected[i].base, protected[i].size, &end) ||
            overlaps(allowed.base, ramend, protected[i].base, end)) return -1;
    }
    for (unsigned i = 0; i < 8; ++i) if (p[i] != magic[i]) return -1;
    count = u32(p + 12);
    if (u32(p + 8) != 1 || !count || count > MAX_SEGS) return -1;
    table = HEADER + (uint64_t)count * 32;
    if (table > length) return -1;
    virt = u64(p + 0x10); entry = u64(p + 0x18);
    adtoff = u64(p + 0x20); adtsize = u64(p + 0x28);
    kend = u64(p + 0x30); phys = u64(p + 0x38); mem = u64(p + 0x40);
    if (phys != allowed.base || mem != allowed.size || kend < virt ||
        !adtsize || adtsize > UINT32_MAX || (adtoff & 7) || adtoff < table ||
        !add(adtsize, 7, &padded)) return -1;
    padded &= ~7ULL;
    if (!add(adtoff, padded, &pend) || pend > length) return -1;
    srcs[sources] = adtoff; srcends[sources++] = pend;
    int terminated = 0;
    for (unsigned i = 0x48; i < HEADER; ++i) if (!p[i]) { terminated = 1; break; }
    if (!terminated) return -1;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned char *s = p + HEADER + i * 32;
        uint64_t va = u64(s), vs = u64(s+8), off = u64(s+16), fs = u64(s+24);
        uint64_t vaend, rounded, offend, dst, dstend;
        if (!vs || (va & 7) || (vs & 7) || (off & 7) || va < virt ||
            !add(va, vs, &vaend) || vaend > kend || fs > vs || !add(fs, 7, &rounded)) return -1;
        rounded &= ~7ULL;
        if (rounded > vs || off < table || !add(off, rounded, &offend) || offend > length ||
            !add(phys, va - virt, &dst) || !add(dst, vs, &dstend) || dstend > ramend) return -1;
        for (unsigned j = 0; j < i; ++j) if (overlaps(va, vaend, vas[j], vaends[j])) return -1;
        vas[i] = va; vaends[i] = vaend;
        if (entry >= va && entry < vaend) entry_found = 1;
        if (rounded) { srcs[sources] = off; srcends[sources++] = offend; }
    }
    if (!entry_found || (entry & 3)) return -1;
    for (unsigned i = 0; i < sources; ++i)
        for (unsigned j = 0; j < i; ++j)
            if (overlaps(srcs[i], srcends[i], srcs[j], srcends[j])) return -1;
    if (!add(phys, kend - virt, &adtpa) || (adtpa & (PAGE - 1)) ||
        !add(adtpa, adtsize, &bapa) || !add(bapa, PAGE - 1, &bapa)) return -1;
    bapa &= ~(uint64_t)(PAGE - 1);
    if (!add(bapa, PAGE, &top) || top > ramend) return -1;
    layout->entry_pa = phys + (entry - virt);
    layout->adt_pa = adtpa; layout->bootargs_pa = bapa; layout->top_pa = top;
    return 0;
}
