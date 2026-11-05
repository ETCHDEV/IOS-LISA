#include "gt9897_frame.h"
#include <assert.h>
#include <string.h>
static void seal(uint8_t *p, unsigned n) {
    unsigned s = 0; for (unsigned i = 0; i < n - 2; i++) s += p[i];
    p[n-2] = s; p[n-1] = s >> 8;
}
int main(void) {
    uint8_t h[8] = {0x80, 0, 1}, p[10] = {0x20, 0, 0x34, 0x02, 0x78, 0x05, 0x30, 0};
    struct gt9897_frame out = {0}; seal(h, 8); seal(p, 10);
    assert(gt9897_decode(h, 8, p, 10, &out) == 1);
    assert(out.count == 1 && out.contacts[0].id == 2 && out.contacts[0].x == 0x234 && out.contacts[0].y == 0x578);
    p[2] ^= 1; assert(gt9897_decode(h, 8, p, 10, &out) == -1); p[2] ^= 1;
    assert(gt9897_decode(h, 8, p, 9, &out) == -1);
    h[2] = 0; seal(h, 8); assert(gt9897_decode(h, 8, 0, 0, &out) == 1 && out.count == 0);
    h[0] = 0; seal(h, 8); assert(gt9897_decode(h, 8, 0, 0, &out) == 0);
    h[0] = 0x80; h[2] = 11; seal(h, 8); assert(gt9897_decode(h, 8, p, 10, &out) == -1);
    uint8_t two[18] = {0}; two[0] = 0x10; two[8] = 0x20;
    h[2] = 2; seal(h, 8); seal(two, 18);
    assert(gt9897_decode(h, 8, two, 18, &out) == 1 && out.count == 2);
    two[8] = 0x10; seal(two, 18);
    assert(gt9897_decode(h, 8, two, 18, &out) == -1);
    two[8] = 0x23; seal(two, 18);
    assert(gt9897_decode(h, 8, two, 18, &out) == 1 && out.count == 1);
    return 0;
}
